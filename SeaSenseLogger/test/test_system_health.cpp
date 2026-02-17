/**
 * Tests for SystemHealth boot loop detection and safe mode
 *
 * Validates that the device enters safe mode after too many consecutive
 * reboots, and that it can be cleared. A regression here means the device
 * either never enters safe mode (bricked) or can't exit it (stuck).
 */

#include "test_framework.h"
#include "../src/system/SystemHealth.h"

// Reset all mock state before each test
static void reset_mocks() {
    _mock_millis = 0;
    _mock_reset_reason = ESP_RST_POWERON;
    mock_nvs_store().clear();
}

// Test: fresh boot (no NVS history) → no safe mode
void test_fresh_boot_no_safe_mode() {
    reset_mocks();

    SystemHealth health;
    health.begin(30000, 3, 120000);  // WDT 30s, threshold 3, window 120s

    ASSERT_FALSE(health.isInSafeMode());
    ASSERT_EQ((uint32_t)1, health.getRebootCount());
    ASSERT_EQ((uint32_t)1, health.getConsecutiveReboots());

    TEST_PASS();
}

// Test: consecutive reboots reach threshold → safe mode
void test_boot_loop_triggers_safe_mode() {
    reset_mocks();

    // Simulate 2 previous boots stored in NVS
    mock_nvs_store()["reboot_cnt"] = 2;
    mock_nvs_store()["consec_boot"] = 2;

    SystemHealth health;
    health.begin(30000, 3, 120000);  // threshold = 3

    // After begin(), consecutive = 2+1 = 3, which >= threshold
    ASSERT_TRUE(health.isInSafeMode());
    ASSERT_EQ((uint32_t)3, health.getConsecutiveReboots());

    TEST_PASS();
}

// Test: just below threshold → no safe mode
void test_below_threshold_no_safe_mode() {
    reset_mocks();

    mock_nvs_store()["consec_boot"] = 1;

    SystemHealth health;
    health.begin(30000, 3, 120000);

    // consecutive = 1+1 = 2, which < 3
    ASSERT_FALSE(health.isInSafeMode());
    ASSERT_EQ((uint32_t)2, health.getConsecutiveReboots());

    TEST_PASS();
}

// Test: clearSafeMode resets state
void test_clear_safe_mode() {
    reset_mocks();

    mock_nvs_store()["consec_boot"] = 4;

    SystemHealth health;
    health.begin(30000, 3, 120000);
    ASSERT_TRUE(health.isInSafeMode());

    health.clearSafeMode();
    ASSERT_FALSE(health.isInSafeMode());
    ASSERT_EQ((uint32_t)0, health.getConsecutiveReboots());

    // NVS should also be zeroed
    ASSERT_EQ((uint32_t)0, mock_nvs_store()["consec_boot"]);

    TEST_PASS();
}

// Test: stable uptime clears consecutive counter via feedWatchdog
void test_feedwatchdog_clears_consecutive_after_window() {
    reset_mocks();

    mock_nvs_store()["consec_boot"] = 1;

    SystemHealth health;
    health.begin(30000, 5, 120000);  // window = 120s

    // Simulate time passing beyond window
    _mock_millis = 121000;
    health.feedWatchdog();

    // NVS consecutive counter should be cleared
    ASSERT_EQ((uint32_t)0, mock_nvs_store()["consec_boot"]);

    TEST_PASS();
}

// Test: feedWatchdog does NOT clear counter before window expires
void test_feedwatchdog_no_clear_before_window() {
    reset_mocks();

    mock_nvs_store()["consec_boot"] = 1;

    SystemHealth health;
    health.begin(30000, 5, 120000);

    // Time is still within window
    _mock_millis = 60000;
    health.feedWatchdog();

    // consecutive should still be 2 (1 from NVS + 1 from begin)
    ASSERT_EQ((uint32_t)2, mock_nvs_store()["consec_boot"]);

    TEST_PASS();
}

// Test: error counters increment and persist
void test_error_counters() {
    reset_mocks();

    SystemHealth health;
    health.begin(30000, 5, 120000);

    ASSERT_EQ((uint32_t)0, health.getErrorCount(ErrorType::SENSOR));
    ASSERT_EQ((uint32_t)0, health.getErrorCount(ErrorType::SD));

    health.recordError(ErrorType::SENSOR);
    health.recordError(ErrorType::SENSOR);
    health.recordError(ErrorType::SD);

    ASSERT_EQ((uint32_t)2, health.getErrorCount(ErrorType::SENSOR));
    ASSERT_EQ((uint32_t)1, health.getErrorCount(ErrorType::SD));
    ASSERT_EQ((uint32_t)0, health.getErrorCount(ErrorType::API));

    // Verify persisted to NVS
    ASSERT_EQ((uint32_t)2, mock_nvs_store()["sensor_err"]);
    ASSERT_EQ((uint32_t)1, mock_nvs_store()["sd_err"]);

    TEST_PASS();
}

// Test: reset reason string
void test_reset_reason_string() {
    reset_mocks();
    _mock_reset_reason = ESP_RST_BROWNOUT;

    SystemHealth health;
    health.begin(30000, 5, 120000);

    ASSERT_STR_EQ("Brownout", health.getResetReasonString());

    TEST_PASS();
}

int main() {
    TEST_SUITE("SystemHealth");

    RUN_TEST(fresh_boot_no_safe_mode);
    RUN_TEST(boot_loop_triggers_safe_mode);
    RUN_TEST(below_threshold_no_safe_mode);
    RUN_TEST(clear_safe_mode);
    RUN_TEST(feedwatchdog_clears_consecutive_after_window);
    RUN_TEST(feedwatchdog_no_clear_before_window);
    RUN_TEST(error_counters);
    RUN_TEST(reset_reason_string);

    TEST_SUMMARY();
}
