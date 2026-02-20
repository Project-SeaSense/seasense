/**
 * SeaSense Logger - Storage Interface
 *
 * Abstract base class for storage implementations
 * Supports both SPIFFS (circular buffer) and SD card (permanent storage)
 */

#ifndef STORAGE_INTERFACE_H
#define STORAGE_INTERFACE_H

#include <Arduino.h>
#include <vector>
#include "../sensors/SensorInterface.h"

/**
 * Storage status enumeration
 */
enum class StorageStatus {
    OK,              // Storage is operational
    NOT_MOUNTED,     // Storage device is not mounted
    FULL,            // Storage device is full
    WRITE_ERROR,     // Write operation failed
    READ_ERROR,      // Read operation failed
    CORRUPTED        // Storage filesystem is corrupted
};

/**
 * Storage statistics
 */
struct StorageStats {
    bool mounted;              // Is storage mounted?
    uint64_t totalBytes;       // Total capacity in bytes
    uint64_t usedBytes;        // Used space in bytes
    uint64_t freeBytes;        // Free space in bytes
    uint32_t totalRecords;     // Total number of records stored
    uint32_t recordsSinceUpload; // Records since last API upload
    String oldestRecordTime;   // millis() of oldest record (or ISO date if available)
    String newestRecordTime;   // millis() of newest record (or ISO date if available)
    StorageStatus status;      // Current status
};

/**
 * Data record structure for CSV storage
 * Matches the CSV format: millis,timestamp_utc,latitude,longitude,altitude,
 * gps_sats,gps_hdop,sensor_type,sensor_model,sensor_serial,
 * sensor_instance,calibration_date,value,unit,quality,
 * wind_speed_true_ms,wind_angle_true_deg,wind_speed_app_ms,wind_angle_app_deg,
 * water_depth_m,stw_ms,water_temp_ext_c,air_temp_c,baro_pressure_pa,
 * humidity_pct,cog_deg,sog_ms,heading_deg,pitch_deg,roll_deg
 */
struct DataRecord {
    unsigned long millis;      // millis() when reading was taken
    String timestampUTC;       // Absolute UTC timestamp (from GPS or NTP)
    double latitude;           // GPS latitude in degrees (NaN if no fix)
    double longitude;          // GPS longitude in degrees (NaN if no fix)
    double altitude;           // GPS altitude in meters (NaN if no fix)
    uint8_t gps_satellites;    // Number of GPS satellites (0 if no fix)
    double gps_hdop;           // GPS horizontal dilution of precision (NaN if no fix)
    String sensorType;         // e.g., "Temperature", "Conductivity"
    String sensorModel;        // e.g., "EZO-RTD", "EZO-EC"
    String sensorSerial;       // e.g., "RTD-12345"
    uint8_t sensorInstance;    // NMEA2000 instance number
    String calibrationDate;    // ISO 8601 date of last calibration
    float value;               // Sensor reading value
    String unit;               // Unit of measurement
    String quality;            // Quality indicator string

    // NMEA2000 environmental context (NaN = not available)
    float windSpeedTrue;       // m/s
    float windAngleTrue;       // degrees
    float windSpeedApparent;   // m/s
    float windAngleApparent;   // degrees
    float waterDepth;          // meters below transducer
    float speedThroughWater;   // m/s
    float waterTempExternal;   // °C from boat transducer
    float airTemp;             // °C
    float baroPressure;        // Pascals
    float humidity;            // % relative
    float cogTrue;             // degrees
    float sog;                 // m/s
    float heading;             // degrees true
    float pitch;               // degrees
    float roll;                // degrees
};

/**
 * Abstract storage interface
 * All storage implementations must inherit from this interface
 */
class IStorage {
public:
    virtual ~IStorage() {}

    /**
     * Initialize the storage device
     * @return true if initialization successful, false otherwise
     */
    virtual bool begin() = 0;

    /**
     * Check if storage is mounted and accessible
     * @return true if mounted, false otherwise
     */
    virtual bool isMounted() const = 0;

    /**
     * Write a sensor data record to storage
     * @param data SensorData structure to write
     * @return true if write successful, false otherwise
     */
    virtual bool write(const SensorData& data) = 0;

    /**
     * Write a raw data record to storage
     * @param record DataRecord structure to write
     * @return true if write successful, false otherwise
     */
    virtual bool writeRecord(const DataRecord& record) = 0;

    /**
     * Read records from storage
     * @param startMillis Start time (millis()) - read records after this time
     * @param maxRecords Maximum number of records to read
     * @param skipRecords Number of records to skip from the start (for pagination)
     * @return Vector of DataRecord structures
     */
    virtual std::vector<DataRecord> readRecords(
        unsigned long startMillis = 0,
        uint16_t maxRecords = 100,
        uint32_t skipRecords = 0
    ) = 0;

    /**
     * Get storage statistics
     * @return StorageStats structure
     */
    virtual StorageStats getStats() const = 0;

    /**
     * Get storage status
     * @return StorageStatus enum
     */
    virtual StorageStatus getStatus() const = 0;

    /**
     * Clear all data from storage
     * @return true if successful, false otherwise
     */
    virtual bool clear() = 0;

    /**
     * Format the storage device
     * ⚠️ WARNING: This will erase all data
     * @return true if successful, false otherwise
     */
    virtual bool format() = 0;

    /**
     * Get the storage type identifier
     * @return String type (e.g., "SPIFFS", "SD")
     */
    virtual String getStorageType() const = 0;

    /**
     * Flush any pending writes to storage
     * Ensures data is written to disk before power loss
     * @return true if successful, false otherwise
     */
    virtual bool flush() = 0;

    /**
     * Get CSV header string
     * @return CSV header line
     */
    virtual String getCSVHeader() const = 0;

    /**
     * Convert DataRecord to CSV line
     * @param record DataRecord to convert
     * @return CSV formatted string
     */
    virtual String recordToCSV(const DataRecord& record) const = 0;

    /**
     * Get the millis() timestamp of the last uploaded record
     * Used for resuming uploads after connection loss
     * @return millis() timestamp, or 0 if no records uploaded yet
     */
    virtual unsigned long getLastUploadedMillis() const = 0;

    /**
     * Set the millis() timestamp of the last uploaded record
     * @param millis millis() timestamp
     * @return true if successful, false otherwise
     */
    virtual bool setLastUploadedMillis(unsigned long millis) = 0;
};

/**
 * Helper function to convert SensorData to DataRecord
 */
inline DataRecord sensorDataToRecord(const SensorData& data, const String& timestampUTC = "") {
    DataRecord record;
    record.millis = data.timestamp;
    record.timestampUTC = timestampUTC;
    record.latitude = NAN;           // NaN = no GPS fix
    record.longitude = NAN;
    record.altitude = NAN;
    record.gps_satellites = 0;
    record.gps_hdop = NAN;
    record.sensorType = data.sensorType;
    record.sensorModel = data.sensorModel;
    record.sensorSerial = data.sensorSerial;
    record.sensorInstance = data.sensorInstance;
    record.calibrationDate = data.calibrationDate;
    record.value = data.value;
    record.unit = data.unit;
    record.quality = sensorQualityToString(data.quality);
    // Initialize NMEA2000 environmental fields to NaN (not available)
    record.windSpeedTrue = NAN;
    record.windAngleTrue = NAN;
    record.windSpeedApparent = NAN;
    record.windAngleApparent = NAN;
    record.waterDepth = NAN;
    record.speedThroughWater = NAN;
    record.waterTempExternal = NAN;
    record.airTemp = NAN;
    record.baroPressure = NAN;
    record.humidity = NAN;
    record.cogTrue = NAN;
    record.sog = NAN;
    record.heading = NAN;
    record.pitch = NAN;
    record.roll = NAN;
    return record;
}

/**
 * Helper function to convert StorageStatus enum to string
 */
inline String storageStatusToString(StorageStatus status) {
    switch (status) {
        case StorageStatus::OK:           return "OK";
        case StorageStatus::NOT_MOUNTED:  return "Not Mounted";
        case StorageStatus::FULL:         return "Full";
        case StorageStatus::WRITE_ERROR:  return "Write Error";
        case StorageStatus::READ_ERROR:   return "Read Error";
        case StorageStatus::CORRUPTED:    return "Corrupted";
        default:                          return "Unknown";
    }
}

#endif // STORAGE_INTERFACE_H
