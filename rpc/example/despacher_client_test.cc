#include "src/general/net.hpp"
#include "src/general/dispacher.hpp"
#include "src/client/caller.hpp"
#include<thread>
void onrpcResponse(const lcz_rpc::BaseConnection::ptr& conn,lcz_rpc::RpcResponse::ptr& msg)
{
    LCZ_DEBUG("收到rpc响应");
    std::string msg_str = msg->serialize();
    std::cout << msg_str << std::endl;
   
    //创建rpc响应
    
}
void ontopicResponse(const lcz_rpc::BaseConnection::ptr& conn,lcz_rpc::TopicResponse::ptr& msg)
{
    LCZ_DEBUG("收到topic响应");

    std::string msg_str = msg->serialize();
    std::cout << msg_str << std::endl;
    //创建rpc响应
    
}


int main()
{
    auto dispacher=std::make_shared<lcz_rpc::Dispacher>();
    dispacher->registerhandler<lcz_rpc::RpcResponse>(lcz_rpc::MsgType::RSP_RPC,onrpcResponse);
    dispacher->registerhandler<lcz_rpc::TopicResponse>(lcz_rpc::MsgType::RSP_TOPIC,ontopicResponse);
    lcz_rpc::BaseClient::ptr client = lcz_rpc::ClientFactory::create("127.0.0.1",8889);
    auto msg_cb=std::bind(&lcz_rpc::Dispacher::onMessage,dispacher.get(),std::placeholders::_1,std::placeholders::_2);
    
    client->setMessageCallback(msg_cb);
    //client->setConnectionCallback(onConnection);
    client->connect();
    auto rpc_req=lcz_rpc::MessageFactory::create<lcz_rpc::RpcRequest>();
    rpc_req->setId("5555");
    rpc_req->setMsgType(lcz_rpc::MsgType::REQ_RPC);
    rpc_req->setMethod("add");
    Json::Value param;
    param["num1"]=1;
    param["num2"]=2;
    rpc_req->setParams(param);
    client->send(rpc_req);

    auto topic_req=lcz_rpc::MessageFactory::create<lcz_rpc::TopicRequest>();
    topic_req->setId("6666");
    topic_req->setTopicKey("news");
    topic_req->setTopicMsg("cnmsbxx");
    topic_req->setMsgType(lcz_rpc::MsgType::REQ_TOPIC);
    topic_req->setOptype(lcz_rpc::TopicOpType::CREATE);
    client->send(topic_req);

    std::this_thread::sleep_for(std::chrono::seconds(10));
    client->shutdown();
    return 0;
}

