/**
 * SeaSense Logger - Storage Manager
 *
 * Orchestrates dual storage system (SPIFFS + SD card)
 * - Writes to both storage systems simultaneously
 * - Provides graceful degradation if one fails
 * - Tracks upload progress across both systems
 * - Power-loss safe operations
 */

#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include "StorageInterface.h"
#include "SPIFFSStorage.h"
#include "SDStorage.h"

class StorageManager {
public:
    /**
     * Constructor
     * @param spiffsMaxRecords Maximum records in SPIFFS circular buffer
     * @param sdCsPin SD card chip select pin
     */
    StorageManager(uint16_t spiffsMaxRecords = 100, uint8_t sdCsPin = 5);

    ~StorageManager();

    /**
     * Initialize both storage systems
     * @return true if at least one storage system initialized successfully
     */
    bool begin();

    /**
     * Write sensor data to both storage systems
     * @param data SensorData to write
     * @return true if written to at least one storage system
     */
    bool write(const SensorData& data);

    /**
     * Write raw data record to both storage systems
     * @param record DataRecord to write
     * @return true if written to at least one storage system
     */
    bool writeRecord(const DataRecord& record);

    /**
     * Read records from primary storage (SD card if available, else SPIFFS)
     * @param startMillis Start time (millis()) - read records after this time
     * @param maxRecords Maximum number of records to read
     * @return Vector of DataRecord structures
     */
    std::vector<DataRecord> readRecords(
        unsigned long startMillis = 0,
        uint16_t maxRecords = 100,
        uint32_t skipRecords = 0
    );

    /**
     * Get combined storage statistics
     * @return Combined StorageStats structure
     */
    StorageStats getStats() const;

    /**
     * Get overall storage status
     * @return StorageStatus enum
     */
    StorageStatus getStatus() const;

    /**
     * Clear all data from both storage systems
     * @return true if successful
     */
    bool clear();

    /**
     * Get last uploaded millis timestamp
     * Reads from SPIFFS (primary upload tracker)
     * @return millis() timestamp of last upload
     */
    unsigned long getLastUploadedMillis() const;

    /**
     * Set last uploaded millis timestamp
     * Writes to SPIFFS (primary upload tracker)
     * @param millis millis() timestamp
     * @return true if successful
     */
    bool setLastUploadedMillis(unsigned long millis);

    /**
     * Check if SPIFFS is mounted
     * @return true if mounted
     */
    bool isSPIFFSMounted() const;

    /**
     * Check if SD card is mounted
     * @return true if mounted
     */
    bool isSDMounted() const;

    /**
     * Get SPIFFS statistics
     * @return StorageStats for SPIFFS
     */
    StorageStats getSPIFFSStats() const;

    /**
     * Get SD card statistics
     * @return StorageStats for SD card
     */
    StorageStats getSDStats() const;

    /**
     * Add bytes to persistent lifetime upload counter
     * @param bytes Number of bytes uploaded
     */
    void addBytesUploaded(size_t bytes);

    /**
     * Get persistent lifetime total bytes uploaded
     * @return Total bytes uploaded across all sessions
     */
    uint64_t getTotalBytesUploaded() const;

    /**
     * Get human-readable status string
     * @return Status description
     */
    String getStatusString() const;

private:
    SPIFFSStorage* _spiffs;
    SDStorage* _sd;

    bool _spiffsAvailable;
    bool _sdAvailable;

    /**
     * Get primary storage (SD if available, else SPIFFS)
     * @return Pointer to primary storage
     */
    IStorage* getPrimaryStorage() const;

    /**
     * Get secondary storage (SPIFFS if SD is primary, else nullptr)
     * @return Pointer to secondary storage
     */
    IStorage* getSecondaryStorage() const;
};

#endif // STORAGE_MANAGER_H
