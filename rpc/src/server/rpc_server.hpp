#pragma once
#include "../general/dispacher.hpp"
#include "rpc_registry.hpp"
#include "rpc_router.hpp"
#include "../client/rpc_client.hpp"
#include "rpc_topic.hpp"
#include "../general/net.hpp"
#include <atomic>//原子操作
#include <iostream>
#include <thread>//线程支持
#include "../general/publicconfig.hpp"

namespace lcz_rpc
{
    namespace server
    {
        // 注册中心服务端类：提供服务注册/发现/心跳扫描
        class RegistryServer
        {
        public:
            using ptr = std::shared_ptr<RegistryServer>;
            RegistryServer(int port)
                : _pdmanager(std::make_shared<PwithDManager>())
                , _dispacher(std::make_shared<Dispacher>())
            {
                auto service_cb = std::bind(&PwithDManager::onserviceRequest, _pdmanager.get(), std::placeholders::_1, std::placeholders::_2);
                _dispacher->registerhandler<ServiceRequest>(MsgType::REQ_SERVICE, service_cb);
                _server = lcz_rpc::ServerFactory::create(port);
                auto msg_cb = std::bind(&lcz_rpc::Dispacher::onMessage, _dispacher.get(), std::placeholders::_1, std::placeholders::_2);
                _server->setMessageCallback(msg_cb);
                auto close_cb = std::bind(&RegistryServer::onconnShoutdown, this, std::placeholders::_1);
                _server->setCloseCallback(close_cb);
                // server->setConnectionCallback(onConnection);
                //启动心跳扫描定时器
                _hb_loop_ptr = _hb_loop.startLoop();//启动心跳扫描线程的事件循环
                _hb_loop_ptr->runEvery(_hb_config.check_interval_sec, [this]() {
                    ILOG("[RegistryServer-服务扫描] 开始扫描过期提供者，idle_timeout=%d秒", 
                         _hb_config.idle_timeout_sec);
                    auto expired = _pdmanager->sweepAndNotify(_hb_config.idle_timeout_sec);
                    if (!expired.empty()) {
                        ILOG("[RegistryServer-服务扫描] 发现 %zu 个过期提供者，已通知下线", expired.size());
                        // 不依赖日志宏等级，便于演示
                        for (const auto &pr : expired) {
                            std::cout << "[Registry] 剔除过期提供者 method=" << pr.first
                                      << " host=" << pr.second.first << ":" << pr.second.second
                                      << " (idle>" << _hb_config.idle_timeout_sec << "s)"
                                      << std::endl;
                        }
                    }
                });
            }
            // 启动注册中心服务器
            void start()
            {
                _server->start();
            }

        private:
            // 连接关闭时清理 provider/discoverer
            void onconnShoutdown(const BaseConnection::ptr &conn)
            {
                // demo 友好输出：Provider 进程退出/断开时，注册中心会立即感知连接断开
                // 注意：这种“即时下线”不会走超时扫描剔除逻辑
                std::cout << "[Registry] 连接断开，触发下线处理" << std::endl;
                _pdmanager->onconnShoutdown(conn);
            }

        private:
            Dispacher::ptr _dispacher;
            PwithDManager::ptr _pdmanager;
            BaseServer::ptr _server;

            // 心跳扫描定时器（Muduo库实现）
            HeartbeatConfig _hb_config;//心跳扫描配置
            muduo::net::EventLoopThread _hb_loop;//心跳扫描线程
            muduo::net::EventLoop* _hb_loop_ptr = nullptr;//心跳扫描线程指针

        };
        
        // RPC 服务端类：提供 RPC 方法注册与调用，可选向注册中心注册并心跳/负载上报
        class RpcServer
        {
        public:
            using ptr = std::shared_ptr<RpcServer>;
            // 两套地址信息：1.rpc服务提供的访问地址信息2.注册中心服务端地址信息
            RpcServer(const HostInfo &access_addr, bool enablediscover = false, const HostInfo &registry_server_addr=HostInfo("",0))
                : _access_addr(access_addr), _enablediscover(enablediscover), _dispacher(std::make_shared<Dispacher>()), _rpc_router(std::make_shared<RpcRouter>()), _proto_rpc_router(std::make_shared<ProtoRpcRouter>())
            {
                if (_enablediscover) // 如果启用服务发现，创建注册中心客户端
                {
                    _client_registry = std::make_shared<client::ClientRegistry>(registry_server_addr.first, registry_server_addr.second);
                    _report_loop_ptr=_report_loop.startLoop();//启动上报负载的线程的事件循环
                }
                // 注册 RPC 请求处理回调（JSON）
                auto rpc_cb = std::bind(&lcz_rpc::server::RpcRouter::onrpcRequst, _rpc_router.get(), std::placeholders::_1, std::placeholders::_2);
                _dispacher->registerhandler<lcz_rpc::RpcRequest>(lcz_rpc::MsgType::REQ_RPC, rpc_cb);
                // 路径二：纯 Proto RPC 请求处理
                auto proto_rpc_cb = std::bind(&lcz_rpc::server::ProtoRpcRouter::onProtoRequest, _proto_rpc_router.get(), std::placeholders::_1, std::placeholders::_2);
                _dispacher->registerhandler<lcz_rpc::ProtoRpcRequest>(lcz_rpc::MsgType::REQ_RPC_PROTO, proto_rpc_cb);
                 //// 创建网络服务器实例
                 _server = lcz_rpc::ServerFactory::create(access_addr.second);
                 // 设置消息处理回调
                 auto msg_cb = std::bind(&lcz_rpc::Dispacher::onMessage, _dispacher.get(), std::placeholders::_1, std::placeholders::_2);
                 _server->setMessageCallback(msg_cb);
 
                // RpcServer 仅维持 Provider 心跳与负载上报
            }
            // 注册 RPC 方法；若启用发现则同步向注册中心注册并启动心跳/负载上报
            void registerMethod(const ServiceDescribe::ptr &service)
            {
                if (_enablediscover)  // 如果启用服务发现，向注册中心注册方法
                {
                    int currentLoad = 10; // 临时写死，后续再做动态更新
                    bool ok = false;
                    // demo/工程稳定性：注册中心刚启动时可能有短暂不可用，这里做少量重试
                    for (int attempt = 1; attempt <= 3; ++attempt)
                    {
                        ok = _client_registry->methodRegistry(service->getMethodname(), _access_addr, currentLoad);
                        if (ok) break;
                        std::cout << "[Provider] 注册到 Registry 失败，method=" << service->getMethodname()
                                  << " host=" << _access_addr.first << ":" << _access_addr.second
                                  << " attempt=" << attempt << "/3"
                                  << std::endl;
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    if(ok)
                    {
                        std::cout << "[Provider] 注册成功 method=" << service->getMethodname()
                                  << " host=" << _access_addr.first << ":" << _access_addr.second
                                  << " load=" << currentLoad
                                  << std::endl;
                        {
                            std::unique_lock<std::mutex>lock(_methods_mutex);
                            _registered_methods.emplace_back(service->getMethodname());
                        }
                        if(!_report_started.exchange(true))//原子地将_report_started设置为true
                        {
                            //每3秒上报一次负载,绑定_client_registry的reportLoad方法
                            _report_loop_ptr->runEvery(
                                3.0,  // 周期按需配置
                                std::bind(&RpcServer::reportLoadTick, this));
                            //heartbeat_interval_sec秒发送一次心跳,绑定_client_registry的heartbeatTick方法
                            _report_loop_ptr->runEvery(
                                static_cast<double>(_hb_config.heartbeat_interval_sec)/*这是给runEvery方法的参数，表示心跳间隔时间*/,
                                std::bind(&RpcServer::heartbeatTick, this));
                        }
                    }
                    else
                    {
                        std::cout << "[Provider] 注册到 Registry 最终失败，method=" << service->getMethodname()
                                  << " host=" << _access_addr.first << ":" << _access_addr.second
                                  << std::endl;
                    }

                }
                  // 在路由器中注册方法（线程安全）
                _rpc_router->registerMethod(service);
            }
            // 路径二：注册纯 Proto RPC 方法，热路径零 JSON
            template<typename Req, typename Resp>
            void registerProtoHandler(const std::string& method,
                std::function<void(const BaseConnection::ptr&, const Req&, Resp*)> handler)
            {
                _proto_rpc_router->registerProtoHandler<Req, Resp>(method, std::move(handler));
            }
            // 启动服务器（阻塞）
            void start() { _server->start(); }
            
            // 在后台线程启动服务器，主线程可继续调用 registerMethod
            void startInThread() 
            { 
                if (_server_thread.joinable()) {
                    WLOG("RpcServer 已经在运行中");
                    return;
                }
                _server_thread = std::thread([this]() {
                    _server->start();  // 在单独线程中阻塞运行
                });
                ILOG("RpcServer 已在后台线程启动，主线程可继续调用 registerMethod()");
            }
            
            // 等待 startInThread 启动的线程结束
            void wait() 
            {
                if (_server_thread.joinable()) {
                    _server_thread.join();
                }
            }
            
            // 析构函数：确保线程正确退出
            ~RpcServer() 
            {
                if (_server_thread.joinable()) {
                    // 注意：这里无法直接停止 muduo 的事件循环
                    // 实际应用中可能需要添加 stop() 方法来优雅关闭
                    _server_thread.join();
                }
            }
        private:
            // 获取当前负载（占位实现，后续可动态更新）
            int currentLoad()const
            {
                static int fake = 0;
                return (fake += 5) % 100;
                // 后续再做动态更新
            }
            // 定时器回调：向注册中心上报当前负载
            void reportLoadTick()
            {
                if (!_enablediscover || !_client_registry) return;
                const int load = currentLoad();

                std::vector<std::string> methods;
                {
                    std::unique_lock<std::mutex> lock(_methods_mutex);
                    methods = _registered_methods;//获取已注册的方法
                }
                for (const auto &method : methods) {
                    if (!_client_registry->reportLoad(method, _access_addr, load)) {
                        WLOG("reportLoad 失败: method=%s", method.c_str());
                    }
                }
            }
            // 定时器回调：向注册中心发送心跳
            void heartbeatTick()
            {
                if (!_enablediscover || !_client_registry) return;//如果未启用服务发现或注册中心客户端为空，则返回
                std::vector<std::string> methods;
                { 
                    std::unique_lock<std::mutex> lock(_methods_mutex);
                    methods = _registered_methods;//获取已注册的方法
                }
                ILOG("[RpcServer-Provider心跳定时器] 开始发送心跳，method数量=%zu", methods.size());
                //遍历已注册的方法，发送心跳给注册中心
                for (const auto &method : methods) {
                    //发送心跳给注册中心
                    if (!_client_registry->heartbeatProvider(method, _access_addr)) {
                        WLOG("[RpcServer-Provider心跳失败] method=%s", method.c_str());
                    }
                }
            }

        private:
            HostInfo _access_addr;// 本机RPC服务访问地址
            bool _enablediscover;//是否启用服务发现
            client::ClientRegistry::ptr _client_registry;//注册中心客户端
            Dispacher::ptr _dispacher;//消息分发器
            RpcRouter::ptr _rpc_router;//RPC路由器
            ProtoRpcRouter::ptr _proto_rpc_router;//路径二：纯 Proto RPC 路由器
            BaseServer::ptr _server;//网络服务器

            HeartbeatConfig _hb_config; // 心跳配置

            //这是和负载上报相关的设置
            muduo::net::EventLoopThread _report_loop;//上报负载的线程
            muduo::net::EventLoop * _report_loop_ptr = nullptr;//上报负载的线程指针
            muduo::net::TimerId _report_timer;//上报负载的定时器
            std::mutex _methods_mutex;//方法互斥锁
            std::vector<std::string> _registered_methods;//已注册的方法
            std::atomic<bool> _report_started{false};//上报负载的线程是否启动
            std::thread _server_thread;//服务器运行线程（用于非阻塞启动）
        };
        // 主题服务端类：提供主题的创建/删除/订阅/发布
        class TopicServer
        {
        public:
            using ptr = std::shared_ptr<TopicServer>;
            TopicServer(int port)
                : _topicmanager(std::make_shared<TopicManager>()), _dispacher(std::make_shared<Dispacher>())
            {
                auto service_cb = std::bind(&TopicManager::ontopicRequest, _topicmanager.get(), std::placeholders::_1, std::placeholders::_2);
                _dispacher->registerhandler<TopicRequest>(MsgType::REQ_TOPIC, service_cb);
                _server = lcz_rpc::ServerFactory::create(port);
                auto msg_cb = std::bind(&lcz_rpc::Dispacher::onMessage, _dispacher.get(), std::placeholders::_1, std::placeholders::_2);
                _server->setMessageCallback(msg_cb);
                auto close_cb = std::bind(&TopicServer::onconnShoutdown, this, std::placeholders::_1);
                _server->setCloseCallback(close_cb);
            }
            // 启动主题服务器
            void start()
            {
                _server->start();
            }

        private:
            // 连接关闭时清理订阅者
            void onconnShoutdown(const BaseConnection::ptr &conn)
            {
                _topicmanager->onconnShoutdown(conn);
            }

        private:
            lcz_rpc::server::TopicManager::ptr _topicmanager;
            Dispacher::ptr _dispacher;
            BaseServer::ptr _server;

        };

    } // namespace server
}