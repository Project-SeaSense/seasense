/**
 * Tests for ConfigManager::clampConfig()
 *
 * Validates that config values are clamped to safe operating bounds.
 * A regression here could brick the device (e.g., sensorIntervalMs=0 → CPU spin).
 */

#define private public  // Access private clampConfig()
#include "test_framework.h"
#include "../src/config/ConfigManager.h"

// Test: zero/underflow values get clamped to minimums
void test_zero_values_clamped_to_minimum() {
    ConfigManager cm;

    // Set all values to zero (the dangerous case)
    ConfigManager::SamplingConfig sampling = {0};
    cm.setSamplingConfig(sampling);

    ConfigManager::APIConfig api;
    api.url = "";
    api.apiKey = "";
    api.uploadInterval = 0;
    api.batchSize = 0;
    api.maxRetries = 0;
    cm.setAPIConfig(api);

    PumpConfig pump = {};
    pump.cycleIntervalMs = 0;
    pump.maxPumpOnTimeMs = 0;
    cm.setPumpConfig(pump);

    // Verify clamped to minimums
    ASSERT_EQ((uint32_t)5000, cm._sampling.sensorIntervalMs);
    ASSERT_EQ((uint32_t)60000, cm._api.uploadInterval);
    ASSERT_EQ((uint8_t)1, cm._api.batchSize);
    ASSERT_EQ((uint8_t)1, cm._api.maxRetries);
    ASSERT_EQ((uint32_t)10000, (uint32_t)cm._pump.cycleIntervalMs);
    ASSERT_EQ((uint32_t)5000, (uint32_t)cm._pump.maxPumpOnTimeMs);

    TEST_PASS();
}

// Test: overflow values get clamped to maximums
void test_overflow_values_clamped_to_maximum() {
    ConfigManager cm;

    ConfigManager::SamplingConfig sampling;
    sampling.sensorIntervalMs = 999999999;
    cm.setSamplingConfig(sampling);

    ConfigManager::APIConfig api;
    api.url = "";
    api.apiKey = "";
    api.uploadInterval = 999999999;
    api.batchSize = 255;  // max uint8_t
    api.maxRetries = 255;
    cm.setAPIConfig(api);

    PumpConfig pump = {};
    pump.cycleIntervalMs = 99999999;
    pump.maxPumpOnTimeMs = 65535;  // uint16_t max — within clamp range so unchanged
    cm.setPumpConfig(pump);

    // Verify clamped to maximums
    ASSERT_EQ((uint32_t)86400000, cm._sampling.sensorIntervalMs);
    ASSERT_EQ((uint32_t)86400000, cm._api.uploadInterval);
    ASSERT_EQ((uint8_t)255, cm._api.batchSize);  // max allowed
    ASSERT_EQ((uint8_t)20, cm._api.maxRetries);
    ASSERT_EQ((uint32_t)3600000, (uint32_t)cm._pump.cycleIntervalMs);
    // maxPumpOnTimeMs is uint16_t (max 65535) — clamp upper bound is 120000
    // so 65535 is within [5000, 120000] and passes through unchanged
    ASSERT_EQ((uint32_t)65535, (uint32_t)cm._pump.maxPumpOnTimeMs);

    TEST_PASS();
}

// Test: valid mid-range values pass through unchanged
void test_valid_values_unchanged() {
    ConfigManager cm;

    ConfigManager::SamplingConfig sampling;
    sampling.sensorIntervalMs = 900000;  // 15 minutes
    cm.setSamplingConfig(sampling);

    ConfigManager::APIConfig api;
    api.url = "https://api.example.com";
    api.apiKey = "key123";
    api.uploadInterval = 300000;  // 5 minutes
    api.batchSize = 100;
    api.maxRetries = 5;
    cm.setAPIConfig(api);

    PumpConfig pump = {};
    pump.cycleIntervalMs = 60000;   // 1 minute
    pump.maxPumpOnTimeMs = 30000;   // 30 seconds
    cm.setPumpConfig(pump);

    // All should be unchanged
    ASSERT_EQ((uint32_t)900000, cm._sampling.sensorIntervalMs);
    ASSERT_EQ((uint32_t)300000, cm._api.uploadInterval);
    ASSERT_EQ((uint8_t)100, cm._api.batchSize);
    ASSERT_EQ((uint8_t)5, cm._api.maxRetries);
    ASSERT_EQ((uint32_t)60000, (uint32_t)cm._pump.cycleIntervalMs);
    ASSERT_EQ((uint32_t)30000, (uint32_t)cm._pump.maxPumpOnTimeMs);

    TEST_PASS();
}

// Test: exact boundary values are accepted
void test_boundary_values_accepted() {
    ConfigManager cm;

    ConfigManager::SamplingConfig sampling;
    sampling.sensorIntervalMs = 5000;  // exact minimum
    cm.setSamplingConfig(sampling);
    ASSERT_EQ((uint32_t)5000, cm._sampling.sensorIntervalMs);

    sampling.sensorIntervalMs = 86400000;  // exact maximum
    cm.setSamplingConfig(sampling);
    ASSERT_EQ((uint32_t)86400000, cm._sampling.sensorIntervalMs);

    TEST_PASS();
}

// Test: clampConfig is called through setAPIConfig
void test_setter_triggers_clamp() {
    ConfigManager cm;

    ConfigManager::APIConfig api;
    api.url = "";
    api.apiKey = "";
    api.uploadInterval = 10;  // way below minimum of 60000
    api.batchSize = 50;
    api.maxRetries = 3;
    cm.setAPIConfig(api);

    // Should be clamped even though we set it via setter
    ASSERT_EQ((uint32_t)60000, cm._api.uploadInterval);

    TEST_PASS();
}

int main() {
    TEST_SUITE("ConfigManager::clampConfig");

    RUN_TEST(zero_values_clamped_to_minimum);
    RUN_TEST(overflow_values_clamped_to_maximum);
    RUN_TEST(valid_values_unchanged);
    RUN_TEST(boundary_values_accepted);
    RUN_TEST(setter_triggers_clamp);

    TEST_SUMMARY();
}
