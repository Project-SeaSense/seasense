/**
 * SeaSense Logger - API Uploader
 *
 * Bandwidth-conscious upload to Project SeaSense API
 * - Configurable upload interval and batch size
 * - Progress tracking (resume after connection loss)
 * - Gentle retry with exponential backoff
 * - NTP time sync for absolute timestamps
 */

#ifndef API_UPLOADER_H
#define API_UPLOADER_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "../storage/StorageManager.h"

/**
 * Upload status
 */
enum class UploadStatus {
    IDLE,               // No upload in progress
    SYNCING_TIME,       // Syncing time with NTP
    QUERYING_DATA,      // Reading data from storage
    UPLOADING,          // Uploading to API
    SUCCESS,            // Upload successful
    ERROR_NO_WIFI,      // No WiFi connection
    ERROR_NO_TIME,      // NTP sync failed
    ERROR_NO_DATA,      // No data to upload
    ERROR_API           // API request failed
};

/**
 * Single upload attempt record (stored in circular history buffer)
 */
struct UploadRecord {
    unsigned long startMs;      // millis() when upload attempt began
    unsigned long durationMs;   // how long the attempt took
    bool success;
    uint32_t recordCount;       // sensor records in the batch
    size_t payloadBytes;        // wire bytes sent (compressed if applicable; 0 on pre-send failure)
};

/**
 * Upload configuration
 */
struct UploadConfig {
    String apiUrl;              // API endpoint URL
    String apiKey;              // API authentication key
    String partnerID;           // Partner ID
    String deviceGUID;          // Device GUID
    bool enabled;               // Enable/disable uploads
    unsigned long intervalMs;   // Upload interval in milliseconds
    uint16_t batchSize;         // Records per upload batch
    uint8_t maxRetries;         // Maximum retry attempts
};

class APIUploader {
public:
    /**
     * Constructor
     * @param storage Pointer to storage manager
     */
    explicit APIUploader(StorageManager* storage);

    /**
     * Initialize API uploader
     * @param config Upload configuration
     * @return true if successful
     */
    bool begin(const UploadConfig& config);

    /**
     * Process upload cycle
     * Call this periodically from loop()
     * Non-blocking - returns immediately if not time to upload
     */
    void process();

    /**
     * Get current upload status
     * @return UploadStatus enum
     */
    UploadStatus getStatus() const { return _status; }

    /**
     * Get status string
     * @return Human-readable status
     */
    String getStatusString() const;

    /**
     * Get last upload time
     * @return millis() of last successful upload
     */
    unsigned long getLastUploadTime() const { return _lastUploadTime; }

    /**
     * Get records pending upload
     * @return Number of records since last upload
     */
    uint32_t getPendingRecords() const;

    /**
     * Get time until next upload
     * @return Milliseconds until next upload attempt
     */
    unsigned long getTimeUntilNext() const;

    /**
     * Check if NTP time is synchronized
     * @return true if time is synced
     */
    bool isTimeSynced() const { return _timeSynced; }

    /**
     * Get current retry count
     * @return Number of consecutive failed upload attempts
     */
    uint8_t getRetryCount() const { return _retryCount; }

    /**
     * Get last error detail string
     * @return Descriptive error message (empty if no error)
     */
    const String& getLastError() const { return _lastError; }

    /**
     * Force immediate upload attempt
     * Ignores interval timing
     */
    void forceUpload();

    /**
     * Update device GUID for subsequent uploads
     * Called after GUID regeneration so next upload uses the new value
     */
    void setDeviceGUID(const String& guid) { _config.deviceGUID = guid; }

    /**
     * Get upload history (most-recent-first order)
     * @param count Set to number of valid entries returned
     * @return Pointer to history array (oldest→newest internally; caller iterates count-1 down to 0 for newest-first)
     */
    const UploadRecord* getUploadHistory(uint8_t& count) const;

    /** Index of the most-recently-written history slot + 1 (i.e., next write position) */
    uint8_t getHistoryHead() const { return _historyHead; }

    static const uint8_t UPLOAD_HISTORY_SIZE = 10;

    /**
     * Get total bytes sent this session (resets on reboot)
     */
    unsigned long getTotalBytesSent() const { return _totalBytesSent; }

    /** Last upload attempt start time (millis), success or fail */
    unsigned long getLastAttemptTime() const { return _lastAttemptTime; }

    /** Human-readable reason for latest upload failure (empty if none) */
    String getLastError() const { return _lastError; }

    /** True when a forced upload has been queued and not yet processed */
    bool isForcePending() const { return _forcePending; }

private:
    StorageManager* _storage;
    UploadConfig _config;

    // State
    UploadStatus _status;
    unsigned long _lastUploadTime;
    unsigned long _lastScheduledTime;   // millis() anchor for elapsed-time pattern
    unsigned long _currentIntervalMs;   // active interval (normal or retry backoff)
    uint8_t _retryCount;
    bool _timeSynced;
    time_t _bootTimeEpoch;  // Epoch time when ESP32 booted
    String _lastError;      // Descriptive last error message

    // Upload history
    UploadRecord _uploadHistory[UPLOAD_HISTORY_SIZE];
    uint8_t _historyCount;      // valid entries (0..10)
    uint8_t _historyHead;       // index where next entry will be written
    unsigned long _totalBytesSent;  // session total wire bytes
    size_t _lastPayloadBytes;   // set by uploadPayload(), consumed by process()
    unsigned long _lastAttemptTime; // millis() of latest attempt (success or fail)
    String _lastError;          // last failure reason
    bool _forcePending;         // force-upload request queued

    /**
     * Check if WiFi is connected
     * @return true if connected
     */
    bool isWiFiConnected() const;

    /**
     * Sync time with NTP server
     * @return true if successful
     */
    bool syncNTP();

    /**
     * Convert millis() to absolute UTC timestamp
     * @param millisTimestamp millis() timestamp
     * @return ISO 8601 formatted UTC timestamp
     */
    String millisToUTC(unsigned long millisTimestamp) const;

    /**
     * Build API payload from data records
     * @param records Vector of data records
     * @return JSON payload string
     */
    String buildPayload(const std::vector<DataRecord>& records) const;

    /**
     * Upload payload to API
     * @param payload JSON payload
     * @return true if successful
     */
    bool uploadPayload(const String& payload);

    /**
     * Schedule retry with exponential backoff
     */
    void scheduleRetry();

    /**
     * Reset retry count and timing
     */
    void resetRetry();
};

#endif // API_UPLOADER_H
