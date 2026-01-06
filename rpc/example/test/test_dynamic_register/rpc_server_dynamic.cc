#include "src/server/rpc_server.hpp"
#include "src/general/detail.hpp"
#include <thread>
#include <chrono>
#include <iostream>

// 示例服务方法1：加法
void add(const Json::Value &req, Json::Value &resp)
{
    int num1 = req["num1"].asInt();
    int num2 = req["num2"].asInt();
    resp = num1 + num2;
    std::cout << "[add] 被调用: " << num1 << " + " << num2 << " = " << resp.asInt() << std::endl;
}

// 示例服务方法2：乘法
void multiply(const Json::Value &req, Json::Value &resp)
{
    int num1 = req["num1"].asInt();
    int num2 = req["num2"].asInt();
    resp = num1 * num2;
    std::cout << "[multiply] 被调用: " << num1 << " * " << num2 << " = " << resp.asInt() << std::endl;
}

// 示例服务方法3：减法（稍后动态注册）
void subtract(const Json::Value &req, Json::Value &resp)
{
    int num1 = req["num1"].asInt();
    int num2 = req["num2"].asInt();
    resp = num1 - num2;
    std::cout << "[subtract] 被调用: " << num1 << " - " << num2 << " = " << resp.asInt() << std::endl;
}

int main()
{
    // 创建 RPC 服务器（启用服务发现）
    lcz_rpc::server::RpcServer server(
        lcz_rpc::HostInfo("127.0.0.1", 8889),
        true,  // 启用服务发现
        lcz_rpc::HostInfo("127.0.0.1", 8080));  // 注册中心地址

    // ========== 启动前注册服务 ==========
    std::cout << "\n========== 启动前注册服务 ==========" << std::endl;
    
    // 注册 add 方法
    std::unique_ptr<lcz_rpc::server::ServiceFactory> add_factory(new lcz_rpc::server::ServiceFactory());
    add_factory->setMethodName("add");
    add_factory->setParamdescribe("num1", lcz_rpc::server::ValType::INTEGRAL);
    add_factory->setParamdescribe("num2", lcz_rpc::server::ValType::INTEGRAL);
    add_factory->setReturntype(lcz_rpc::server::ValType::INTEGRAL);
    add_factory->setServiceCallback(add);
    server.registerMethod(add_factory->build());
    std::cout << "✓ 已注册方法: add" << std::endl;

    // 注册 multiply 方法
    std::unique_ptr<lcz_rpc::server::ServiceFactory> mul_factory(new lcz_rpc::server::ServiceFactory());
    mul_factory->setMethodName("multiply");
    mul_factory->setParamdescribe("num1", lcz_rpc::server::ValType::INTEGRAL);
    mul_factory->setParamdescribe("num2", lcz_rpc::server::ValType::INTEGRAL);
    mul_factory->setReturntype(lcz_rpc::server::ValType::INTEGRAL);
    mul_factory->setServiceCallback(multiply);
    server.registerMethod(mul_factory->build());
    std::cout << "✓ 已注册方法: multiply" << std::endl;

    // ========== 非阻塞启动服务器 ==========
    std::cout << "\n========== 启动服务器（非阻塞） ==========" << std::endl;
    server.startInThread();  // 在后台线程启动，主线程不阻塞
    std::cout << "✓ 服务器已在后台线程启动" << std::endl;
    
    // 等待服务器完全启动
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // ========== 启动后动态注册服务 ==========
    std::cout << "\n========== 启动后动态注册服务 ==========" << std::endl;
    
    // 等待 2 秒后注册 subtract 方法
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    std::unique_ptr<lcz_rpc::server::ServiceFactory> sub_factory(new lcz_rpc::server::ServiceFactory());
    sub_factory->setMethodName("subtract");
    sub_factory->setParamdescribe("num1", lcz_rpc::server::ValType::INTEGRAL);
    sub_factory->setParamdescribe("num2", lcz_rpc::server::ValType::INTEGRAL);
    sub_factory->setReturntype(lcz_rpc::server::ValType::INTEGRAL);
    sub_factory->setServiceCallback(subtract);
    server.registerMethod(sub_factory->build());
    std::cout << "✓ 已动态注册方法: subtract（服务器运行中）" << std::endl;

    // ========== 主线程继续运行 ==========
    std::cout << "\n========== 服务器运行中，主线程可继续执行其他操作 ==========" << std::endl;
    std::cout << "提示：可以使用客户端测试 add、multiply、subtract 方法" << std::endl;
    std::cout << "按 Ctrl+C 退出..." << std::endl;

    // 等待服务器线程（实际应用中可以用 wait() 或信号处理）
    server.wait();  // 阻塞等待服务器线程结束

    return 0;
}

