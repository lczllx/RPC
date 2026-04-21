// =============================================================================
// test_lv_more_messages.cc — LV 协议 + 各类业务消息（扩展往返）
// -----------------------------------------------------------------------------
// 总测什么：
//   在 test_lv_protocol.cc 之外，补充 Topic / Service / 全 Proto 路径上的
//   serialize → StringBuffer → onMessage 往返，覆盖「能合法 check()」的典型填法。
// 不测什么：
//   muduo、注册中心进程、负载均衡运行时行为。
//
// 分块说明：
//   §1 Topic（JSON）— 订阅、带 FANOUT 的发布、TopicResponse。
//   §2 Service（JSON）— 注册、负载上报、发现响应（多实例列表）。
//   §3 Proto RPC — ProtoRpcRequest / ProtoRpcResponse。
//   §4 Proto Topic — ProtoTopicRequest / ProtoTopicResponse。
//   §5 Proto Service — 发现请求与带主机列表的发现响应。
// 辅助：匿名命名空间内 ExpectLvRoundTrip 模板，减少重复代码。
// =============================================================================

#include <gtest/gtest.h>

#include "src/general/message.hpp"
#include "src/general/net.hpp"
#include "string_buffer.hpp"

using lcz_rpc::HostInfo;
using lcz_rpc::LVProtocol;
using lcz_rpc::MsgType;
using lcz_rpc::ProtoRpcRequest;
using lcz_rpc::ProtoRpcResponse;
using lcz_rpc::ProtoServiceRequest;
using lcz_rpc::ProtoServiceResponse;
using lcz_rpc::ProtoTopicRequest;
using lcz_rpc::ProtoTopicResponse;
using lcz_rpc::RespCode;
using lcz_rpc::ServiceOpType;
using lcz_rpc::ServiceRequest;
using lcz_rpc::ServiceResponse;
using lcz_rpc::TopicForwardStrategy;
using lcz_rpc::TopicOpType;
using lcz_rpc::TopicRequest;
using lcz_rpc::TopicResponse;
using lcz_rpc::test::StringBuffer;

namespace {

// 通用：给定已通过 check() 的消息，验证 LV 往返后仍可 check 且类型一致
template <typename MsgPtr>
void ExpectLvRoundTrip(MsgPtr original) {
  LVProtocol proto;
  std::string wire = proto.serialize(original);
  ASSERT_FALSE(wire.empty()) << "serialize empty";
  auto buf = std::make_shared<StringBuffer>(std::move(wire));
  ASSERT_TRUE(proto.canProcessed(buf));
  lcz_rpc::BaseMessage::ptr parsed;
  ASSERT_TRUE(proto.onMessage(buf, parsed));
  ASSERT_NE(parsed.get(), nullptr);
  auto round = std::dynamic_pointer_cast<typename MsgPtr::element_type>(parsed);
  ASSERT_NE(round.get(), nullptr);
  EXPECT_TRUE(round->check());
}

}  // namespace

// -----------------------------------------------------------------------------
// §1 Topic（JSON）
// -----------------------------------------------------------------------------

TEST(LVMore, TopicRequestSubscribeRoundTrip) {
  auto m = std::make_shared<TopicRequest>();
  m->setTopicKey("events.user");
  m->setOptype(TopicOpType::SUBSCRIBE);
  m->setId("tid-1");
  m->setMsgType(MsgType::REQ_TOPIC);
  ASSERT_TRUE(m->check());
  ExpectLvRoundTrip(m);
}

TEST(LVMore, TopicRequestPublishWithFanoutRoundTrip) {
  auto m = std::make_shared<TopicRequest>();
  m->setTopicKey("news");
  m->setOptype(TopicOpType::PUBLISH);
  m->setTopicMsg("hello");
  m->setForwardStrategy(TopicForwardStrategy::FANOUT);
  m->setFanoutLimit(3);
  m->setId("tid-2");
  m->setMsgType(MsgType::REQ_TOPIC);
  ASSERT_TRUE(m->check());
  ExpectLvRoundTrip(m);
}

TEST(LVMore, TopicResponseRoundTrip) {
  auto m = std::make_shared<TopicResponse>();
  m->setMsgType(MsgType::RSP_TOPIC);
  m->setRcode(RespCode::SUCCESS);
  m->setResult(Json::Value("done"));
  m->setId("tr-1");
  ASSERT_TRUE(m->check());
  ExpectLvRoundTrip(m);
}

// -----------------------------------------------------------------------------
// §2 Service（JSON）
// -----------------------------------------------------------------------------

TEST(LVMore, ServiceRequestRegisterRoundTrip) {
  auto m = std::make_shared<ServiceRequest>();
  m->setMethod("demo.Echo");
  m->setOptype(ServiceOpType::REGISTER);
  m->setHost(HostInfo{"127.0.0.1", 9000});
  m->setId("sr-1");
  m->setMsgType(MsgType::REQ_SERVICE);
  ASSERT_TRUE(m->check());
  ExpectLvRoundTrip(m);
}

TEST(LVMore, ServiceRequestLoadReportRoundTrip) {
  auto m = std::make_shared<ServiceRequest>();
  m->setMethod("demo.Echo");
  m->setOptype(ServiceOpType::LOAD_REPORT);
  m->setHost(HostInfo{"10.0.0.1", 8080});
  m->setLoad(42);
  m->setId("sr-2");
  m->setMsgType(MsgType::REQ_SERVICE);
  ASSERT_TRUE(m->check());
  ExpectLvRoundTrip(m);
}

TEST(LVMore, ServiceResponseDiscoverRoundTrip) {
  auto m = std::make_shared<ServiceResponse>();
  m->setMsgType(MsgType::RSP_SERVICE);
  m->setRcode(RespCode::SUCCESS);
  m->setOptype(ServiceOpType::DISCOVER);
  m->setMethod("demo.Echo");
  std::vector<HostInfo> hosts;
  hosts.push_back({"127.0.0.1", 7001});
  hosts.push_back({"127.0.0.1", 7002});
  m->setHost(hosts);
  m->setId("srv-1");
  ASSERT_TRUE(m->check());
  ExpectLvRoundTrip(m);
}

// -----------------------------------------------------------------------------
// §3 Proto RPC
// -----------------------------------------------------------------------------

TEST(LVMore, ProtoRpcRequestResponseRoundTrip) {
  auto req = std::make_shared<ProtoRpcRequest>();
  req->setMethod("Echo");
  req->setBody("payload-bytes");
  req->setId("p1");
  req->setMsgType(MsgType::REQ_RPC_PROTO);
  ASSERT_TRUE(req->check());
  ExpectLvRoundTrip(req);

  auto rsp = std::make_shared<ProtoRpcResponse>();
  rsp->setRcode(RespCode::SUCCESS);
  rsp->setBody("ok");
  rsp->setId("p2");
  rsp->setMsgType(MsgType::RSP_RPC_PROTO);
  ASSERT_TRUE(rsp->check());
  ExpectLvRoundTrip(rsp);
}

// -----------------------------------------------------------------------------
// §4 Proto Topic
// -----------------------------------------------------------------------------

TEST(LVMore, ProtoTopicRequestResponseRoundTrip) {
  auto req = std::make_shared<ProtoTopicRequest>();
  req->setTopicKey("tk");
  req->setOptype(TopicOpType::SUBSCRIBE);
  req->setId("pt1");
  req->setMsgType(MsgType::REQ_TOPIC_PROTO);
  ASSERT_TRUE(req->check());
  ExpectLvRoundTrip(req);

  auto rsp = std::make_shared<ProtoTopicResponse>();
  rsp->setRcode(RespCode::SUCCESS);
  rsp->setResult("ok");
  rsp->setId("pt2");
  rsp->setMsgType(MsgType::RSP_TOPIC_PROTO);
  ASSERT_TRUE(rsp->check());
  ExpectLvRoundTrip(rsp);
}

// -----------------------------------------------------------------------------
// §5 Proto Service
// -----------------------------------------------------------------------------

TEST(LVMore, ProtoServiceRequestDiscoverRoundTrip) {
  auto m = std::make_shared<ProtoServiceRequest>();
  m->setMethod("svc.Method");
  m->setOptype(ServiceOpType::DISCOVER);
  m->setId("ps1");
  m->setMsgType(MsgType::REQ_SERVICE_PROTO);
  ASSERT_TRUE(m->check());
  ExpectLvRoundTrip(m);
}

TEST(LVMore, ProtoServiceResponseDiscoverRoundTrip) {
  auto m = std::make_shared<ProtoServiceResponse>();
  m->setMsgType(MsgType::RSP_SERVICE_PROTO);
  m->setRcode(RespCode::SUCCESS);
  m->setOptype(ServiceOpType::DISCOVER);
  m->setMethod("svc.Method");
  std::vector<HostInfo> hosts;
  hosts.emplace_back("127.0.0.1", 6001);
  m->setHost(hosts);
  m->setId("ps2");
  ASSERT_TRUE(m->check());
  ExpectLvRoundTrip(m);
}
