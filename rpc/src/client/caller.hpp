#pragma once
#include "requestor.hpp"
#include "../general/message.hpp"
#include "../general/log_system/lcz_log.h"
#include <future>
#include <functional>

namespace lcz_rpc
{
    namespace client
    {
        //requestor里面的send是对basemessage进行处理
        //这里的caller是对rpcresponse里面的result进行处理
        // 路径二：call_proto 纯 Proto API，热路径零 JSON
        // RPC 调用封装类：封装 Requestor，提供同步/future/回调三种调用方式（JSON）+ call_proto（Proto）
        class RpcCaller
        {
            public:
            using ptr=std::shared_ptr<RpcCaller>;
            using RpcAsyncRespose=std::future<Json::Value>;
            using ResponseCallback=std::function<void(const Json::Value&)>;
            RpcCaller(const Requestor::ptr& reqtor):_requestor(reqtor){}
            // 同步 RPC：阻塞等待响应，结果写入 result
            bool call(const BaseConnection::ptr& conn,const std::string& method_name,const Json::Value& params,Json::Value& result)
            {
                LCZ_DEBUG("RpcCaller sync call method=%s", method_name.c_str());
                RpcRequest::ptr req_msg=MessageFactory::create<RpcRequest>();
                req_msg->setId(uuid());
                req_msg->setMsgType(MsgType::REQ_RPC);
                req_msg->setMethod(method_name);
                req_msg->setParams(params);
                BaseMessage::ptr resp_msg;
                bool ret= _requestor->send(conn,std::dynamic_pointer_cast<BaseMessage>(req_msg),resp_msg);
                if(!ret){LCZ_ERROR("rpc同步请求失败");return false;}
                RpcResponse::ptr rpc_respmsg=std::dynamic_pointer_cast<RpcResponse>(resp_msg);
                if(rpc_respmsg.get()==nullptr)
                {
                LCZ_ERROR("类型向下转换失败失败");return false; 
                }
                if(rpc_respmsg->rcode()!=RespCode::SUCCESS)
                {
                    LCZ_ERROR("rpc请求出错：%s",errReason(rpc_respmsg->rcode()).c_str());return false; 
                }
                result=rpc_respmsg->result();
                LCZ_DEBUG("RpcCaller sync call finish method=%s", method_name.c_str());
                return true;
            }
            // 异步 RPC：通过 result future 获取结果
            bool call(const BaseConnection::ptr& conn, const std::string& method_name,Json::Value& params,RpcAsyncRespose& result)
            {
                LCZ_DEBUG("RpcCaller future call method=%s", method_name.c_str());
                //向服务端发送异步回调请求，设置回调函数，在回调 函数中对pomise设置数据
                auto req_msg=MessageFactory::create<RpcRequest>();
                req_msg->setId(uuid());
                req_msg->setMsgType(MsgType::REQ_RPC);
                req_msg->setMethod(method_name);
                req_msg->setParams(params);
                BaseMessage::ptr resp_msg;
                
                auto json_pomise=std::make_shared<std::promise<Json::Value>>();//防止作用域结束销毁
                result=json_pomise->get_future();///创建 Promise-Future 对，通过 get_future() 连接

                Requestor::ReqCallback cb=std::bind(&RpcCaller::callBack,this,json_pomise,std::placeholders::_1);
                bool ret= _requestor->send(conn,std::dynamic_pointer_cast<BaseMessage>(req_msg),cb);
                if(!ret){LCZ_ERROR("rpc异步请求失败");return false;}

                return true;

            }
            // 回调式 RPC：响应到达时调用 cb
            bool call(const BaseConnection::ptr& conn, const std::string& method_name,Json::Value& params,const ResponseCallback& cb)
            {
                LCZ_DEBUG("RpcCaller callback call method=%s", method_name.c_str());
                auto req_msg=MessageFactory::create<RpcRequest>();
                req_msg->setId(uuid());
                req_msg->setMsgType(MsgType::REQ_RPC);
                req_msg->setMethod(method_name);
                req_msg->setParams(params);
                BaseMessage::ptr resp_msg;
               
                Requestor::ReqCallback reqcb=std::bind(&RpcCaller::callBackself,this,cb,std::placeholders::_1);
                bool ret= _requestor->send(conn,std::dynamic_pointer_cast<BaseMessage>(req_msg),reqcb);
                if(!ret){LCZ_ERROR("rpc回调请求失败");return false;}

                return true;
            }
            private:
            // 异步模式回调：校验 rcode 后设置 promise
            void callBack(std::shared_ptr<std::promise<Json::Value>> result,const BaseMessage::ptr& msg)
            {
                RpcResponse::ptr rpc_respmsg=std::dynamic_pointer_cast<RpcResponse>(msg);
                if(rpc_respmsg.get()==nullptr)
                {
                LCZ_ERROR("类型向下转换失败失败");return ; 
                }
                if(rpc_respmsg->rcode()!=RespCode::SUCCESS)
                {
                    LCZ_ERROR("rpc异步出错：%s",errReason(rpc_respmsg->rcode()).c_str());return; 
                }
                result->set_value(rpc_respmsg->result());//被触发时设置结果
            }
            // 回调模式：校验 rcode 后执行用户 cb
            void callBackself(const ResponseCallback &cb,const BaseMessage::ptr& msg)
            {
                RpcResponse::ptr rpc_respmsg=std::dynamic_pointer_cast<RpcResponse>(msg);
                if(rpc_respmsg.get()==nullptr)
                {
                LCZ_ERROR("类型向下转换失败失败");return ; 
                }
                if(rpc_respmsg->rcode()!=RespCode::SUCCESS)
                {
                    LCZ_ERROR("rpc回调出错：%s",errReason(rpc_respmsg->rcode()).c_str());return; 
                }
                cb(rpc_respmsg->result());//使用回调处理结果
            }

        public:
            // ---------- 路径二：纯 Proto API ----------
            // 同步 call_proto：Req/Resp 为 protobuf 类型，线缆为二进制，零 JSON
            template<typename Req, typename Resp>
            bool call_proto(const BaseConnection::ptr& conn, const std::string& method_name,
                           const Req& req, Resp* resp,
                           std::chrono::milliseconds timeout = std::chrono::seconds(5))
            {
                LCZ_DEBUG("RpcCaller call_proto sync method=%s", method_name.c_str());
                auto req_msg = MessageFactory::create<ProtoRpcRequest>();
                req_msg->setId(uuid());
                req_msg->setMsgType(MsgType::REQ_RPC_PROTO);
                req_msg->setMethod(method_name);
                std::string body;
                if (!req.SerializeToString(&body)) {
                    LCZ_ERROR("call_proto: Req::SerializeToString failed");
                    return false;
                }
                req_msg->setBody(body);

                BaseMessage::ptr resp_msg;
                if (!_requestor->send(conn, std::dynamic_pointer_cast<BaseMessage>(req_msg), resp_msg, timeout)) {
                    LCZ_ERROR("call_proto sync send failed");
                    return false;
                }
                auto proto_resp = std::dynamic_pointer_cast<ProtoRpcResponse>(resp_msg);
                if (!proto_resp) {
                    LCZ_ERROR("call_proto: response type not ProtoRpcResponse");
                    return false;
                }
                if (proto_resp->rcode() != RespCode::SUCCESS) {
                    LCZ_ERROR("call_proto error: %s", errReason(proto_resp->rcode()).c_str());
                    return false;
                }
                if (!resp->ParseFromString(proto_resp->body())) {
                    LCZ_ERROR("call_proto: Resp::ParseFromString failed");
                    return false;
                }
                LCZ_DEBUG("RpcCaller call_proto sync finish method=%s", method_name.c_str());
                return true;
            }

            // 异步 call_proto：通过 future<Resp> 获取结果（Resp 需默认构造）
            template<typename Req, typename Resp>
            bool call_proto(const BaseConnection::ptr& conn, const std::string& method_name,
                           const Req& req, std::future<Resp>* out_future,
                           std::chrono::milliseconds timeout = std::chrono::seconds(5))
            {
                LCZ_DEBUG("RpcCaller call_proto async method=%s", method_name.c_str());
                auto req_msg = MessageFactory::create<ProtoRpcRequest>();
                req_msg->setId(uuid());
                req_msg->setMsgType(MsgType::REQ_RPC_PROTO);
                req_msg->setMethod(method_name);
                std::string body;
                if (!req.SerializeToString(&body)) {
                    LCZ_ERROR("call_proto async: Req::SerializeToString failed");
                    return false;
                }
                req_msg->setBody(body);

                auto prom = std::make_shared<std::promise<Resp>>();
                *out_future = prom->get_future();
                Requestor::ReqCallback cb = [prom](const BaseMessage::ptr& msg) {
                    auto pr = std::dynamic_pointer_cast<ProtoRpcResponse>(msg);
                    if (!pr) {
                        LCZ_ERROR("call_proto async: response type not ProtoRpcResponse");
                        prom->set_exception(std::make_exception_ptr(std::runtime_error("invalid response type")));
                        return;
                    }
                    if (pr->rcode() != RespCode::SUCCESS) {
                        prom->set_exception(std::make_exception_ptr(std::runtime_error(errReason(pr->rcode()))));
                        return;
                    }
                    Resp r;
                    if (!r.ParseFromString(pr->body())) {
                        prom->set_exception(std::make_exception_ptr(std::runtime_error("ParseFromString failed")));
                        return;
                    }
                    prom->set_value(std::move(r));
                };
                if (!_requestor->send(conn, std::dynamic_pointer_cast<BaseMessage>(req_msg), cb)) {
                    LCZ_ERROR("call_proto async send failed");
                    return false;
                }
                return true;
            }

            // 回调式 call_proto
            template<typename Req, typename Resp>
            bool call_proto(const BaseConnection::ptr& conn, const std::string& method_name,
                           const Req& req, std::function<void(const Resp&)> on_success,
                           std::function<void(RespCode)> on_error = nullptr)
            {
                LCZ_DEBUG("RpcCaller call_proto callback method=%s", method_name.c_str());
                auto req_msg = MessageFactory::create<ProtoRpcRequest>();
                req_msg->setId(uuid());
                req_msg->setMsgType(MsgType::REQ_RPC_PROTO);
                req_msg->setMethod(method_name);
                std::string body;
                if (!req.SerializeToString(&body)) {
                    LCZ_ERROR("call_proto callback: Req::SerializeToString failed");
                    if (on_error) on_error(RespCode::INVALID_MSG);
                    return false;
                }
                req_msg->setBody(body);

                Requestor::ReqCallback cb = [on_success, on_error](const BaseMessage::ptr& msg) {
                    auto pr = std::dynamic_pointer_cast<ProtoRpcResponse>(msg);
                    if (!pr) {
                        LCZ_ERROR("call_proto callback: response type not ProtoRpcResponse");
                        if (on_error) on_error(RespCode::INVALID_MSG);
                        return;
                    }
                    if (pr->rcode() != RespCode::SUCCESS) {
                        if (on_error) on_error(pr->rcode());
                        return;
                    }
                    Resp r;
                    if (!r.ParseFromString(pr->body())) {
                        if (on_error) on_error(RespCode::PARSE_FAILED);
                        return;
                    }
                    if (on_success) on_success(r);
                };
                return _requestor->send(conn, std::dynamic_pointer_cast<BaseMessage>(req_msg), cb);
            }

            private:
            Requestor::ptr _requestor;
        };
    }
}