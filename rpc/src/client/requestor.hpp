#pragma once

/*对客户端的每一个请求进行管理
发送请求时：创建ReqDescribe并存入映射表
等待响应时：根据请求类型使用不同机制等待结果
收到响应时：通过onResponse查找对应的请求描述，执行相应处理
清理资源：处理完成后删除请求描述，防止内存泄漏
*/
#include "../general/net.hpp"
#include "../general/message.hpp"
#include "../general/dispacher.hpp"
#include<future>
#include<functional>
#include<chrono>
#include "../general/publicconfig.hpp"
#include "muduo/net/EventLoop.h"
#include "muduo/net/TcpConnection.h"

namespace lcz_rpc
{
    namespace client
    {
        class Requestor
        {
            public:
            using ptr=std::shared_ptr<Requestor>;
            using ReqCallback=std::function<void(const BaseMessage::ptr&)>;
            using AsyncResponse=std::future<BaseMessage::ptr>;
            // 单个 RPC 请求的描述信息：记录请求类型、回调以及等待中的 promise
            struct ReqDescribe
            {
                using ptr=std::shared_ptr<ReqDescribe>;
                ReqType reqtype;
                ReqCallback callback;
                std::promise<BaseMessage::ptr> response;
                BaseMessage::ptr request;
                muduo::net::TimerId timer_id;  // 超时定时器 ID（用于取消）
                bool timeout_triggered = false;  // 是否已触发超时
            };
            // 处理服务端响应：匹配请求 id，触发对应的 promise 或回调
            void onResponse(const BaseConnection::ptr& conn,BaseMessage::ptr& msg)
            {
                std::string id=msg->rid();
                ReqDescribe::ptr req_desc=getDesc(id);
                if(req_desc.get()==nullptr)
                {
                    ELOG("收到 %s 响应，但消息描述不存在",id.c_str());
                    return;
                }
                
                // 检查是否已超时
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    if(req_desc->timeout_triggered)
                    {
                        WLOG("收到 %s 响应，但请求已超时，忽略响应", id.c_str());
                        delDescUnlocked(id);
                        return;
                    }
                    
                    // 取消定时器（如果存在）
                    if(req_desc->timer_id != muduo::net::TimerId())
                    {
                        // 获取 EventLoop 并取消定时器
                        auto* muduo_conn = dynamic_cast<MuduoConnection*>(conn.get());
                        if(muduo_conn && muduo_conn->getLoop())
                        {
                            muduo_conn->getLoop()->cancel(req_desc->timer_id);
                        }
                    }
                }
                
                if(req_desc->reqtype==ReqType::ASYNC)
                {
                    req_desc->response.set_value(msg);//设置结果
                }
                else if(req_desc->reqtype==ReqType::CALLBACK)
                {
                    if(req_desc->callback)req_desc->callback(msg);//回调处理
                }
                else{
                    ELOG("未知请求类型");
                }
                delDesc(id);//处理完删除掉这个描述信息
            }
            
            // 超时处理回调
            void onTimeout(const std::string& req_id)
            {
                ReqDescribe::ptr req_desc = getDesc(req_id);
                if(req_desc.get() == nullptr)
                {
                    DLOG("超时回调：请求 %s 已不存在（可能已收到响应）", req_id.c_str());
                    return;
                }
                
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    if(req_desc->timeout_triggered)
                    {
                        return;  // 已经处理过超时
                    }
                    req_desc->timeout_triggered = true;
                }
                
                ELOG("请求超时: id=%s", req_id.c_str());
                
                // 根据请求类型处理超时
                if(req_desc->reqtype == ReqType::ASYNC)
                {
                    // 创建一个超时响应消息
                    auto timeout_msg = MessageFactory::create<RpcResponse>();
                    timeout_msg->setId(req_id);
                    timeout_msg->setRcode(RespCode::TIMEOUT);
                    // 注意：这里不能直接 set_value，因为 promise 可能已经被设置
                    // 更好的方式是使用 shared_future 或者检查 promise 状态
                    // 简化处理：尝试设置，如果失败说明已经收到响应
                    try {
                        req_desc->response.set_value(timeout_msg);
                    } catch(...) {
                        // promise 已经被设置，说明响应已到达，忽略超时
                        DLOG("超时处理：请求 %s 已收到响应，忽略超时", req_id.c_str());
                    }
                }
                else if(req_desc->reqtype == ReqType::CALLBACK)
                {
                    // 回调模式：可以调用回调并传递超时错误
                    // 或者不调用，让调用方自己处理超时
                    WLOG("回调模式请求超时: id=%s，回调不会被调用", req_id.c_str());
                }
                
                delDesc(req_id);
            }
            //异步（带超时，默认5秒）
            bool send(const BaseConnection::ptr& conn,const BaseMessage::ptr& req,AsyncResponse& async_resp,
                     std::chrono::milliseconds timeout = std::chrono::seconds(5))
            {
                ReqDescribe::ptr req_desc=newDesc(req,ReqType::ASYNC);
                if(req_desc.get()==nullptr)
                {
                    ELOG("构造请求描述对象失败！");
                    return false;
                }
                
                // 设置超时定时器（使用 muduo 定时器）
                auto* muduo_conn = dynamic_cast<MuduoConnection*>(conn.get());
                if(muduo_conn && muduo_conn->getLoop())
                {
                    double timeout_sec = timeout.count() / 1000.0;
                    std::string req_id = req->rid();
                    req_desc->timer_id = muduo_conn->getLoop()->runAfter(timeout_sec, 
                        [this, req_id]() {
                            this->onTimeout(req_id);
                        });
                    DLOG("设置超时定时器: id=%s, timeout=%.2fs", req_id.c_str(), timeout_sec);
                }
                else
                {
                    WLOG("无法获取 EventLoop，超时机制不可用: id=%s", req->rid().c_str());
                }
                
                conn->send(req);//异步请求发送
                async_resp= req_desc->response.get_future();//获取关联的future对象
                return true;
            }
            //同步（带超时，默认5秒）
            bool send(const BaseConnection::ptr& conn,const BaseMessage::ptr& req,BaseMessage::ptr& resp,
                     std::chrono::milliseconds timeout = std::chrono::seconds(5))
            {
                DLOG("Requestor sync send id=%s, timeout=%ldms", req->rid().c_str(), timeout.count());
                AsyncResponse async_resp;
                if(send(conn,req,async_resp, timeout)==false)
                {
                    ELOG("Requestor sync send failed id=%s", req->rid().c_str());
                    return false;
                }
                
                // 使用 wait_for 实现超时
                if(async_resp.wait_for(timeout) == std::future_status::timeout)
                {
                    ELOG("Requestor sync recv timeout id=%s", req->rid().c_str());
                    onTimeout(req->rid());  // 触发超时处理
                    return false;
                }
                
                resp=async_resp.get();
                DLOG("Requestor sync recv id=%s", req->rid().c_str());
                return true;
            }
            //回调
            bool send(const BaseConnection::ptr& conn,const BaseMessage::ptr& req,const ReqCallback& cb)
            {
                ReqDescribe::ptr req_desc=newDesc(req,ReqType::CALLBACK,cb);
                if(req_desc.get()==nullptr)
                {
                    ELOG("构造请求描述对象失败！");
                    return false;
                }
                conn->send(req);
                return true;
            }
            private:
            ReqDescribe::ptr newDesc(const BaseMessage::ptr& req,ReqType req_type,const ReqCallback& cb=ReqCallback())
            {
                std::unique_lock<std::mutex> lock(_mutex);
                ReqDescribe::ptr req_desc=std::make_shared<ReqDescribe>();
                req_desc->reqtype=req_type;
                req_desc->request=req;
                if(req_type==ReqType::CALLBACK&&cb)req_desc->callback=cb;
                _request_desc[req->rid()]=req_desc;
                DLOG("newDesc add id=%s", req->rid().c_str());
                return req_desc;
            }
            ReqDescribe::ptr getDesc(const std::string& rid)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                auto it=_request_desc.find(rid);
                if(it!=_request_desc.end())
                {
                    return it->second;
                }
                return  ReqDescribe::ptr();               
            }
            void delDesc(std::string& rid)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                delDescUnlocked(rid);
            }
            
            void delDescUnlocked(const std::string& rid)
            {
                _request_desc.erase(rid);
            }
            private:
            std::mutex _mutex;
            std::unordered_map<std::string,ReqDescribe::ptr> _request_desc;//rid desc
        };
    }
}