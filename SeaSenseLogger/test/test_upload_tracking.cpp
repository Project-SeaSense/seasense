/**
 * Tests for upload progress tracking across reboots
 *
 * Validates that recordsAtLastUpload is correctly maintained:
 * - Persists the upload marker when setLastUploadedMillis is called
 * - recordsSinceUpload correctly reflects pending records
 * - Circular buffer trim adjusts the upload marker
 * - After simulated reboot, pending count is correct
 *
 * Uses mock SPIFFS where file operations are no-ops but succeed.
 */

#define private public  // Access private members
#include "test_framework.h"
#include "../src/system/SystemHealth.h"
#include "../src/storage/SPIFFSStorage.h"

// Global SystemHealth instance (referenced by SPIFFSStorage via extern)
SystemHealth systemHealth;

// Helper: create a minimal test record
static DataRecord makeRecord(unsigned long ms) {
    DataRecord r;
    r.millis = ms;
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

// Test: fresh device has all records pending
void test_fresh_device_all_pending() {
    SPIFFSStorage storage(1000);
    storage._mounted = true;
    storage._cachedRecordCount = 0;
    storage._metadata.recordsAtLastUpload = 0;

    // Write 10 records
    for (int i = 0; i < 10; i++) {
        storage.writeRecord(makeRecord(i * 1000));
    }

    StorageStats stats = storage.getStats();
    ASSERT_EQ((uint32_t)10, stats.totalRecords);
    ASSERT_EQ((uint32_t)10, stats.recordsSinceUpload);

    TEST_PASS();
}

// Test: after upload, pending count drops to zero
void test_upload_clears_pending() {
    SPIFFSStorage storage(1000);
    storage._mounted = true;
    storage._cachedRecordCount = 0;

    // Write 10 records
    for (int i = 0; i < 10; i++) {
        storage.writeRecord(makeRecord(i * 1000));
    }

    // Mark all as uploaded
    storage.setLastUploadedMillis(9000);

    StorageStats stats = storage.getStats();
    ASSERT_EQ((uint32_t)10, stats.totalRecords);
    ASSERT_EQ((uint32_t)0, stats.recordsSinceUpload);

    TEST_PASS();
}

// Test: new records after upload are pending
void test_new_records_after_upload_pending() {
    SPIFFSStorage storage(1000);
    storage._mounted = true;
    storage._cachedRecordCount = 0;

    // Write 10, upload all
    for (int i = 0; i < 10; i++) {
        storage.writeRecord(makeRecord(i * 1000));
    }
    storage.setLastUploadedMillis(9000);

    // Write 5 more
    for (int i = 0; i < 5; i++) {
        storage.writeRecord(makeRecord(10000 + i * 1000));
    }

    StorageStats stats = storage.getStats();
    ASSERT_EQ((uint32_t)15, stats.totalRecords);
    ASSERT_EQ((uint32_t)5, stats.recordsSinceUpload);

    TEST_PASS();
}

// Test: simulated reboot preserves upload marker
void test_reboot_preserves_marker() {
    // Simulate first boot: write records, upload, save metadata
    SPIFFSStorage storage1(1000);
    storage1._mounted = true;
    storage1._cachedRecordCount = 0;

    for (int i = 0; i < 20; i++) {
        storage1.writeRecord(makeRecord(i * 1000));
    }
    storage1.setLastUploadedMillis(19000);

    // Capture the persisted state
    uint32_t savedRecordsAtUpload = storage1._metadata.recordsAtLastUpload;
    ASSERT_EQ((uint32_t)20, savedRecordsAtUpload);

    // Simulate reboot: new storage instance, restore metadata
    SPIFFSStorage storage2(1000);
    storage2._mounted = true;
    storage2._cachedRecordCount = 20;  // File still has 20 records
    storage2._metadata.recordsAtLastUpload = savedRecordsAtUpload;

    // No new records since reboot — nothing pending
    StorageStats stats = storage2.getStats();
    ASSERT_EQ((uint32_t)0, stats.recordsSinceUpload);

    // Write 3 new records after reboot
    for (int i = 0; i < 3; i++) {
        storage2.writeRecord(makeRecord(100000 + i * 1000));
    }

    stats = storage2.getStats();
    ASSERT_EQ((uint32_t)23, stats.totalRecords);
    ASSERT_EQ((uint32_t)3, stats.recordsSinceUpload);

    TEST_PASS();
}

// Test: circular buffer trim adjusts upload marker
// Uses direct trimOldRecords() call to isolate marker logic from hysteresis.
void test_trim_adjusts_upload_marker() {
    SPIFFSStorage storage(100);  // max 100 records
    storage._mounted = true;

    // Simulate state: 120 records in buffer, all 100 original were uploaded
    storage._cachedRecordCount = 120;
    storage._metadata.recordsAtLastUpload = 100;

    // Before trim: 20 records pending
    ASSERT_EQ((uint32_t)20, storage.getStats().recordsSinceUpload);

    // Trim removes 20 oldest (120 - 100 = 20)
    storage.trimOldRecords();

    // After trim: 100 records in buffer, marker adjusted from 100 to 80
    ASSERT_EQ((uint32_t)100, storage._cachedRecordCount);
    ASSERT_EQ((uint32_t)80, storage._metadata.recordsAtLastUpload);

    StorageStats stats = storage.getStats();
    ASSERT_EQ((uint32_t)20, stats.recordsSinceUpload);

    TEST_PASS();
}

// Test: trim removes more than uploaded — marker goes to zero
// Uses direct trimOldRecords() call to isolate marker logic from hysteresis.
void test_trim_past_upload_marker() {
    SPIFFSStorage storage(50);  // max 50 records
    storage._mounted = true;

    // Simulate state: 80 records in buffer, only 10 were uploaded
    storage._cachedRecordCount = 80;
    storage._metadata.recordsAtLastUpload = 10;

    // Trim removes 30 oldest (80 - 50 = 30), marker was 10 → clamped to 0
    storage.trimOldRecords();

    ASSERT_EQ((uint32_t)0, storage._metadata.recordsAtLastUpload);
    ASSERT_EQ((uint32_t)50, storage._cachedRecordCount);

    // All 50 records in buffer are "pending"
    StorageStats stats = storage.getStats();
    ASSERT_EQ((uint32_t)50, stats.recordsSinceUpload);

    TEST_PASS();
}

// Test: totalRecords <= recordsAtLastUpload returns 0 pending (not totalRecords)
void test_no_false_pending_after_upload() {
    SPIFFSStorage storage(1000);
    storage._mounted = true;
    storage._cachedRecordCount = 50;
    storage._metadata.recordsAtLastUpload = 50;

    StorageStats stats = storage.getStats();
    ASSERT_EQ((uint32_t)0, stats.recordsSinceUpload);

    TEST_PASS();
}

// Test: addBytesUploaded accumulates and persists across simulated reboot
void test_total_bytes_uploaded() {
    SPIFFSStorage storage(1000);
    storage._mounted = true;

    // Fresh device starts at 0
    ASSERT_EQ((uint64_t)0, storage.getTotalBytesUploaded());

    // Simulate three uploads
    storage.addBytesUploaded(1500);
    storage.addBytesUploaded(2300);
    storage.addBytesUploaded(800);
    ASSERT_EQ((uint64_t)4600, storage.getTotalBytesUploaded());

    // Simulate reboot: new instance, restore metadata
    uint64_t saved = storage._metadata.totalBytesUploaded;
    SPIFFSStorage storage2(1000);
    storage2._mounted = true;
    storage2._metadata.totalBytesUploaded = saved;
    ASSERT_EQ((uint64_t)4600, storage2.getTotalBytesUploaded());

    // More uploads after reboot
    storage2.addBytesUploaded(1000);
    ASSERT_EQ((uint64_t)5600, storage2.getTotalBytesUploaded());

    TEST_PASS();
}

// Test: readRecords with skipRecords compiles and integrates with upload tracking
void test_readRecords_skipRecords_param() {
    SPIFFSStorage storage(1000);
    storage._mounted = true;
    storage._cachedRecordCount = 0;

    // Write 20 records
    for (int i = 0; i < 20; i++) {
        storage.writeRecord(makeRecord(i * 1000));
    }

    // Mark all 20 as uploaded
    storage.setLastUploadedMillis(19000);
    ASSERT_EQ((uint32_t)0, storage.getStats().recordsSinceUpload);

    // Write 5 more (these are pending)
    for (int i = 0; i < 5; i++) {
        storage.writeRecord(makeRecord(20000 + i * 1000));
    }

    StorageStats stats = storage.getStats();
    ASSERT_EQ((uint32_t)25, stats.totalRecords);
    ASSERT_EQ((uint32_t)5, stats.recordsSinceUpload);

    // Compute alreadyUploaded the same way APIUploader does
    uint32_t alreadyUploaded = stats.totalRecords - stats.recordsSinceUpload;
    ASSERT_EQ((uint32_t)20, alreadyUploaded);

    // readRecords with skip (mock SPIFFS returns empty, but call must compile)
    std::vector<DataRecord> records = storage.readRecords(0, 50, alreadyUploaded);
    // Mock file is empty, so 0 records returned — that's expected
    ASSERT_EQ((size_t)0, records.size());

    TEST_PASS();
}

// Test: readRecords without skip still works (default param = 0)
void test_readRecords_default_skipRecords() {
    SPIFFSStorage storage(1000);
    storage._mounted = true;
    storage._cachedRecordCount = 0;

    // readRecords with default skipRecords (backward compatible)
    std::vector<DataRecord> records = storage.readRecords(0, 100);
    ASSERT_EQ((size_t)0, records.size());

    TEST_PASS();
}

int main() {
    TEST_SUITE("Upload Tracking (SPIFFSStorage)");

    RUN_TEST(fresh_device_all_pending);
    RUN_TEST(upload_clears_pending);
    RUN_TEST(new_records_after_upload_pending);
    RUN_TEST(reboot_preserves_marker);
    RUN_TEST(trim_adjusts_upload_marker);
    RUN_TEST(trim_past_upload_marker);
    RUN_TEST(no_false_pending_after_upload);
    RUN_TEST(total_bytes_uploaded);
    RUN_TEST(readRecords_skipRecords_param);
    RUN_TEST(readRecords_default_skipRecords);

    TEST_SUMMARY();
}
