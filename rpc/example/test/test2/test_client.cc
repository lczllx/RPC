
#include "src/client/rpc_client.hpp"
#include "src/general/detail.hpp"
#include<thread>
void onrpcResponse(const lcz_rpc::BaseConnection::ptr& conn,lcz_rpc::RpcResponse::ptr& msg)
{
    DLOG("收到rpc响应");
    std::string msg_str = msg->serialize();
    std::cout << msg_str << std::endl;
   
    
    
}
void ontopicResponse(const lcz_rpc::BaseConnection::ptr& conn,lcz_rpc::TopicResponse::ptr& msg)
{
    DLOG("收到topic响应");

    std::string msg_str = msg->serialize();
    std::cout << msg_str << std::endl;
    
    
}


int main()
{
    lcz_rpc::client::RpcClient client(false,"127.0.0.1",8889);
    //同步请求
    Json::Value params,result;
    params["num1"]=66;
    params["num2"]=33;
    
    bool ret=client.call("add",params,result);
    if(ret)std::cout<<"result:"<<result.asInt()<<std::endl;
    else std::cout<<"调用失败"<<std::endl;

    Json::Value paramss,resultt;
    lcz_rpc::client::RpcCaller::RpcAsyncRespose resp_future;
    paramss["num1"]=66;
    paramss["num2"]=3;
    
    ret=client.call("add",paramss,resp_future);
    if(ret){
        resultt=resp_future.get();
        std::cout<<"result:"<<resultt.asInt()<<std::endl;
    }
    else std::cout<<"调用失败"<<std::endl;
    return 0;
}

