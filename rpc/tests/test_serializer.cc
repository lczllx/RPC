// =============================================================================
// test_serializer.cc — ISerializer 序列化器接口单测
// -----------------------------------------------------------------------------
// 总测什么：
//   ProtobufSerializer 对 ProtoRpcRequest 编解码往返一致、
//   解码损坏数据返回 false、name() 返回标识符、
//   std::shared_ptr<ISerializer> 多态调用正确。
// 不测什么：
//   FlatBuffers 实现（尚未实现）、网络传输。
//
// 分块说明：
//   §1 ProtobufSerializer — 正常编解码
//   §2 ProtobufSerializer — 解码损坏数据
//   §3 多态 — shared_ptr<ISerializer> 调用
// =============================================================================

#include <gtest/gtest.h>

#include "src/general/serializer.hpp"
#include "src/general/message.hpp"

using lcz_rpc::BaseMessage;
using lcz_rpc::ISerializer;
using lcz_rpc::ProtobufSerializer;
using lcz_rpc::MessageFactory;
using lcz_rpc::MsgType;
using lcz_rpc::ProtoRpcRequest;

// -----------------------------------------------------------------------------
// §1 ProtobufSerializer — 正常编解码
// -----------------------------------------------------------------------------

// ProtoRpcRequest 编码后解码，字段应一致
TEST(ProtobufSerializer, EncodeDecodeRoundtrip) {
  ProtobufSerializer ser;

  auto req = MessageFactory::create<ProtoRpcRequest>();
  ASSERT_NE(req, nullptr);
  req->setId("msg-001");
  req->setMsgType(MsgType::REQ_RPC_PROTO);
  req->setMethod("add");
  req->setTraceId("trace-abc123");
  req->setSpanId("0");
  req->setBody("test-body");

  // 编码
  std::string wire = ser.encode(req);
  EXPECT_FALSE(wire.empty());

  // 解码到新对象
  auto decoded_raw = MessageFactory::create<ProtoRpcRequest>();
  ASSERT_NE(decoded_raw, nullptr);
  BaseMessage::ptr decoded = decoded_raw; // 转为基类引用供 decode 写回
  bool ok = ser.decode(wire, decoded);
  EXPECT_TRUE(ok);

  // 验证字段
  auto typed = std::dynamic_pointer_cast<ProtoRpcRequest>(decoded);
  ASSERT_NE(typed, nullptr);
  EXPECT_EQ(typed->method(), "add");
  EXPECT_EQ(typed->trace_id(), "trace-abc123");
  EXPECT_EQ(typed->span_id(), "0");
  EXPECT_EQ(typed->body(), "test-body");
}

// -----------------------------------------------------------------------------
// §2 ProtobufSerializer — 解码损坏数据
// -----------------------------------------------------------------------------

// 解码非法字节应返回 false
TEST(ProtobufSerializer, DecodeCorruptedData) {
  ProtobufSerializer ser;
  auto msg_raw = MessageFactory::create<ProtoRpcRequest>();
  ASSERT_NE(msg_raw, nullptr);
  BaseMessage::ptr msg = msg_raw;

  std::string garbage = "this is not a valid protobuf payload";
  EXPECT_FALSE(ser.decode(garbage, msg));
}

// -----------------------------------------------------------------------------
// §3 多态
// -----------------------------------------------------------------------------

// 通过 shared_ptr<ISerializer> 调用 ProtobufSerializer，编解码正常
TEST(SerializerInterface, PolymorphicCall) {
  std::shared_ptr<ISerializer> ser = std::make_shared<ProtobufSerializer>();

  auto req = MessageFactory::create<ProtoRpcRequest>();
  ASSERT_NE(req, nullptr);
  req->setId("poly-1");
  req->setMsgType(MsgType::REQ_RPC_PROTO);
  req->setMethod("echo");
  req->setBody("poly-body");

  std::string wire = ser->encode(req);
  EXPECT_FALSE(wire.empty());

  auto decoded_raw = MessageFactory::create<ProtoRpcRequest>();
  ASSERT_NE(decoded_raw, nullptr);
  BaseMessage::ptr decoded = decoded_raw;
  EXPECT_TRUE(ser->decode(wire, decoded));

  auto typed = std::dynamic_pointer_cast<ProtoRpcRequest>(decoded);
  ASSERT_NE(typed, nullptr);
  EXPECT_EQ(typed->method(), "echo");
}

// name() 返回 protobuf 标识
TEST(SerializerInterface, NameReturnsIdentifier) {
  std::shared_ptr<ISerializer> ser = std::make_shared<ProtobufSerializer>();
  EXPECT_STREQ(ser->name(), "protobuf");
}
