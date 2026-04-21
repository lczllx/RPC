#pragma once

// =============================================================================
// string_buffer.hpp — 单元测试用「假缓冲区」
// -----------------------------------------------------------------------------
// 作用：在不启动 muduo、不建 TCP 的前提下，为 LVProtocol::canProcessed / onMessage
// 提供与 muduo::net::Buffer 在 int32 读写语义上一致的 BaseBuffer 实现。
// 被 test_lv_protocol.cc、test_lv_more_messages.cc 等引用。
// =============================================================================

#include <arpa/inet.h>
#include <cstring>
#include <string>

#include "src/general/abstract.hpp"

namespace lcz_rpc {
namespace test {

// 测试用内存缓冲区：行为与 muduo::net::Buffer 在 LV 解析路径上兼容（peek/read int32 为网络序转主机序）。
class StringBuffer : public BaseBuffer {
 public:
  explicit StringBuffer(std::string data) : data_(std::move(data)), read_idx_(0) {}

  size_t readableSize() override { return data_.size() - read_idx_; }

  int32_t peekInt32() override {
    if (readableSize() < 4) return 0;
    uint32_t be = 0;
    std::memcpy(&be, data_.data() + read_idx_, 4);
    return static_cast<int32_t>(ntohl(be));
  }

  void retrieveInt32() override {
    if (readableSize() >= 4) read_idx_ += 4;
  }

  int32_t readInt32() override {
    int32_t v = peekInt32();
    retrieveInt32();
    return v;
  }

  std::string retrieveAsString(size_t len) override {
    if (readableSize() < len) return {};
    std::string out(data_, read_idx_, len);
    read_idx_ += len;
    return out;
  }

 private:
  std::string data_;
  size_t read_idx_;
};

}  // namespace test
}  // namespace lcz_rpc
