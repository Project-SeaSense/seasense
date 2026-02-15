/**
 * SeaSense Logger - SPIFFS Storage Implementation
 *
 * Implements circular buffer storage in ESP32 SPIFFS flash memory
 * - Fast access for recent records
 * - Limited capacity (~100 records)
 * - Tracks upload progress (last uploaded millis)
 * - Survives deep sleep and reboots
 */

#ifndef SPIFFS_STORAGE_H
#define SPIFFS_STORAGE_H

#include "StorageInterface.h"
#include <SPIFFS.h>
#include <FS.h>

class SPIFFSStorage : public IStorage {
public:
    /**
     * Constructor
     * @param maxRecords Maximum number of records to keep (circular buffer)
     */
    explicit SPIFFSStorage(uint16_t maxRecords = 100);

    virtual ~SPIFFSStorage();

    // ========================================================================
    // IStorage Interface Implementation
    // ========================================================================

    virtual bool begin() override;
    virtual bool isMounted() const override;
    virtual bool write(const SensorData& data) override;
    virtual bool writeRecord(const DataRecord& record) override;
    virtual std::vector<DataRecord> readRecords(
        unsigned long startMillis = 0,
        uint16_t maxRecords = 100
    ) override;
    virtual StorageStats getStats() const override;
    virtual StorageStatus getStatus() const override;
    virtual bool clear() override;
    virtual bool format() override;
    virtual String getStorageType() const override { return "SPIFFS"; }
    virtual bool flush() override;
    virtual String getCSVHeader() const override;
    virtual String recordToCSV(const DataRecord& record) const override;
    virtual unsigned long getLastUploadedMillis() const override;
    virtual bool setLastUploadedMillis(unsigned long millis) override;

private:
    // ========================================================================
    // Configuration
    // ========================================================================

    uint16_t _maxRecords;           // Maximum records in circular buffer
    bool _mounted;                  // Is SPIFFS mounted?

    // File paths
    static const char* DATA_FILE;        // "/spiffs/data.csv"
    static const char* METADATA_FILE;    // "/spiffs/metadata.json"
    static const char* TEMP_FILE;        // "/spiffs/data.tmp"

    // ========================================================================
    // Helper Methods
    // ========================================================================

    /**
     * Load metadata from JSON file
     * Includes: last uploaded millis, record count, etc.
     * @return true if successful
     */
    bool loadMetadata();

    /**
     * Save metadata to JSON file
     * @return true if successful
     */
    bool saveMetadata();

    /**
     * Count records in data file
     * @return Number of records
     */
    uint32_t countRecords() const;

    /**
     * Trim old records to maintain circular buffer size
     * Keeps the most recent _maxRecords
     * @return true if successful
     */
    bool trimOldRecords();

    /**
     * Parse CSV line into DataRecord
     * @param line CSV line string
     * @param record Output DataRecord
     * @return true if parsing successful
     */
    bool parseCSVLine(const String& line, DataRecord& record) const;

    /**
     * Ensure data file has CSV header
     * Creates file with header if it doesn't exist
     * @return true if successful
     */
    bool ensureDataFileWithHeader();

    // Metadata
    struct Metadata {
        unsigned long lastUploadedMillis;
        uint32_t totalRecordsWritten;
    } _metadata;
};

#endif // SPIFFS_STORAGE_H
