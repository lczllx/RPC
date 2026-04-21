#include "src/server/rpc_server.hpp"
#include <iostream>
#include <thread>
void add(const Json::Value &req, Json::Value &resp)
{
    int num1 = req["num1"].asInt();
    int num2 = req["num2"].asInt();
    resp = num1 + num2;
}
int main()
{
    std::cout << "=== RPC 服务提供者启动（test1）===" << std::endl;
    std::cout << "服务地址: 127.0.0.1:8889" << std::endl;
    std::cout << "注册中心: 127.0.0.1:8080" << std::endl;
    std::cout << "提供服务: add(num1:int, num2:int) -> int" << std::endl;
    std::cout << "心跳间隔: 10s（Provider -> Registry）" << std::endl;
    std::cout << "=================================" << std::endl;

    std::unique_ptr<lcz_rpc::server::ServiceFactory> req_factory(new lcz_rpc::server::ServiceFactory());
    req_factory->setMethodName("add");
    req_factory->setParamdescribe("num1", lcz_rpc::server::ValType::INTEGRAL);
    req_factory->setParamdescribe("num2", lcz_rpc::server::ValType::INTEGRAL);
    req_factory->setReturntype(lcz_rpc::server::ValType::INTEGRAL);
    req_factory->setServiceCallback(add);

    lcz_rpc::server::RpcServer server(lcz_rpc::HostInfo("127.0.0.1", 8889),true,lcz_rpc::HostInfo("127.0.0.1", 8080));
    server.registerMethod(req_factory->build());

    // server->setConnectionCallback(onConnection);
    server.start();
    return 0;
}