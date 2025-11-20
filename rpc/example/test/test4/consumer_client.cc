#include "src/client/rpc_client.hpp"
#include "src/general/detail.hpp"
#include <iostream>
#include <thread>

int main()
{
    std::cout << "=== RPC 客户端（消费者）启动 ===" << std::endl;
    std::cout << "注册中心: 127.0.0.1:7070" << std::endl;
    std::cout << "心跳间隔: 10秒" << std::endl;
    std::cout << "=============================" << std::endl;
    
    // 创建 RPC 客户端（启用服务发现）
    lcz_rpc::client::RpcClient client(true, "127.0.0.1", 7070);
    
    // 等待服务发现
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 尝试调用服务（这会触发服务发现，从而启动 Consumer 心跳）
    Json::Value params, result;
    params["num1"] = 10;
    params["num2"] = 20;
    
    std::cout << "尝试调用服务 add(10, 20)..." << std::endl;
    bool ret = client.call("add", params, result);
    
    if (ret) {
        std::cout << "调用成功，结果: " << result.asInt() << std::endl;
    } else {
        std::cout << "调用失败（服务可能还未注册）" << std::endl;
    }
    
    std::cout << "客户端已启动，Consumer 心跳将每10秒发送一次..." << std::endl;
    std::cout << "按 Ctrl+C 停止..." << std::endl;
    
    // 保持运行，观察心跳日志
    std::this_thread::sleep_for(std::chrono::hours(1));
    
    return 0;
}

