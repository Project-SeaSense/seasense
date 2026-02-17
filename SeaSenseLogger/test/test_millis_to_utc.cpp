/**
 * Tests for APIUploader::millisToUTC()
 *
 * Validates timestamp conversion from millis() to ISO 8601 UTC.
 * A regression here means incorrect timestamps in API uploads.
 *
 * We test the logic directly rather than compiling all of APIUploader.cpp,
 * since that pulls in StorageManager, HTTPClient, etc.
 */

#include <Arduino.h>
#include "test_framework.h"
#include <ctime>

// ============================================================================
// Extract of millisToUTC logic (mirrors APIUploader::millisToUTC exactly)
// ============================================================================

struct UTCConverter {
    bool timeSynced = false;
    time_t bootTimeEpoch = 0;

    String millisToUTC(unsigned long millisTimestamp) const {
        if (!timeSynced) {
            return "";
        }

        time_t epoch = bootTimeEpoch + (millisTimestamp / 1000);

        struct tm timeinfo;
        gmtime_r(&epoch, &timeinfo);

        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

        return String(buffer);
    }
};

// ============================================================================
// Tests
// ============================================================================

// Test: known epoch + known millis → correct ISO 8601
void test_known_timestamp_conversion() {
    UTCConverter conv;
    conv.timeSynced = true;
    conv.bootTimeEpoch = 1718450000;  // 2024-06-15T11:13:20Z

    // millis = 60000 → 60 seconds after boot → 1718450060 → 2024-06-15T11:14:20Z
    String result = conv.millisToUTC(60000);
    ASSERT_STR_EQ("2024-06-15T11:14:20Z", result);

    TEST_PASS();
}

// Test: millis = 0 → boot time exactly
void test_millis_zero_is_boot_time() {
    UTCConverter conv;
    conv.timeSynced = true;
    conv.bootTimeEpoch = 1735689600;  // 2025-01-01T00:00:00Z

    String result = conv.millisToUTC(0);
    ASSERT_STR_EQ("2025-01-01T00:00:00Z", result);

    TEST_PASS();
}

// Test: unsynced → empty string
void test_unsynced_returns_empty() {
    UTCConverter conv;
    conv.timeSynced = false;
    conv.bootTimeEpoch = 0;

    String result = conv.millisToUTC(60000);
    ASSERT_STR_EQ("", result);

    TEST_PASS();
}

// Test: large millis (hours of uptime) → still correct
void test_large_millis_offset() {
    UTCConverter conv;
    conv.timeSynced = true;
    conv.bootTimeEpoch = 1748736000;  // 2025-06-01T00:00:00Z

    // 12 hours = 43200000 millis → 2025-06-01T12:00:00Z
    String result = conv.millisToUTC(43200000);
    ASSERT_STR_EQ("2025-06-01T12:00:00Z", result);

    TEST_PASS();
}

// Test: millis precision — sub-second millis are truncated correctly
void test_subsecond_truncation() {
    UTCConverter conv;
    conv.timeSynced = true;
    conv.bootTimeEpoch = 1735689600;  // 2025-01-01T00:00:00Z

    // 1500ms → 1 second (integer division)
    String result = conv.millisToUTC(1500);
    ASSERT_STR_EQ("2025-01-01T00:00:01Z", result);

    // 999ms → 0 seconds
    result = conv.millisToUTC(999);
    ASSERT_STR_EQ("2025-01-01T00:00:00Z", result);

    TEST_PASS();
}

int main() {
    TEST_SUITE("millisToUTC");

    RUN_TEST(known_timestamp_conversion);
    RUN_TEST(millis_zero_is_boot_time);
    RUN_TEST(unsynced_returns_empty);
    RUN_TEST(large_millis_offset);
    RUN_TEST(subsecond_truncation);

    TEST_SUMMARY();
}
