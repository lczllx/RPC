// =============================================================================
// test_message_checks.cc — 消息 check() 失败路径（负例）
// -----------------------------------------------------------------------------
// 总测什么：
//   故意填错/漏填字段时，RpcRequest / TopicRequest / ServiceRequest::check()
//   是否返回 false（与业务校验规则一致）。
// 不测什么：
//   LV 往返、工厂创建成功路径。
//
// 分块说明：
//   §1 RpcRequest — 缺 method；params 非 object。
//   §2 TopicRequest — 缺 topic_key；PUBLISH 缺 topic_msg。
//   §3 ServiceRequest — REGISTER 缺 host；LOAD_REPORT 缺 load。
// =============================================================================

#include <gtest/gtest.h>

#include "src/general/message.hpp"

using lcz_rpc::RpcRequest;
using lcz_rpc::ServiceOpType;
using lcz_rpc::ServiceRequest;
using lcz_rpc::TopicOpType;
using lcz_rpc::TopicRequest;

// -----------------------------------------------------------------------------
// §1 RpcRequest 负例
// -----------------------------------------------------------------------------

// RpcRequest：缺少 method
TEST(MessageChecks, RpcRequestFailsWithoutMethod) {
  auto m = std::make_shared<RpcRequest>();
  Json::Value p(Json::objectValue);
  m->setParams(p);
  m->setMsgType(lcz_rpc::MsgType::REQ_RPC);
  EXPECT_FALSE(m->check());
}

// RpcRequest：parameters 不是 object
TEST(MessageChecks, RpcRequestFailsWhenParamsNotObject) {
  auto m = std::make_shared<RpcRequest>();
  m->setMethod("add");
  m->setParams(Json::Value("not-object"));
  m->setMsgType(lcz_rpc::MsgType::REQ_RPC);
  EXPECT_FALSE(m->check());
}

// -----------------------------------------------------------------------------
// §2 TopicRequest 负例
// -----------------------------------------------------------------------------

// TopicRequest：缺少 topic_key
TEST(MessageChecks, TopicRequestFailsWithoutTopicKey) {
  auto m = std::make_shared<TopicRequest>();
  m->setOptype(TopicOpType::SUBSCRIBE);
  m->setMsgType(lcz_rpc::MsgType::REQ_TOPIC);
  EXPECT_FALSE(m->check());
}

// TopicRequest：PUBLISH 但未设置 topic_msg
TEST(MessageChecks, TopicRequestPublishFailsWithoutMsg) {
  auto m = std::make_shared<TopicRequest>();
  m->setTopicKey("t");
  m->setOptype(TopicOpType::PUBLISH);
  m->setMsgType(lcz_rpc::MsgType::REQ_TOPIC);
  EXPECT_FALSE(m->check());
}

// -----------------------------------------------------------------------------
// §3 ServiceRequest 负例
// -----------------------------------------------------------------------------

// ServiceRequest：REGISTER 但缺少 host
TEST(MessageChecks, ServiceRegisterFailsWithoutHost) {
  auto m = std::make_shared<ServiceRequest>();
  m->setMethod("m");
  m->setOptype(ServiceOpType::REGISTER);
  m->setMsgType(lcz_rpc::MsgType::REQ_SERVICE);
  EXPECT_FALSE(m->check());
}

// ServiceRequest：LOAD_REPORT 但未带 load
TEST(MessageChecks, ServiceLoadReportFailsWithoutLoad) {
  auto m = std::make_shared<ServiceRequest>();
  m->setMethod("m");
  m->setOptype(ServiceOpType::LOAD_REPORT);
  m->setHost(std::make_pair(std::string("127.0.0.1"), 8080));
  m->setMsgType(lcz_rpc::MsgType::REQ_SERVICE);
  EXPECT_FALSE(m->check());
}
