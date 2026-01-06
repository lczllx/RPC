#pragma once
#include "../general/dispacher.hpp"
#include "rpc_registry.hpp"
#include "rpc_router.hpp"
#include "../client/rpc_client.hpp"
#include "rpc_topic.hpp"
#include "../general/net.hpp"
#include <atomic>//原子操作
#include <thread>//线程支持
#include "../general/publicconfig.hpp"

namespace lcz_rpc
{
    namespace server
    {
        // 注册中心服务端
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
                    }
                });
            }
            void start()
            {
                _server->start();
            }

        private:
            void onconnShoutdown(const BaseConnection::ptr &conn)
            {
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
        
        // 不带注册中心的 RPC 服务，如果开启发现则自动向注册中心注册方法
        class RpcServer
        {
        public:
            using ptr = std::shared_ptr<RpcServer>;
            // 两套地址信息：1.rpc服务提供的访问地址信息2.注册中心服务端地址信息
            RpcServer(const HostInfo &access_addr, bool enablediscover = false, const HostInfo &registry_server_addr=HostInfo("",0))
                : _access_addr(access_addr), _enablediscover(enablediscover), _dispacher(std::make_shared<Dispacher>()), _rpc_router(std::make_shared<RpcRouter>())
            {
                if (_enablediscover) // 如果启用服务发现，创建注册中心客户端
                {
                    _client_registry = std::make_shared<client::ClientRegistry>(registry_server_addr.first, registry_server_addr.second);
                    _report_loop_ptr=_report_loop.startLoop();//启动上报负载的线程的事件循环
                }
                // 注册RPC请求处理回调
                auto rpc_cb = std::bind(&lcz_rpc::server::RpcRouter::onrpcRequst, _rpc_router.get(), std::placeholders::_1, std::placeholders::_2);
                _dispacher->registerhandler<lcz_rpc::RpcRequest>(lcz_rpc::MsgType::REQ_RPC, rpc_cb);
                 //// 创建网络服务器实例
                 _server = lcz_rpc::ServerFactory::create(access_addr.second);
                 // 设置消息处理回调
                 auto msg_cb = std::bind(&lcz_rpc::Dispacher::onMessage, _dispacher.get(), std::placeholders::_1, std::placeholders::_2);
                 _server->setMessageCallback(msg_cb);
 
                // RpcServer 仅维持 Provider 心跳与负载上报
            }
            void registerMethod(const ServiceDescribe::ptr &service)
            {
                if (_enablediscover)  // 如果启用服务发现，向注册中心注册方法
                {
                    int currentLoad = 10; // 临时写死，后续再做动态更新
                    if(_client_registry->methodRegistry(service->getMethodname(), _access_addr, currentLoad))
                    {
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

                }
                  // 在路由器中注册方法（线程安全）
                _rpc_router->registerMethod(service);

            }
            // 启动服务器（阻塞版本，在调用线程中运行事件循环）
            void start() { _server->start(); }
            
            // 启动服务器（非阻塞版本，在单独线程中运行事件循环，支持动态注册服务）
            //解决start后不能注册服务的问题
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
            
            // 等待服务器线程结束
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
            int currentLoad()const
            {
                static int fake = 0;
                return (fake += 5) % 100;
                // 后续再做动态更新
            }
            void reportLoadTick()//上报负载的定时器回调函数
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
            //心跳扫描定时器回调函数
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
        //主题服务器
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
            void start()
            {
                _server->start();
            }

        private:
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