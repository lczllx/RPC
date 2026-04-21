// =============================================================================
// test_message_factory_exhaustive.cc — MessageFactory 全覆盖表驱动测试
// -----------------------------------------------------------------------------
// 总测什么：
//   对「全部」合法 MsgType（12 种）各测一次：非空、msgType 一致、dynamic_pointer_cast
//   到预期具体类型成功。
// 与 test_message_factory.cc 区别：
//   本文件一张表扫完所有枚举；前者按业务分条、含非法枚举用例。
// =============================================================================

#include <gtest/gtest.h>

#include "src/general/message.hpp"

#include <memory>

using lcz_rpc::BaseMessage;
using lcz_rpc::MessageFactory;
using lcz_rpc::MsgType;
using lcz_rpc::ProtoRpcRequest;
using lcz_rpc::ProtoRpcResponse;
using lcz_rpc::ProtoServiceRequest;
using lcz_rpc::ProtoServiceResponse;
using lcz_rpc::ProtoTopicRequest;
using lcz_rpc::ProtoTopicResponse;
using lcz_rpc::RpcRequest;
using lcz_rpc::RpcResponse;
using lcz_rpc::ServiceRequest;
using lcz_rpc::ServiceResponse;
using lcz_rpc::TopicRequest;
using lcz_rpc::TopicResponse;

// MessageFactory 对「全部」合法 MsgType 均能创建对象且 msgType 一致
TEST(MessageFactoryExhaustive, AllMsgTypesConstructAndMatch) {
  const struct {
    MsgType t;
    bool (*is_right)(const BaseMessage::ptr&);
  } kCases[] = {
      {MsgType::REQ_RPC,
       [](const BaseMessage::ptr& m) { return (bool)std::dynamic_pointer_cast<RpcRequest>(m); }},
      {MsgType::RSP_RPC,
       [](const BaseMessage::ptr& m) { return (bool)std::dynamic_pointer_cast<RpcResponse>(m); }},
      {MsgType::REQ_TOPIC,
       [](const BaseMessage::ptr& m) { return (bool)std::dynamic_pointer_cast<TopicRequest>(m); }},
      {MsgType::RSP_TOPIC,
       [](const BaseMessage::ptr& m) { return (bool)std::dynamic_pointer_cast<TopicResponse>(m); }},
      {MsgType::REQ_SERVICE,
       [](const BaseMessage::ptr& m) { return (bool)std::dynamic_pointer_cast<ServiceRequest>(m); }},
      {MsgType::RSP_SERVICE,
       [](const BaseMessage::ptr& m) { return (bool)std::dynamic_pointer_cast<ServiceResponse>(m); }},
      {MsgType::REQ_RPC_PROTO,
       [](const BaseMessage::ptr& m) { return (bool)std::dynamic_pointer_cast<ProtoRpcRequest>(m); }},
      {MsgType::RSP_RPC_PROTO,
       [](const BaseMessage::ptr& m) { return (bool)std::dynamic_pointer_cast<ProtoRpcResponse>(m); }},
      {MsgType::REQ_TOPIC_PROTO,
       [](const BaseMessage::ptr& m) { return (bool)std::dynamic_pointer_cast<ProtoTopicRequest>(m); }},
      {MsgType::RSP_TOPIC_PROTO,
       [](const BaseMessage::ptr& m) { return (bool)std::dynamic_pointer_cast<ProtoTopicResponse>(m); }},
      {MsgType::REQ_SERVICE_PROTO,
       [](const BaseMessage::ptr& m) { return (bool)std::dynamic_pointer_cast<ProtoServiceRequest>(m); }},
      {MsgType::RSP_SERVICE_PROTO,
       [](const BaseMessage::ptr& m) { return (bool)std::dynamic_pointer_cast<ProtoServiceResponse>(m); }},
  };

  for (const auto& c : kCases) {
    auto m = MessageFactory::create(c.t);
    ASSERT_NE(m.get(), nullptr) << "msgtype=" << static_cast<int>(c.t);
    EXPECT_EQ(m->msgType(), c.t);
    EXPECT_TRUE(c.is_right(m)) << "wrong concrete type for msgtype=" << static_cast<int>(c.t);
  }
}
