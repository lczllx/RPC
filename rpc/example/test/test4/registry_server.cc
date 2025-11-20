#include "src/server/rpc_server.hpp"
#include <iostream>
#include <thread>

int main()
{
    std::cout << "=== 注册中心服务器启动 ===" << std::endl;
    std::cout << "监听端口: 7070" << std::endl;
    std::cout << "心跳配置:" << std::endl;
    std::cout << "  - 检查间隔: 5秒" << std::endl;
    std::cout << "  - 空闲超时: 15秒" << std::endl;
    std::cout << "  - 初始宽限: 30秒" << std::endl;
    std::cout << "  - 心跳间隔: 10秒" << std::endl;
    std::cout << "  - 连接超时: 30秒" << std::endl;
    std::cout << "=========================" << std::endl;
    
    lcz_rpc::server::RegistryServer registry(7070);
    registry.start();
    
    // 保持运行
    std::this_thread::sleep_for(std::chrono::hours(1));
    
    return 0;
}

