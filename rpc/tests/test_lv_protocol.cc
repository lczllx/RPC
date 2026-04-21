// =============================================================================
// test_lv_protocol.cc — LV 定帧协议（基础行为）
// -----------------------------------------------------------------------------
// 总测什么：
//   LVProtocol 对「JSON 系」消息的 serialize → 内存缓冲 → onMessage 是否与发送前一致；
//   以及半包、粘包（连续两帧）场景。
// 不测什么：
//   真实 muduo 连接、注册中心、RpcServer/RpcClient 全链路。
//
// 分块说明：
//   §1 RpcRequest / RpcResponse — JSON RPC 请求与响应的往返。
//   §2 ServiceRequest(DISCOVER) — 服务发现请求（无 host）的往返。
//   §3a 半包 — 长度不足时不可解析。
//   §3b 粘包 — 同一缓冲内连续两帧。
// =============================================================================

#include <gtest/gtest.h>

#include "src/general/message.hpp"
#include "src/general/net.hpp"
#include "string_buffer.hpp"

using lcz_rpc::LVProtocol;
using lcz_rpc::MsgType;
using lcz_rpc::RespCode;
using lcz_rpc::RpcRequest;
using lcz_rpc::RpcResponse;
using lcz_rpc::ServiceOpType;
using lcz_rpc::ServiceRequest;
using lcz_rpc::test::StringBuffer;

// -----------------------------------------------------------------------------
// §1 JSON RPC：请求 / 响应往返
// -----------------------------------------------------------------------------

// LV 帧：serialize → StringBuffer → onMessage 还原为 RpcRequest
TEST(LVProtocol, RoundTripRpcRequest) {
  auto original = std::make_shared<RpcRequest>();
  original->setMethod("add");
  Json::Value params(Json::objectValue);
  params["x"] = 1;
  params["y"] = 2;
  original->setParams(params);
  original->setId("test-rid-001");
  original->setMsgType(MsgType::REQ_RPC);
  ASSERT_TRUE(original->check());

  LVProtocol proto;
  std::string wire = proto.serialize(original);
  ASSERT_FALSE(wire.empty());

  auto buf = std::make_shared<StringBuffer>(std::move(wire));
  ASSERT_TRUE(proto.canProcessed(buf));

  lcz_rpc::BaseMessage::ptr parsed;
  ASSERT_TRUE(proto.onMessage(buf, parsed));
  ASSERT_NE(parsed.get(), nullptr);

  auto round = std::dynamic_pointer_cast<RpcRequest>(parsed);
  ASSERT_NE(round.get(), nullptr);
  EXPECT_EQ(round->rid(), "test-rid-001");
  EXPECT_EQ(round->msgType(), MsgType::REQ_RPC);
  EXPECT_EQ(round->method(), "add");
  EXPECT_TRUE(round->check());
}

// §1（续）JSON RPC 响应往返
// RpcResponse 往返（含 rcode、result）
TEST(LVProtocol, RoundTripRpcResponse) {
  auto original = std::make_shared<RpcResponse>();
  original->setMsgType(MsgType::RSP_RPC);
  original->setRcode(RespCode::SUCCESS);
  original->setResult(Json::Value("ok"));
  original->setId("rsp-1");
  ASSERT_TRUE(original->check());

  LVProtocol proto;
  std::string wire = proto.serialize(original);
  auto buf = std::make_shared<StringBuffer>(std::move(wire));
  ASSERT_TRUE(proto.canProcessed(buf));

  lcz_rpc::BaseMessage::ptr parsed;
  ASSERT_TRUE(proto.onMessage(buf, parsed));
  auto round = std::dynamic_pointer_cast<RpcResponse>(parsed);
  ASSERT_NE(round.get(), nullptr);
  EXPECT_EQ(round->rid(), "rsp-1");
  EXPECT_EQ(round->rcode(), RespCode::SUCCESS);
  EXPECT_TRUE(round->check());
}

// -----------------------------------------------------------------------------
// §2 注册中心侧 JSON：服务发现请求（DISCOVER 可无 host）
// -----------------------------------------------------------------------------

// 服务发现请求：DISCOVER 不要求 KEY_HOST（与 Provider 注册不同）
TEST(LVProtocol, RoundTripServiceRequestDiscover) {
  auto original = std::make_shared<ServiceRequest>();
  original->setMethod("demo.Method");
  original->setOptype(ServiceOpType::DISCOVER);
  original->setId("svc-1");
  original->setMsgType(MsgType::REQ_SERVICE);
  ASSERT_TRUE(original->check());

  LVProtocol proto;
  std::string wire = proto.serialize(original);
  auto buf = std::make_shared<StringBuffer>(std::move(wire));
  ASSERT_TRUE(proto.canProcessed(buf));

  lcz_rpc::BaseMessage::ptr parsed;
  ASSERT_TRUE(proto.onMessage(buf, parsed));
  auto round = std::dynamic_pointer_cast<ServiceRequest>(parsed);
  ASSERT_NE(round.get(), nullptr);
  EXPECT_EQ(round->optype(), ServiceOpType::DISCOVER);
  EXPECT_EQ(round->method(), "demo.Method");
  EXPECT_TRUE(round->check());
}

// -----------------------------------------------------------------------------
// §3a 半包：可读字节不足一整帧
// -----------------------------------------------------------------------------

// 半包：可读长度不足一整帧时 canProcessed 为 false
TEST(LVProtocol, IncompleteFrameNotReady) {
  LVProtocol proto;
  std::string fragment(2, '\0');
  auto buf = std::make_shared<StringBuffer>(std::move(fragment));
  EXPECT_FALSE(proto.canProcessed(buf));
}

// -----------------------------------------------------------------------------
// §3b 粘包：两帧拼在同一缓冲
// -----------------------------------------------------------------------------

// 同一缓冲区连续两帧：第一次 onMessage 后剩余数据仍可解析第二帧
TEST(LVProtocol, TwoConsecutiveFrames) {
  LVProtocol proto;

  auto a = std::make_shared<RpcRequest>();
  a->setMethod("m1");
  Json::Value p(Json::objectValue);
  a->setParams(p);
  a->setId("a");
  a->setMsgType(MsgType::REQ_RPC);
  ASSERT_TRUE(a->check());

  auto b = std::make_shared<RpcRequest>();
  b->setMethod("m2");
  b->setParams(p);
  b->setId("b");
  b->setMsgType(MsgType::REQ_RPC);
  ASSERT_TRUE(b->check());

  std::string combined = proto.serialize(a) + proto.serialize(b);
  auto buf = std::make_shared<StringBuffer>(std::move(combined));

  lcz_rpc::BaseMessage::ptr first;
  ASSERT_TRUE(proto.onMessage(buf, first));
  auto r1 = std::dynamic_pointer_cast<RpcRequest>(first);
  ASSERT_NE(r1.get(), nullptr);
  EXPECT_EQ(r1->rid(), "a");

  ASSERT_TRUE(proto.canProcessed(buf));
  lcz_rpc::BaseMessage::ptr second;
  ASSERT_TRUE(proto.onMessage(buf, second));
  auto r2 = std::dynamic_pointer_cast<RpcRequest>(second);
  ASSERT_NE(r2.get(), nullptr);
  EXPECT_EQ(r2->rid(), "b");
}
