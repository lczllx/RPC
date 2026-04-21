#include "src/server/rpc_server.hpp"
#include <iostream>
#include <thread>
int main()
{
    std::cout << "=== 注册中心服务器启动（test1）===" << std::endl;
    std::cout << "监听端口: 8080" << std::endl;
    std::cout << "心跳配置（默认）:" << std::endl;
    std::cout << "  - 扫描间隔: 5s" << std::endl;
    std::cout << "  - 空闲剔除: 15s" << std::endl;
    std::cout << "  - Provider 心跳: 10s" << std::endl;
    std::cout << "=================================" << std::endl;

    lcz_rpc::server::RegistryServer regi_server(8080);
    regi_server.start();//启动注册中心服务器

    return 0;
}