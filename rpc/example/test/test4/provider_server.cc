#include "src/server/rpc_server.hpp"
#include "src/general/detail.hpp"
#include <iostream>
#include <thread>

void add(const Json::Value& req, Json::Value& resp)
{
    int num1 = req["num1"].asInt();
    int num2 = req["num2"].asInt();
    resp = num1 + num2;
}

int main()
{
    std::cout << "=== RPC 服务提供者启动 ===" << std::endl;
    std::cout << "服务地址: 127.0.0.1:8889" << std::endl;
    std::cout << "注册中心: 127.0.0.1:7070" << std::endl;
    std::cout << "心跳间隔: 10秒" << std::endl;
    std::cout << "========================" << std::endl;
    
    // 创建服务工厂
    std::unique_ptr<lcz_rpc::server::ServiceFactory> req_factory(new lcz_rpc::server::ServiceFactory());
    req_factory->setMethodName("add");
    req_factory->setParamdescribe("num1", lcz_rpc::server::ValType::INTEGRAL);
    req_factory->setParamdescribe("num2", lcz_rpc::server::ValType::INTEGRAL);
    req_factory->setReturntype(lcz_rpc::server::ValType::INTEGRAL);
    req_factory->setServiceCallback(add);
    
    // 创建 RPC 服务器（启用服务发现）
    lcz_rpc::server::RpcServer server(
        lcz_rpc::HostInfo("127.0.0.1", 8889),  // access_addr
        true,                                   // enablediscover
        lcz_rpc::HostInfo("127.0.0.1", 7070)   // registry_server_addr
    );
    server.registerMethod(req_factory->build());
    
    server.start();
    
    std::cout << "服务已启动，按 Ctrl+C 停止..." << std::endl;
    
    // 保持运行
    std::this_thread::sleep_for(std::chrono::hours(1));
    
    return 0;
}

