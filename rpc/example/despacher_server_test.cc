#include "src/general/net.hpp"
#include "src/general/dispacher.hpp"
#include "src/server/rpc_router.hpp"

void onrpcRequst(const lcz_rpc::BaseConnection::ptr& conn,lcz_rpc::RpcRequest::ptr& msg)
{
    LCZ_DEBUG("收到rpc请求，%s,",msg->method().c_str());
    std::string msg_str = msg->serialize();
    std::cout << msg_str << std::endl;
    //创建rpc响应
    auto rpc_resp=lcz_rpc::MessageFactory::create<lcz_rpc::RpcResponse>();
    rpc_resp->setId("5555");
    rpc_resp->setMsgType(lcz_rpc::MsgType::RSP_RPC);
    rpc_resp->setRcode(lcz_rpc::RespCode::SUCCESS);
    rpc_resp->setResult(11);
    conn->send(rpc_resp);

}
void ontopicRequst(const lcz_rpc::BaseConnection::ptr& conn,lcz_rpc::TopicRequest::ptr& msg)
{
    LCZ_DEBUG("收到topic请求,%s,",msg->method().c_str());

    std::string msg_str = msg->serialize();
    std::cout << msg_str << std::endl;
    //创建topic响应
    auto topic_resp=lcz_rpc::MessageFactory::create<lcz_rpc::TopicResponse>();
    topic_resp->setId("6666");
    topic_resp->setMsgType(lcz_rpc::MsgType::RSP_TOPIC);
    topic_resp->setRcode(lcz_rpc::RespCode::SUCCESS);
    topic_resp->setResult("Topic created successfully");
    conn->send(topic_resp);
   
    
}

int main()
{
    auto dispacher=std::make_shared<lcz_rpc::Dispacher>();
    dispacher->registerhandler<lcz_rpc::RpcRequest>(lcz_rpc::MsgType::REQ_RPC,onrpcRequst);
    dispacher->registerhandler<lcz_rpc::TopicRequest>(lcz_rpc::MsgType::REQ_TOPIC,ontopicRequst);
    lcz_rpc::BaseServer::ptr server = lcz_rpc::ServerFactory::create(8889);
    auto msg_cb=std::bind(&lcz_rpc::Dispacher::onMessage,dispacher.get(),std::placeholders::_1,std::placeholders::_2);
   
    server->setMessageCallback(msg_cb);

    
    //server->setConnectionCallback(onConnection);
    server->start();
    return 0;
}

