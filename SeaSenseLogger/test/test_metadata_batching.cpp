/**
 * Tests for SPIFFSStorage metadata batching
 *
 * Validates that saveMetadata() is only called every METADATA_SAVE_INTERVAL
 * writes, reducing flash wear from ~4 writes/record to ~4 writes/50 records.
 * Also tests setLastUploadedMillis uses O(1) cached count.
 *
 * Uses mock SPIFFS where file operations are no-ops but succeed.
 */

#define private public  // Access private _metadataDirtyCount, _cachedRecordCount, etc.
#include "test_framework.h"
#include "../src/storage/SPIFFSStorage.h"

// Track saveMetadata calls by wrapping the behavior
static int g_saveMetadataCalls = 0;

// Helper: create a minimal test record
static DataRecord makeMinimalRecord() {
    DataRecord r;
    r.millis = 1000;
    r.timestampUTC = "";
    r.latitude = NAN;
    r.longitude = NAN;
    r.altitude = NAN;
    r.gps_satellites = 0;
    r.gps_hdop = NAN;
    r.sensorType = "Temperature";
    r.sensorModel = "EZO-RTD";
    r.sensorSerial = "001";
    r.sensorInstance = 1;
    r.calibrationDate = "";
    r.value = 22.5f;
    r.unit = "C";
    r.quality = "good";
    r.windSpeedTrue = NAN;
    r.windAngleTrue = NAN;
    r.windSpeedApparent = NAN;
    r.windAngleApparent = NAN;
    r.waterDepth = NAN;
    r.speedThroughWater = NAN;
    r.waterTempExternal = NAN;
    r.airTemp = NAN;
    r.baroPressure = NAN;
    r.humidity = NAN;
    r.cogTrue = NAN;
    r.sog = NAN;
    r.heading = NAN;
    r.pitch = NAN;
    r.roll = NAN;
    return r;
}

// Test: dirty count starts at zero
void test_dirty_count_initial_value() {
    SPIFFSStorage storage(100);
    ASSERT_EQ((uint16_t)0, storage._metadataDirtyCount);

    TEST_PASS();
}

// Test: METADATA_SAVE_INTERVAL constant is 50
void test_save_interval_is_50() {
    ASSERT_EQ((uint16_t)50, SPIFFSStorage::METADATA_SAVE_INTERVAL);

    TEST_PASS();
}

// Test: writeRecord increments dirty count
void test_write_increments_dirty_count() {
    SPIFFSStorage storage(1000);
    storage._mounted = true;

    DataRecord rec = makeMinimalRecord();
    storage.writeRecord(rec);

    // After 1 write: dirty count should be 1 (not reset yet)
    ASSERT_EQ((uint16_t)1, storage._metadataDirtyCount);
    ASSERT_EQ((uint32_t)1, storage._metadata.totalRecordsWritten);

    TEST_PASS();
}

// Test: dirty count resets after METADATA_SAVE_INTERVAL writes
void test_dirty_count_resets_at_interval() {
    SPIFFSStorage storage(2000);
    storage._mounted = true;

    DataRecord rec = makeMinimalRecord();

    // Write 49 records — dirty count should be 49
    for (int i = 0; i < 49; i++) {
        storage.writeRecord(rec);
    }
    ASSERT_EQ((uint16_t)49, storage._metadataDirtyCount);
    ASSERT_EQ((uint32_t)49, storage._metadata.totalRecordsWritten);

    // 50th write triggers saveMetadata and resets counter
    storage.writeRecord(rec);
    ASSERT_EQ((uint16_t)0, storage._metadataDirtyCount);
    ASSERT_EQ((uint32_t)50, storage._metadata.totalRecordsWritten);

    TEST_PASS();
}

// Test: dirty count cycles correctly over multiple intervals
void test_dirty_count_cycles() {
    SPIFFSStorage storage(5000);
    storage._mounted = true;

    DataRecord rec = makeMinimalRecord();

    // Write 150 records (3 full cycles of 50)
    for (int i = 0; i < 150; i++) {
        storage.writeRecord(rec);
    }
    // After 150 writes: 3 saves happened, dirty count is 0
    ASSERT_EQ((uint16_t)0, storage._metadataDirtyCount);
    ASSERT_EQ((uint32_t)150, storage._metadata.totalRecordsWritten);

    // Write 25 more — dirty count should be 25
    for (int i = 0; i < 25; i++) {
        storage.writeRecord(rec);
    }
    ASSERT_EQ((uint16_t)25, storage._metadataDirtyCount);

    TEST_PASS();
}

// Test: setLastUploadedMillis uses cached count (O(1) instead of O(n))
void test_set_last_uploaded_uses_cached_count() {
    SPIFFSStorage storage(1000);
    storage._mounted = true;
    storage._cachedRecordCount = 42;

    storage.setLastUploadedMillis(12345);

    // Should use _cachedRecordCount, not countRecords()
    ASSERT_EQ((uint32_t)42, storage._metadata.recordsAtLastUpload);
    ASSERT_EQ((unsigned long)12345, storage._metadata.lastUploadedMillis);

    TEST_PASS();
}

// Test: setLastUploadedMillis resets dirty count (forces save)
void test_set_last_uploaded_resets_dirty_count() {
    SPIFFSStorage storage(1000);
    storage._mounted = true;

    DataRecord rec = makeMinimalRecord();

    // Write 30 records (dirty count = 30)
    for (int i = 0; i < 30; i++) {
        storage.writeRecord(rec);
    }
    ASSERT_EQ((uint16_t)30, storage._metadataDirtyCount);

    // setLastUploadedMillis forces save, resets dirty count
    storage.setLastUploadedMillis(5000);
    ASSERT_EQ((uint16_t)0, storage._metadataDirtyCount);

    TEST_PASS();
}

// Test: cachedRecordCount tracks writes
void test_cached_record_count_increments() {
    SPIFFSStorage storage(1000);
    storage._mounted = true;
    storage._cachedRecordCount = 0;

    DataRecord rec = makeMinimalRecord();

    for (int i = 0; i < 10; i++) {
        storage.writeRecord(rec);
    }

    ASSERT_EQ((uint32_t)10, storage._cachedRecordCount);

    TEST_PASS();
}

int main() {
    TEST_SUITE("Metadata Batching (SPIFFSStorage)");

    RUN_TEST(dirty_count_initial_value);
    RUN_TEST(save_interval_is_50);
    RUN_TEST(write_increments_dirty_count);
    RUN_TEST(dirty_count_resets_at_interval);
    RUN_TEST(dirty_count_cycles);
    RUN_TEST(set_last_uploaded_uses_cached_count);
    RUN_TEST(set_last_uploaded_resets_dirty_count);
    RUN_TEST(cached_record_count_increments);

    TEST_SUMMARY();
}
