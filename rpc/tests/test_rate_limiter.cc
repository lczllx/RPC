// =============================================================================
// test_rate_limiter.cc — TokenBucket 令牌桶限流器单测
// -----------------------------------------------------------------------------
// 总测什么：
//   令牌充足时 allow() 返回 true、快速连续调用触发限流、
//   令牌随时间补充恢复、burst 上限约束、retryAfterMs 为正数。
// 不测什么：
//   多线程竞态（单线程逻辑正确即可，_mutex 为标准库担保）。
//
// 分块说明：
//   §1 基础 — 单次 allow()、令牌耗尽后拒绝
//   §2 恢复 — sleep 后令牌恢复
//   §3 burst — 桶容量上限
//   §4 辅助 — retryAfterMs 返回值
// =============================================================================

#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include "src/general/rate_limiter.hpp"

using lcz_rpc::TokenBucket;

// -----------------------------------------------------------------------------
// §1 基础
// -----------------------------------------------------------------------------

// 新桶令牌充足，allow() 返回 true
TEST(TokenBucket, AllowWhenTokensAvailable) {
  TokenBucket bucket(1000.0, 2000.0);  // 1000 req/s, burst 2000
  EXPECT_TRUE(bucket.allow());
}

// 耗尽 burst 后拒绝：burst=1 的桶，第 2 次调用应被拒绝
TEST(TokenBucket, DenyWhenExhausted) {
  TokenBucket bucket(1000.0, 1.0);     // burst=1
  EXPECT_TRUE(bucket.allow());          // 消耗唯一令牌
  EXPECT_FALSE(bucket.allow());         // 桶空
}

// -----------------------------------------------------------------------------
// §2 恢复
// -----------------------------------------------------------------------------

// 桶空后在 sleep 足够时间后令牌恢复
TEST(TokenBucket, RefillAfterSleep) {
  TokenBucket bucket(500.0, 1.0);      // 500 req/s, burst=1
  EXPECT_TRUE(bucket.allow());          // 耗尽
  EXPECT_FALSE(bucket.allow());         // 确认空

  // 500 req/s = 每令牌 2ms，sleep 5ms 应恢复至少 2 个令牌
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  EXPECT_TRUE(bucket.allow());
}

// -----------------------------------------------------------------------------
// §3 burst
// -----------------------------------------------------------------------------

// burst 上限：初始 burst 个令牌，第 burst+1 次被拒
TEST(TokenBucket, BurstLimit) {
  const double burst = 3.0;
  TokenBucket bucket(1000.0, burst);

  for (int i = 0; i < static_cast<int>(burst); ++i) {
    EXPECT_TRUE(bucket.allow()) << "第 " << (i + 1) << " 次应有令牌";
  }
  EXPECT_FALSE(bucket.allow());         // 超过 burst
}

// steady 限流：1000 req/s 桶运行 5ms，应消耗 ≤8 个令牌（上限约 5ms / 1ms + burst）
TEST(TokenBucket, SteadyRateApproximation) {
  TokenBucket bucket(1000.0, 2.0);
  int allowed = 0;
  auto start = std::chrono::steady_clock::now();

  while (std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - start).count() < 5) {
    if (bucket.allow()) ++allowed;
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  // 5ms 内一共 2(burst) + 5(按速率补充) ≈ 7，留余地
  EXPECT_GE(allowed, 2);
  EXPECT_LE(allowed, 10);
}

// -----------------------------------------------------------------------------
// §4 辅助
// -----------------------------------------------------------------------------

// retryAfterMs 返回正数：速率 1000/s 等待 1ms 即可
TEST(TokenBucket, RetryAfterMsPositive) {
  TokenBucket bucket(1000.0, 1.0);
  EXPECT_GT(bucket.retryAfterMs(), 0);
}
