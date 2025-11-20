#include "src/server/rpc_server.hpp"
#include "src/general/detail.hpp"
#include<thread>
int main()
{
    lcz_rpc::server::RegistryServer regi_server(8080);
    regi_server.start();//启动注册中心服务器

    return 0;
}