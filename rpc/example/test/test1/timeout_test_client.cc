// 超时测试：只发一次同步调用，服务端不响应则应在约 5 秒后超时并正常退出
#include "src/client/rpc_client.hpp"
#include "src/general/detail.hpp"
#include <iostream>

int main()
{
    std::cout << "[超时测试] 连接并发送一次 sync call，服务端 10s 才响应，客户端 5s 超时..." << std::endl;
    lcz_rpc::client::RpcClient client(true, "127.0.0.1", 8080);
    Json::Value params, result;
    params["num1"] = 1;
    params["num2"] = 2;

    bool ret = client.call("add", params, result);
    if (ret)
        std::cout << "[超时测试] result: " << result.asInt() << std::endl;
    else
        std::cout << "[超时测试] 按预期超时，调用失败" << std::endl;

    std::cout << "[超时测试] 结束" << std::endl;
    return 0;
}
