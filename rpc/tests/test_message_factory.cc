// =============================================================================
// test_message_factory.cc — MessageFactory 分组冒烟测试
// -----------------------------------------------------------------------------
// 总测什么：
//   MessageFactory::create(MsgType) 能否创建非空对象，且 msgType 与具体类型匹配；
//   以及非法 MsgType 是否返回 nullptr。
// 不测什么：
//   消息字段合法性（见 test_message_checks.cc）；LV 线缆（见 test_lv_*.cc）。
//
// 分块说明：
//   §1 JSON RPC / Service / Topic — 各 create 一对 req/rsp。
//   §2 Proto — REQ_RPC_PROTO 创建为 ProtoRpcRequest。
//   §3 异常 — 非法枚举走 default 分支。
// =============================================================================

#include <gtest/gtest.h>

#include "src/general/message.hpp"

using lcz_rpc::MessageFactory;
using lcz_rpc::MsgType;
using lcz_rpc::ProtoRpcRequest;
using lcz_rpc::RpcRequest;
using lcz_rpc::RpcResponse;
using lcz_rpc::ServiceRequest;
using lcz_rpc::ServiceResponse;
using lcz_rpc::TopicRequest;
using lcz_rpc::TopicResponse;

// -----------------------------------------------------------------------------
// §1 JSON：RPC / Service / Topic
// -----------------------------------------------------------------------------

// 工厂为各 MsgType 创建非空对象，且 msgType 与枚举一致
TEST(MessageFactory, CreatesJsonRpcTypes) {
  auto req = MessageFactory::create(MsgType::REQ_RPC);
  ASSERT_NE(req.get(), nullptr);
  EXPECT_EQ(req->msgType(), MsgType::REQ_RPC);
  EXPECT_NE(std::dynamic_pointer_cast<RpcRequest>(req).get(), nullptr);

  auto rsp = MessageFactory::create(MsgType::RSP_RPC);
  ASSERT_NE(rsp.get(), nullptr);
  EXPECT_EQ(rsp->msgType(), MsgType::RSP_RPC);
  EXPECT_NE(std::dynamic_pointer_cast<RpcResponse>(rsp).get(), nullptr);
}

TEST(MessageFactory, CreatesServiceTypes) {
  auto req = MessageFactory::create(MsgType::REQ_SERVICE);
  ASSERT_NE(req.get(), nullptr);
  EXPECT_EQ(req->msgType(), MsgType::REQ_SERVICE);
  EXPECT_NE(std::dynamic_pointer_cast<ServiceRequest>(req).get(), nullptr);

  auto rsp = MessageFactory::create(MsgType::RSP_SERVICE);
  ASSERT_NE(rsp.get(), nullptr);
  EXPECT_EQ(rsp->msgType(), MsgType::RSP_SERVICE);
  EXPECT_NE(std::dynamic_pointer_cast<ServiceResponse>(rsp).get(), nullptr);
}

TEST(MessageFactory, CreatesTopicTypes) {
  auto req = MessageFactory::create(MsgType::REQ_TOPIC);
  ASSERT_NE(req.get(), nullptr);
  EXPECT_EQ(req->msgType(), MsgType::REQ_TOPIC);
  EXPECT_NE(std::dynamic_pointer_cast<TopicRequest>(req).get(), nullptr);

  auto rsp = MessageFactory::create(MsgType::RSP_TOPIC);
  ASSERT_NE(rsp.get(), nullptr);
  EXPECT_EQ(rsp->msgType(), MsgType::RSP_TOPIC);
  EXPECT_NE(std::dynamic_pointer_cast<TopicResponse>(rsp).get(), nullptr);
}

// -----------------------------------------------------------------------------
// §2 Proto（示例：REQ_RPC_PROTO）
// -----------------------------------------------------------------------------

TEST(MessageFactory, CreatesProtoTypes) {
  auto pr = MessageFactory::create(MsgType::REQ_RPC_PROTO);
  ASSERT_NE(pr.get(), nullptr);
  EXPECT_EQ(pr->msgType(), MsgType::REQ_RPC_PROTO);
  EXPECT_NE(std::dynamic_pointer_cast<ProtoRpcRequest>(pr).get(), nullptr);
}

// -----------------------------------------------------------------------------
// §3 非法 MsgType
// -----------------------------------------------------------------------------

// 非法枚举值走 default，返回空指针
TEST(MessageFactory, InvalidMsgTypeReturnsNull) {
  auto bad = MessageFactory::create(static_cast<MsgType>(9999));
  EXPECT_EQ(bad.get(), nullptr);
}
