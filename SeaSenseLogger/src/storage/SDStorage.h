/**
 * SeaSense Logger - SD Card Storage Implementation
 *
 * Implements permanent storage on SD card
 * - Large capacity (1GB+ = 30+ years at 5min intervals)
 * - Removable for manual data retrieval
 * - Power-loss safe write operations
 * - CSV format with full sensor metadata
 */

#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include "StorageInterface.h"
#include <SD.h>
#include <SPI.h>

class SDStorage : public IStorage {
public:
    /**
     * Constructor
     * @param csPin Chip select pin for SD card
     */
    explicit SDStorage(uint8_t csPin);

    virtual ~SDStorage();

    // ========================================================================
    // IStorage Interface Implementation
    // ========================================================================

    virtual bool begin() override;
    virtual bool isMounted() const override;
    virtual bool write(const SensorData& data) override;
    virtual bool writeRecord(const DataRecord& record) override;
    virtual std::vector<DataRecord> readRecords(
        unsigned long startMillis = 0,
        uint16_t maxRecords = 100,
        uint32_t skipRecords = 0
    ) override;
    virtual StorageStats getStats() const override;
    virtual StorageStatus getStatus() const override;
    virtual bool clear() override;
    virtual bool format() override;
    virtual String getStorageType() const override { return "SD"; }
    virtual bool flush() override;
    virtual String getCSVHeader() const override;
    virtual String recordToCSV(const DataRecord& record) const override;
    virtual unsigned long getLastUploadedMillis() const override;
    virtual bool setLastUploadedMillis(unsigned long millis) override;

    // ========================================================================
    // SD-Specific Methods
    // ========================================================================

    /**
     * Check if SD card is present and accessible
     * @return true if card is present
     */
    bool isCardPresent() const;

    /**
     * Get SD card size in bytes
     * @return Card size in bytes
     */
    uint64_t getCardSize() const;

    /**
     * Get card type (SD, SDHC, etc.)
     * @return String description of card type
     */
    String getCardType() const;

private:
    // ========================================================================
    // Configuration
    // ========================================================================

    uint8_t _csPin;                 // Chip select pin
    bool _mounted;                  // Is SD card mounted?
    SPIClass _spi;                  // Dedicated SPI bus (HSPI/SPI3)

    // File paths
    static const char* DATA_FILE;        // "/data.csv"
    static const char* METADATA_FILE;    // "/metadata.json"

    // Metadata
    struct Metadata {
        unsigned long lastUploadedMillis;
        uint32_t recordsAtLastUpload;
    } _metadata;

    // ========================================================================
    // Helper Methods
    // ========================================================================

    /**
     * Load metadata from JSON file on SD card
     * @return true if successful
     */
    bool loadMetadata();

    /**
     * Save metadata to JSON file on SD card
     * @return true if successful
     */
    bool saveMetadata();

    /**
     * Count records in data file
     * @return Number of records
     */
    uint32_t countRecords() const;

    /**
     * Parse CSV line into DataRecord
     * @param line CSV line string
     * @param record Output DataRecord
     * @return true if parsing successful
     */
    bool parseCSVLine(const String& line, DataRecord& record) const;

    /**
     * Ensure data file exists with CSV header
     * Creates file with header if it doesn't exist
     * @return true if successful
     */
    bool ensureDataFileWithHeader();

    /**
     * Safe write operation with power-loss protection
     * Opens file, writes, flushes, and closes immediately
     * @param data String data to write
     * @return true if successful
     */
    bool safeWrite(const String& data);
};

#endif // SD_STORAGE_H
