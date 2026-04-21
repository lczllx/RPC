// =============================================================================
// test_err_reason.cc — errReason(RespCode) 文案工具
// -----------------------------------------------------------------------------
// 总测什么：
//   fields.hpp 中 errReason 对常见错误码是否返回非空字符串；未知枚举是否有回退文案。
// 不测什么：
//   业务逻辑、RPC 调用。
//
// 分块说明：
//   §1 已知 RespCode — SUCCESS / TIMEOUT / SERVICE_NOT_FOUND 等。
//   §2 未知 / 未映射枚举 — 静态_cast 的异常值。
// =============================================================================

#include <gtest/gtest.h>

#include "src/general/fields.hpp"

using lcz_rpc::RespCode;
using lcz_rpc::errReason;

// -----------------------------------------------------------------------------
// §1 已知错误码
// -----------------------------------------------------------------------------

// errReason 对已知 RespCode 返回非空描述
TEST(ErrReason, KnownCodesNonEmpty) {
  EXPECT_FALSE(errReason(RespCode::SUCCESS).empty());
  EXPECT_FALSE(errReason(RespCode::TIMEOUT).empty());
  EXPECT_FALSE(errReason(RespCode::SERVICE_NOT_FOUND).empty());
}

// -----------------------------------------------------------------------------
// §2 未知枚举
// -----------------------------------------------------------------------------

// 未映射的枚举回退到未知错误文案（与实现一致）
TEST(ErrReason, UnknownCodeFallback) {
  auto s = errReason(static_cast<RespCode>(-1));
  EXPECT_FALSE(s.empty());
}
