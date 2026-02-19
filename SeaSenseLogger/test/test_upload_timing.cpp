/**
 * Tests for APIUploader rollover-safe timing
 *
 * Validates the elapsed-time scheduling pattern used for upload intervals,
 * retry backoff, force upload, and getTimeUntilNext().
 *
 * We extract the timing state machine rather than compiling all of
 * APIUploader.cpp, which pulls in StorageManager, HTTPClient, etc.
 */

#include <Arduino.h>
#include "test_framework.h"

// ============================================================================
// Extract of APIUploader timing logic (mirrors real implementation exactly)
// ============================================================================

// Retry backoff table (same as APIUploader.cpp)
static const unsigned long RETRY_INTERVALS[] = {
    60000, 120000, 300000, 600000, 1800000
};
static const uint8_t MAX_RETRY_INTERVALS = 5;

struct UploadTimer {
    unsigned long lastScheduledTime = 0;
    unsigned long currentIntervalMs = 0;
    uint8_t retryCount = 0;
    unsigned long configIntervalMs = 300000;  // 5 minutes default

    void begin() {
        lastScheduledTime = _mock_millis;
        currentIntervalMs = configIntervalMs;
    }

    bool shouldProcess() {
        return (_mock_millis - lastScheduledTime) >= currentIntervalMs;
    }

    void onSuccess() {
        retryCount = 0;
        lastScheduledTime = _mock_millis;
        currentIntervalMs = configIntervalMs;
    }

    void onNoData() {
        lastScheduledTime = _mock_millis;
        currentIntervalMs = configIntervalMs;
    }

    void scheduleRetry() {
        uint8_t idx = min(retryCount, (uint8_t)(MAX_RETRY_INTERVALS - 1));
        lastScheduledTime = _mock_millis;
        currentIntervalMs = RETRY_INTERVALS[idx];
        retryCount++;
    }

    void forceUpload() {
        lastScheduledTime = 0;
        currentIntervalMs = 0;
    }

    unsigned long getTimeUntilNext() const {
        unsigned long elapsed = _mock_millis - lastScheduledTime;
        if (elapsed >= currentIntervalMs) return 0;
        return currentIntervalMs - elapsed;
    }
};

// ============================================================================
// Tests
// ============================================================================

// Test: begin() schedules first upload after configIntervalMs
void test_begin_schedules_first_upload() {
    _mock_millis = 10000;
    UploadTimer t;
    t.configIntervalMs = 300000;
    t.begin();

    ASSERT_FALSE(t.shouldProcess());
    ASSERT_EQ((unsigned long)300000, t.getTimeUntilNext());

    // Advance to just before interval
    _mock_millis = 10000 + 299999;
    ASSERT_FALSE(t.shouldProcess());
    ASSERT_EQ((unsigned long)1, t.getTimeUntilNext());

    // Advance to exactly the interval
    _mock_millis = 10000 + 300000;
    ASSERT_TRUE(t.shouldProcess());
    ASSERT_EQ((unsigned long)0, t.getTimeUntilNext());

    TEST_PASS();
}

// Test: onSuccess reschedules at normal interval
void test_success_reschedules_normal() {
    _mock_millis = 500000;
    UploadTimer t;
    t.configIntervalMs = 300000;
    t.begin();

    _mock_millis = 800000;
    ASSERT_TRUE(t.shouldProcess());

    t.onSuccess();
    ASSERT_FALSE(t.shouldProcess());
    ASSERT_EQ((unsigned long)300000, t.getTimeUntilNext());

    // Should fire again after another interval
    _mock_millis = 800000 + 300000;
    ASSERT_TRUE(t.shouldProcess());

    TEST_PASS();
}

// Test: onNoData reschedules at normal interval
void test_no_data_reschedules_normal() {
    _mock_millis = 100000;
    UploadTimer t;
    t.configIntervalMs = 60000;
    t.begin();

    _mock_millis = 160000;
    ASSERT_TRUE(t.shouldProcess());

    t.onNoData();
    ASSERT_FALSE(t.shouldProcess());
    ASSERT_EQ((unsigned long)60000, t.getTimeUntilNext());

    TEST_PASS();
}

// Test: scheduleRetry uses exponential backoff
void test_retry_exponential_backoff() {
    _mock_millis = 100000;
    UploadTimer t;
    t.configIntervalMs = 300000;
    t.begin();

    _mock_millis = 500000;

    // Retry 0: 60s
    t.scheduleRetry();
    ASSERT_EQ((unsigned long)60000, t.getTimeUntilNext());
    ASSERT_EQ((uint8_t)1, t.retryCount);

    // Advance and retry 1: 120s
    _mock_millis += 60000;
    t.scheduleRetry();
    ASSERT_EQ((unsigned long)120000, t.getTimeUntilNext());
    ASSERT_EQ((uint8_t)2, t.retryCount);

    // Retry 2: 300s
    _mock_millis += 120000;
    t.scheduleRetry();
    ASSERT_EQ((unsigned long)300000, t.getTimeUntilNext());
    ASSERT_EQ((uint8_t)3, t.retryCount);

    // Retry 3: 600s
    _mock_millis += 300000;
    t.scheduleRetry();
    ASSERT_EQ((unsigned long)600000, t.getTimeUntilNext());
    ASSERT_EQ((uint8_t)4, t.retryCount);

    // Retry 4+: caps at 1800s
    _mock_millis += 600000;
    t.scheduleRetry();
    ASSERT_EQ((unsigned long)1800000, t.getTimeUntilNext());
    ASSERT_EQ((uint8_t)5, t.retryCount);

    // Further retries stay at cap
    _mock_millis += 1800000;
    t.scheduleRetry();
    ASSERT_EQ((unsigned long)1800000, t.getTimeUntilNext());

    TEST_PASS();
}

// Test: forceUpload fires immediately
void test_force_upload_fires_immediately() {
    _mock_millis = 100000;
    UploadTimer t;
    t.configIntervalMs = 300000;
    t.begin();

    ASSERT_FALSE(t.shouldProcess());

    t.forceUpload();
    ASSERT_TRUE(t.shouldProcess());
    ASSERT_EQ((unsigned long)0, t.getTimeUntilNext());

    TEST_PASS();
}

// Test: success after retry resets backoff
void test_success_resets_retry() {
    _mock_millis = 100000;
    UploadTimer t;
    t.configIntervalMs = 300000;
    t.begin();

    _mock_millis = 500000;
    t.scheduleRetry();  // retryCount = 1
    t.scheduleRetry();  // retryCount = 2
    t.scheduleRetry();  // retryCount = 3

    _mock_millis += 300000;
    t.onSuccess();
    ASSERT_EQ((uint8_t)0, t.retryCount);
    ASSERT_EQ((unsigned long)300000, t.getTimeUntilNext());

    TEST_PASS();
}

// Test: timing across millis() rollover
void test_timing_across_rollover() {
    UploadTimer t;
    t.configIntervalMs = 300000;

    // Set lastScheduledTime near UINT32_MAX
    _mock_millis = 0xFFFB0000UL;  // ~5 min before rollover
    t.begin();

    // Not yet
    _mock_millis = 0xFFFD0000UL;
    ASSERT_FALSE(t.shouldProcess());

    // After rollover, interval elapsed
    _mock_millis = 0x00030000UL;  // wrapped around
    // elapsed = 0x00030000 - 0xFFFB0000 = 0x00080000 = 524288 > 300000
    ASSERT_TRUE(t.shouldProcess());

    // Reschedule after rollover
    t.onSuccess();
    ASSERT_FALSE(t.shouldProcess());

    // Next interval also works after rollover
    _mock_millis = 0x00030000UL + 300000;
    ASSERT_TRUE(t.shouldProcess());

    TEST_PASS();
}

// Test: retry timing across rollover
void test_retry_across_rollover() {
    UploadTimer t;
    t.configIntervalMs = 300000;

    _mock_millis = 0xFFFF0000UL;  // near max
    t.begin();

    _mock_millis = 0xFFFF0000UL + 300000;  // wraps
    ASSERT_TRUE(t.shouldProcess());

    // Schedule retry (60s)
    t.scheduleRetry();
    unsigned long scheduled = _mock_millis;

    // 59s later: not yet
    _mock_millis = scheduled + 59000;
    ASSERT_FALSE(t.shouldProcess());

    // 60s later: fires
    _mock_millis = scheduled + 60000;
    ASSERT_TRUE(t.shouldProcess());

    TEST_PASS();
}

// Test: getTimeUntilNext returns 0 when past due
void test_time_until_next_past_due() {
    _mock_millis = 1000;
    UploadTimer t;
    t.configIntervalMs = 5000;
    t.begin();

    _mock_millis = 100000;  // way past due
    ASSERT_EQ((unsigned long)0, t.getTimeUntilNext());

    TEST_PASS();
}

int main() {
    TEST_SUITE("APIUploader Timing (Rollover-Safe)");

    RUN_TEST(begin_schedules_first_upload);
    RUN_TEST(success_reschedules_normal);
    RUN_TEST(no_data_reschedules_normal);
    RUN_TEST(retry_exponential_backoff);
    RUN_TEST(force_upload_fires_immediately);
    RUN_TEST(success_resets_retry);
    RUN_TEST(timing_across_rollover);
    RUN_TEST(retry_across_rollover);
    RUN_TEST(time_until_next_past_due);

    TEST_SUMMARY();
}
