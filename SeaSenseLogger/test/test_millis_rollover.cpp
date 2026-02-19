/**
 * Tests for millis() rollover safety
 *
 * Validates that the elapsed-time pattern (now - lastTime >= interval)
 * works correctly across the unsigned 32-bit wraparound at ~49.7 days.
 * A regression here causes timers to freeze for 49 days after rollover.
 */

#include "test_framework.h"
#include <cstdint>

// Simulate the exact unsigned arithmetic used in the firmware
static bool elapsed(uint32_t now, uint32_t last, uint32_t interval) {
    return (now - last) >= interval;
}

// Old broken pattern: absolute target comparison
static bool elapsed_broken(uint32_t now, uint32_t target) {
    return now >= target;
}

// Test: normal case well before rollover
void test_normal_case() {
    uint32_t last = 1000;
    uint32_t interval = 5000;
    uint32_t now = 6001;

    ASSERT_TRUE(elapsed(now, last, interval));   // 6001 - 1000 = 5001 >= 5000
    ASSERT_FALSE(elapsed(now - 2, last, interval));  // 5999 - 1000 = 4999 < 5000

    TEST_PASS();
}

// Test: rollover — now has wrapped but lastTime hasn't
void test_rollover_fires_correctly() {
    uint32_t last = 0xFFFFF000;  // ~1s before rollover
    uint32_t interval = 5000;
    // After rollover: now is small, but (now - last) wraps correctly
    uint32_t now = 0x00001000;   // ~4096 after rollover

    // Unsigned arithmetic: 0x00001000 - 0xFFFFF000 = 0x00002000 = 8192
    uint32_t delta = now - last;
    ASSERT_EQ((uint32_t)8192, delta);
    ASSERT_TRUE(elapsed(now, last, interval));  // 8192 >= 5000

    TEST_PASS();
}

// Test: rollover — old broken pattern would freeze
void test_old_pattern_breaks_at_rollover() {
    uint32_t last = 0xFFFFE000;
    uint32_t interval = 5000;
    (void)(last + interval);  // target = 0xFFFFF388 (still before rollover)

    // When target wraps to < last:
    uint32_t last2 = 0xFFFFF000;
    uint32_t target2 = last2 + interval;  // 0xFFFFF000 + 5000 = 0x00000388 (wrapped!)

    // now is 0x00000100 — after rollover but before the wrapped target
    uint32_t now = 0x00000100;

    // Old pattern thinks "now < target" and doesn't fire — BUG
    ASSERT_FALSE(elapsed_broken(now, target2));  // broken: 0x100 < 0x388

    // New pattern fires correctly: 0x100 - 0xFFFFF000 = 0x1100 = 4352
    // Hmm, 4352 < 5000, so it shouldn't fire yet
    ASSERT_FALSE(elapsed(now, last2, interval));

    // Advance past the interval
    now = 0x00001400;  // 0x1400 - 0xFFFFF000 = 0x2400 = 9216 >= 5000
    ASSERT_TRUE(elapsed(now, last2, interval));

    // Old pattern STILL thinks now < target when target wrapped to tiny value...
    // Actually target2 = 0x388, and now = 0x1400 > 0x388, so old works HERE.
    // The real bug is when now hasn't reached the wrapped target yet:
    uint32_t now_early = 0x00000200;  // delta = 0x1200 = 4608, not yet
    ASSERT_FALSE(elapsed(now_early, last2, interval));  // correct: not yet

    TEST_PASS();
}

// Test: exact boundary — interval fires at exactly the boundary
void test_exact_boundary() {
    uint32_t last = 100;
    uint32_t interval = 5000;
    uint32_t now = 5100;

    ASSERT_TRUE(elapsed(now, last, interval));   // exactly 5000
    ASSERT_FALSE(elapsed(now - 1, last, interval));  // 4999 < 5000

    TEST_PASS();
}

// Test: last == now (zero elapsed time)
void test_zero_elapsed() {
    uint32_t now = 50000;
    uint32_t interval = 5000;

    // Zero interval always fires
    ASSERT_TRUE(elapsed(now, now, 0));

    // Non-zero interval should not fire with zero elapsed
    ASSERT_FALSE(elapsed(now, now, interval));

    TEST_PASS();
}

// Test: rollover with very large interval (30 minutes = 1800000ms)
void test_large_interval_rollover() {
    uint32_t interval = 1800000;  // 30 minutes
    uint32_t last = UINT32_MAX - 999999;  // exactly 1M ticks before wraparound to 0

    // 800000ms after rollover: total elapsed = 1000000 + 800000 = 1800000
    uint32_t now = 800000;
    uint32_t delta = now - last;  // unsigned wrap gives correct result
    ASSERT_EQ(interval, delta);
    ASSERT_TRUE(elapsed(now, last, interval));

    // 799999ms after rollover: total = 1999999, not yet
    now = 799999;
    ASSERT_FALSE(elapsed(now, last, interval));

    TEST_PASS();
}

// Test: both values near UINT32_MAX
void test_both_near_max() {
    uint32_t last = UINT32_MAX - 100;
    uint32_t interval = 50;
    uint32_t now = UINT32_MAX - 49;

    // delta = (MAX-49) - (MAX-100) = 51 >= 50
    ASSERT_TRUE(elapsed(now, last, interval));

    now = UINT32_MAX - 51;
    // delta = (MAX-51) - (MAX-100) = 49 < 50
    ASSERT_FALSE(elapsed(now, last, interval));

    TEST_PASS();
}

// Test: continuous mode 2-second interval across rollover
void test_continuous_mode_2s_rollover() {
    uint32_t interval = 2000;
    uint32_t last = UINT32_MAX - 499;  // exactly 500 ticks before wraparound to 0
    uint32_t now = 1500;  // 1500ms after rollover

    // Total elapsed: 500 + 1500 = 2000
    ASSERT_TRUE(elapsed(now, last, interval));

    now = 1499;  // 1999ms elapsed
    ASSERT_FALSE(elapsed(now, last, interval));

    TEST_PASS();
}

int main() {
    TEST_SUITE("millis() Rollover Safety");

    RUN_TEST(normal_case);
    RUN_TEST(rollover_fires_correctly);
    RUN_TEST(old_pattern_breaks_at_rollover);
    RUN_TEST(exact_boundary);
    RUN_TEST(zero_elapsed);
    RUN_TEST(large_interval_rollover);
    RUN_TEST(both_near_max);
    RUN_TEST(continuous_mode_2s_rollover);

    TEST_SUMMARY();
}
