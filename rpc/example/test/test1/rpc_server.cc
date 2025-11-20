#include "src/server/rpc_server.hpp"
#include "src/general/detail.hpp"
#include <thread>
void add(const Json::Value &req, Json::Value &resp)
{
    int num1 = req["num1"].asInt();
    int num2 = req["num2"].asInt();
    resp = num1 + num2;
}
int main()
{
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