/**
 * Tests for SPIFFSStorage CSV serialization/parsing
 *
 * Validates that recordToCSV → parseCSVLine round-trips correctly.
 * A regression here means data corruption on the SD card or SPIFFS.
 */

#define private public  // Access private parseCSVLine/recordToCSV
#include "test_framework.h"
#include "../src/system/SystemHealth.h"
#include "../src/storage/SPIFFSStorage.h"

// Global SystemHealth instance (referenced by SPIFFSStorage via extern)
SystemHealth systemHealth;

// Helper: create a fully populated test record
static DataRecord makeTestRecord() {
    DataRecord r;
    r.millis = 1234567;
    r.timestampUTC = "2025-06-15T12:30:00Z";
    r.latitude = 52.123456;
    r.longitude = 4.654321;
    r.altitude = 1.5;
    r.gps_satellites = 8;
    r.gps_hdop = 1.2;
    r.sensorType = "Temperature";
    r.sensorModel = "EZO-RTD";
    r.sensorSerial = "RTD-001";
    r.sensorInstance = 1;
    r.calibrationDate = "2025-06-01";
    r.value = 22.45f;
    r.unit = "C";
    r.quality = "good";
    // NMEA2000 environmental fields
    r.windSpeedTrue = 5.2f;
    r.windAngleTrue = 180.0f;
    r.windSpeedApparent = 6.1f;
    r.windAngleApparent = 170.5f;
    r.waterDepth = 3.5f;
    r.speedThroughWater = 2.1f;
    r.waterTempExternal = 18.3f;
    r.airTemp = 21.0f;
    r.baroPressure = 101325.0f;
    r.humidity = 65.5f;
    r.cogTrue = 270.0f;
    r.sog = 3.5f;
    r.heading = 268.0f;
    r.pitch = 1.2f;
    r.roll = -0.5f;
    r.windSpeedCorrected = 5.8f;
    r.windAngleCorrected = 168.3f;
    r.linAccelX = 0.12f;
    r.linAccelY = -0.05f;
    r.linAccelZ = 0.03f;
    return r;
}

// Test: full 30-field round-trip preserves all values
void test_full_roundtrip() {
    SPIFFSStorage storage(100);
    DataRecord original = makeTestRecord();

    String csv = storage.recordToCSV(original);
    DataRecord parsed;
    bool ok = storage.parseCSVLine(csv, parsed);

    ASSERT_TRUE(ok);
    ASSERT_EQ(original.millis, parsed.millis);
    ASSERT_STR_EQ(original.timestampUTC, parsed.timestampUTC);
    ASSERT_FLOAT_EQ(original.latitude, parsed.latitude, 0.0001);
    ASSERT_FLOAT_EQ(original.longitude, parsed.longitude, 0.0001);
    ASSERT_STR_EQ(original.sensorType, parsed.sensorType);
    ASSERT_STR_EQ(original.sensorModel, parsed.sensorModel);
    ASSERT_STR_EQ(original.sensorSerial, parsed.sensorSerial);
    ASSERT_EQ(original.sensorInstance, parsed.sensorInstance);
    ASSERT_FLOAT_EQ(original.value, parsed.value, 0.01);
    ASSERT_STR_EQ(original.unit, parsed.unit);
    ASSERT_STR_EQ(original.quality, parsed.quality);

    // Environmental fields
    ASSERT_FLOAT_EQ(original.windSpeedTrue, parsed.windSpeedTrue, 0.01);
    ASSERT_FLOAT_EQ(original.windAngleTrue, parsed.windAngleTrue, 0.1);
    ASSERT_FLOAT_EQ(original.waterDepth, parsed.waterDepth, 0.01);
    ASSERT_FLOAT_EQ(original.airTemp, parsed.airTemp, 0.01);
    ASSERT_FLOAT_EQ(original.baroPressure, parsed.baroPressure, 1.0);
    ASSERT_FLOAT_EQ(original.heading, parsed.heading, 0.1);
    ASSERT_FLOAT_EQ(original.windSpeedCorrected, parsed.windSpeedCorrected, 0.01);
    ASSERT_FLOAT_EQ(original.windAngleCorrected, parsed.windAngleCorrected, 0.1);
    ASSERT_FLOAT_EQ(original.linAccelX, parsed.linAccelX, 0.001);
    ASSERT_FLOAT_EQ(original.linAccelY, parsed.linAccelY, 0.001);
    ASSERT_FLOAT_EQ(original.linAccelZ, parsed.linAccelZ, 0.001);

    TEST_PASS();
}

// Test: NaN environmental fields survive round-trip
void test_nan_fields_roundtrip() {
    SPIFFSStorage storage(100);
    DataRecord original = makeTestRecord();

    // Set all environmental fields to NaN
    original.windSpeedTrue = NAN;
    original.windAngleTrue = NAN;
    original.windSpeedApparent = NAN;
    original.windAngleApparent = NAN;
    original.waterDepth = NAN;
    original.speedThroughWater = NAN;
    original.waterTempExternal = NAN;
    original.airTemp = NAN;
    original.baroPressure = NAN;
    original.humidity = NAN;
    original.cogTrue = NAN;
    original.sog = NAN;
    original.heading = NAN;
    original.pitch = NAN;
    original.roll = NAN;
    original.windSpeedCorrected = NAN;
    original.windAngleCorrected = NAN;
    original.linAccelX = NAN;
    original.linAccelY = NAN;
    original.linAccelZ = NAN;

    String csv = storage.recordToCSV(original);
    DataRecord parsed;
    bool ok = storage.parseCSVLine(csv, parsed);

    ASSERT_TRUE(ok);
    ASSERT_NAN(parsed.windSpeedTrue);
    ASSERT_NAN(parsed.waterDepth);
    ASSERT_NAN(parsed.baroPressure);
    ASSERT_NAN(parsed.heading);
    ASSERT_NAN(parsed.roll);
    ASSERT_NAN(parsed.windSpeedCorrected);
    ASSERT_NAN(parsed.windAngleCorrected);
    ASSERT_NAN(parsed.linAccelX);
    ASSERT_NAN(parsed.linAccelY);
    ASSERT_NAN(parsed.linAccelZ);

    TEST_PASS();
}

// Test: old 15-field CSV format (no environmental data) still parses
void test_old_format_backward_compat() {
    SPIFFSStorage storage(100);

    // Old CSV: only 15 fields, no environmental data
    String csv = "1000000,2025-01-01T00:00:00Z,51.5,-3.2,10.0,6,1.5,"
                 "Conductivity,EZO-EC,EC-001,2,2024-12-01,45000.00,uS/cm,good";

    DataRecord parsed;
    bool ok = storage.parseCSVLine(csv, parsed);

    ASSERT_TRUE(ok);
    ASSERT_EQ((unsigned long)1000000, parsed.millis);
    ASSERT_STR_EQ("Conductivity", parsed.sensorType);
    ASSERT_FLOAT_EQ(45000.0f, parsed.value, 1.0);
    ASSERT_STR_EQ("uS/cm", parsed.unit);

    // Environmental fields should default to NaN
    ASSERT_NAN(parsed.windSpeedTrue);
    ASSERT_NAN(parsed.waterDepth);
    ASSERT_NAN(parsed.baroPressure);
    ASSERT_NAN(parsed.windSpeedCorrected);
    ASSERT_NAN(parsed.windAngleCorrected);
    ASSERT_NAN(parsed.linAccelX);

    TEST_PASS();
}

// Test: zero GPS coordinates parse correctly (no fix case)
void test_zero_gps_no_fix() {
    SPIFFSStorage storage(100);
    DataRecord original = makeTestRecord();
    original.latitude = 0.0;
    original.longitude = 0.0;
    original.altitude = 0.0;
    original.gps_satellites = 0;
    original.gps_hdop = 0.0;

    String csv = storage.recordToCSV(original);
    DataRecord parsed;
    bool ok = storage.parseCSVLine(csv, parsed);

    ASSERT_TRUE(ok);
    ASSERT_FLOAT_EQ(0.0, parsed.latitude, 0.0001);
    ASSERT_FLOAT_EQ(0.0, parsed.longitude, 0.0001);
    ASSERT_EQ((uint8_t)0, parsed.gps_satellites);

    TEST_PASS();
}

// Test: minimum field count (10) is accepted, less than 10 rejected
void test_minimum_field_count() {
    SPIFFSStorage storage(100);
    DataRecord parsed;

    // 10 fields — should succeed
    String csv10 = "100,,0,0,0,0,0,Temperature,EZO-RTD,RTD-001";
    ASSERT_TRUE(storage.parseCSVLine(csv10, parsed));

    // 9 fields — should fail
    String csv9 = "100,,0,0,0,0,0,Temperature,EZO-RTD";
    ASSERT_FALSE(storage.parseCSVLine(csv9, parsed));

    TEST_PASS();
}

int main() {
    TEST_SUITE("CSV Round-Trip (SPIFFSStorage)");

    RUN_TEST(full_roundtrip);
    RUN_TEST(nan_fields_roundtrip);
    RUN_TEST(old_format_backward_compat);
    RUN_TEST(zero_gps_no_fix);
    RUN_TEST(minimum_field_count);

    TEST_SUMMARY();
}
