#include <iostream>
#include "src/general/message.hpp"



int main()
{
//     lcz_rpc::RpcRequest::ptr request = lcz_rpc::MessageFactory::createMessage<lcz_rpc::RpcRequest>();//这里使用的是模板函数创建消息对象
//     // lcz_rpc::BaseMessage::ptr message = lcz_rpc::MessageFactory::createMessage(lcz_rpc::MsgType::REQ_RPC);
//     // lcz_rpc::RpcResponse::ptr response =std::dynamic_pointer_cast<lcz_rpc::RpcResponse>(message);//使用动态类型转换将BaseMessage转换为RpcResponse
//     Json::Value params;
//     params["num1"] = 1;
//     params["num2"] = 2;
//     request->setParams(params);
//     request->setMethod("add");
//     std::string serialized = request->serialize();
//     std::cout << serialized << std::endl;
    
//     lcz_rpc::BaseMessage::ptr message = lcz_rpc::MessageFactory::createMessage(MsgType::REQ_RPC);
//    message->unserialize(serialized);
//    lcz_rpc::RpcRequest::ptr request2 = std::dynamic_pointer_cast<lcz_rpc::RpcRequest>(message);
//    std::cout << request2->method() << std::endl;
//    std::cout << request2->params()["num1"].asInt() << std::endl;
//    std::cout << request2->params()["num2"].asInt() << std::endl;

    // lcz_rpc::TopicRequest::ptr topicrequest = lcz_rpc::MessageFactory::createMessage<lcz_rpc::TopicRequest>();
    // topicrequest->setTopicKey("news");
    // topicrequest->setOptype(lcz_rpc::TopicOpType::PUBLISH);
    // topicrequest->setTopicMsg("news content");
    // std::string serialized = topicrequest->serialize();
    // std::cout << serialized << std::endl;

    // lcz_rpc::BaseMessage::ptr message = lcz_rpc::MessageFactory::createMessage(lcz_rpc::MsgType::REQ_TOPIC);
    // bool ret = message->unserialize(serialized);
    // if(!ret)
    // {
    //     std::cout << "unserialize failed!" << std::endl;
    //     return -1;
    // }
    // ret=message->check();
    // if(!ret)
    // {
    //     std::cout << "check failed!" << std::endl;
    //     return -1;
    // }
    // lcz_rpc::TopicRequest::ptr topicrequest2 = std::dynamic_pointer_cast<lcz_rpc::TopicRequest>(message);
    // std::cout << topicrequest2->topicKey() << std::endl;
    // std::cout << static_cast<int>(topicrequest2->optype()) << std::endl;
    // std::cout << topicrequest2->topicMsg() << std::endl;

    // lcz_rpc::ServiceRequest::ptr servicerequest = lcz_rpc::MessageFactory::createMessage<lcz_rpc::ServiceRequest>();
    // servicerequest->setMethod("add");
    // servicerequest->setOptype(lcz_rpc::ServiceOpType::REGISTER);
    // servicerequest->setHost(std::make_pair("127.0.0.1",8080));
    // std::string serialized = servicerequest->serialize();
    // std::cout << serialized << std::endl;

    // lcz_rpc::BaseMessage::ptr message = lcz_rpc::MessageFactory::createMessage(lcz_rpc::MsgType::REQ_SERVICE);
    // bool ret = message->unserialize(serialized);
    // if(!ret)
    // {
    //     std::cout << "unserialize failed!" << std::endl;
    //     return -1;
    // }
    // ret=message->check();
    // if(!ret)
    // {
    //     std::cout << "check failed!" << std::endl;
    //     return -1;
    // }
    // lcz_rpc::ServiceRequest::ptr servicerequest2 = std::dynamic_pointer_cast<lcz_rpc::ServiceRequest>(message);
    // std::cout << servicerequest2->method() << std::endl;
    // std::cout << servicerequest2->host().first << std::endl;
    // std::cout << servicerequest2->host().second << std::endl;
    // std::cout << static_cast<int>(servicerequest2->optype()) << std::endl;
    // lcz_rpc::RpcResponse::ptr rpcresponse = lcz_rpc::MessageFactory::createMessage<lcz_rpc::RpcResponse>();
    // rpcresponse->setRcode(lcz_rpc::RespCode::SUCCESS);
    // rpcresponse->setResult(1);

    // std::string serialized = rpcresponse->serialize();
    // std::cout << serialized << std::endl;

    // lcz_rpc::BaseMessage::ptr message = lcz_rpc::MessageFactory::createMessage(lcz_rpc::MsgType::RSP_RPC);
    // bool ret = message->unserialize(serialized);
    // if(!ret)
    // {
    //     std::cout << "unserialize failed!" << std::endl;
    //     return -1;
    // }
    // ret=message->check();
    // if(!ret)
    // {
    //     std::cout << "check failed!" << std::endl;
    //     return -1;
    // }
    // lcz_rpc::RpcResponse::ptr rpcresponse2 = std::dynamic_pointer_cast<lcz_rpc::RpcResponse>(message);
    // std::cout << static_cast<int>(rpcresponse2->rcode()) << std::endl;
    // std::cout << rpcresponse2->result().asInt() << std::endl;

    // lcz_rpc::TopicResponse::ptr topicresponse = lcz_rpc::MessageFactory::createMessage<lcz_rpc::TopicResponse>();
    // topicresponse->setRcode(lcz_rpc::RespCode::SUCCESS);
    // topicresponse->setResult(1);
    // std::string serialized = topicresponse->serialize();
    // std::cout << serialized << std::endl;

    // lcz_rpc::BaseMessage::ptr message = lcz_rpc::MessageFactory::createMessage(lcz_rpc::MsgType::RSP_TOPIC);
    // bool ret = message->unserialize(serialized);
    // if(!ret)
    // {
    //     std::cout << "unserialize failed!" << std::endl;
    //     return -1;
    // }
    // ret=message->check();
    // if(!ret)
    // {
    //     std::cout << "check failed!" << std::endl;
    //     return -1;
    // }
    // lcz_rpc::TopicResponse::ptr topicresponse2 = std::dynamic_pointer_cast<lcz_rpc::TopicResponse>(message);
    // std::cout << static_cast<int>(topicresponse2->rcode()) << std::endl;
    // std::cout << topicresponse2->result().asInt() << std::endl;

    lcz_rpc::ServiceResponse::ptr serviceresponse = lcz_rpc::MessageFactory::createMessage<lcz_rpc::ServiceResponse>();
    serviceresponse->setRcode(lcz_rpc::RespCode::SUCCESS);
    serviceresponse->setMethod("add");
    serviceresponse->setOptype(lcz_rpc::ServiceOpType::DISCOVER);
    std::vector<lcz_rpc::HostInfo> hosts;
    hosts.push_back(std::make_pair("127.0.0.1",8080));
    hosts.push_back(std::make_pair("127.0.0.1",8081));
    serviceresponse->setHost(hosts);
    std::string serialized = serviceresponse->serialize();
    std::cout << serialized << std::endl;

    lcz_rpc::BaseMessage::ptr message = lcz_rpc::MessageFactory::createMessage(lcz_rpc::MsgType::RSP_SERVICE);
    bool ret = message->unserialize(serialized);
    if(!ret)
    {
        std::cout << "unserialize failed!" << std::endl;
        return -1;
    }
    ret=message->check();
    if(!ret)
    {
        std::cout << "check failed!" << std::endl;
        return -1;
    }
    lcz_rpc::ServiceResponse::ptr serviceresponse2 = std::dynamic_pointer_cast<lcz_rpc::ServiceResponse>(message);
    std::cout << static_cast<int>(serviceresponse2->rcode()) << std::endl;
    std::cout << serviceresponse2->method() << std::endl;
    std::cout << static_cast<int>(serviceresponse2->optype()) << std::endl;
    std::cout << serviceresponse2->hosts().size() << std::endl;
    for(auto host : serviceresponse2->hosts())
    {
        std::cout << host.first << ":" << host.second << std::endl;
    }
    return 0;
}