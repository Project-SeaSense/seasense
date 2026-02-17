/**
 * SeaSense Logger - SPIFFS Storage Implementation
 */

#include "SPIFFSStorage.h"
#include "../../config/hardware_config.h"
#include <ArduinoJson.h>

// File paths
const char* SPIFFSStorage::DATA_FILE = "/data.csv";
const char* SPIFFSStorage::METADATA_FILE = "/metadata.json";
const char* SPIFFSStorage::TEMP_FILE = "/data.tmp";

// ============================================================================
// Constructor / Destructor
// ============================================================================

SPIFFSStorage::SPIFFSStorage(uint16_t maxRecords)
    : _maxRecords(maxRecords),
      _mounted(false)
{
    _metadata.lastUploadedMillis = 0;
    _metadata.totalRecordsWritten = 0;
}

SPIFFSStorage::~SPIFFSStorage() {
    if (_mounted) {
        SPIFFS.end();
    }
}

// ============================================================================
// IStorage Interface Implementation
// ============================================================================

bool SPIFFSStorage::begin() {
    DEBUG_STORAGE_PRINTLN("Initializing SPIFFS storage...");

    if (!SPIFFS.begin(true)) {  // true = format if mount fails
        DEBUG_STORAGE_PRINTLN("SPIFFS mount failed");
        _mounted = false;
        return false;
    }

    _mounted = true;
    DEBUG_STORAGE_PRINTLN("SPIFFS mounted successfully");

    // Load metadata
    if (!loadMetadata()) {
        DEBUG_STORAGE_PRINTLN("No metadata found, creating new");
        saveMetadata();
    }

    // Ensure data file exists with header
    if (!ensureDataFileWithHeader()) {
        DEBUG_STORAGE_PRINTLN("Failed to create data file");
        return false;
    }

    DEBUG_STORAGE_PRINT("SPIFFS initialized, ");
    DEBUG_STORAGE_PRINT(countRecords());
    DEBUG_STORAGE_PRINTLN(" records");

    return true;
}

bool SPIFFSStorage::isMounted() const {
    return _mounted;
}

bool SPIFFSStorage::write(const SensorData& data) {
    DataRecord record = sensorDataToRecord(data);
    return writeRecord(record);
}

bool SPIFFSStorage::writeRecord(const DataRecord& record) {
    if (!_mounted) {
        DEBUG_STORAGE_PRINTLN("SPIFFS not mounted");
        return false;
    }

    // Open file in append mode
    File file = SPIFFS.open(DATA_FILE, FILE_APPEND);
    if (!file) {
        DEBUG_STORAGE_PRINTLN("Failed to open data file for writing");
        return false;
    }

    // Write CSV line
    String csvLine = recordToCSV(record);
    file.println(csvLine);

    // Flush and close
    file.flush();
    file.close();

    // Update metadata
    _metadata.totalRecordsWritten++;
    saveMetadata();

    DEBUG_STORAGE_PRINT("Written record: ");
    DEBUG_STORAGE_PRINTLN(csvLine);

    // Trim old records if buffer is full
    uint32_t count = countRecords();
    if (count > _maxRecords) {
        DEBUG_STORAGE_PRINTLN("Circular buffer full, trimming old records");
        trimOldRecords();
    }

    return true;
}

std::vector<DataRecord> SPIFFSStorage::readRecords(
    unsigned long startMillis,
    uint16_t maxRecords
) {
    std::vector<DataRecord> records;

    if (!_mounted) {
        return records;
    }

    File file = SPIFFS.open(DATA_FILE, FILE_READ);
    if (!file) {
        DEBUG_STORAGE_PRINTLN("Failed to open data file for reading");
        return records;
    }

    // Skip CSV header
    if (file.available()) {
        file.readStringUntil('\n');
    }

    // Read records
    while (file.available() && records.size() < maxRecords) {
        String line = file.readStringUntil('\n');
        line.trim();

        if (line.length() == 0) continue;

        DataRecord record;
        if (parseCSVLine(line, record)) {
            // Filter by start time if specified
            if (startMillis == 0 || record.millis >= startMillis) {
                records.push_back(record);
            }
        }
    }

    file.close();

    DEBUG_STORAGE_PRINT("Read ");
    DEBUG_STORAGE_PRINT(records.size());
    DEBUG_STORAGE_PRINTLN(" records from SPIFFS");

    return records;
}

StorageStats SPIFFSStorage::getStats() const {
    StorageStats stats;
    stats.mounted = _mounted;

    if (_mounted) {
        stats.totalBytes = SPIFFS.totalBytes();
        stats.usedBytes = SPIFFS.usedBytes();
        stats.freeBytes = stats.totalBytes - stats.usedBytes;
        stats.totalRecords = countRecords();
        stats.recordsSinceUpload = 0;  // TODO: Calculate based on lastUploadedMillis
        stats.status = StorageStatus::OK;
    } else {
        stats.totalBytes = 0;
        stats.usedBytes = 0;
        stats.freeBytes = 0;
        stats.totalRecords = 0;
        stats.recordsSinceUpload = 0;
        stats.status = StorageStatus::NOT_MOUNTED;
    }

    return stats;
}

StorageStatus SPIFFSStorage::getStatus() const {
    if (!_mounted) {
        return StorageStatus::NOT_MOUNTED;
    }

    // Check if nearly full (>95%)
    if (SPIFFS.usedBytes() > (SPIFFS.totalBytes() * 0.95)) {
        return StorageStatus::FULL;
    }

    return StorageStatus::OK;
}

bool SPIFFSStorage::clear() {
    if (!_mounted) {
        return false;
    }

    DEBUG_STORAGE_PRINTLN("Clearing all SPIFFS data");

    // Remove data file
    if (SPIFFS.exists(DATA_FILE)) {
        SPIFFS.remove(DATA_FILE);
    }

    // Reset metadata
    _metadata.lastUploadedMillis = 0;
    _metadata.totalRecordsWritten = 0;
    saveMetadata();

    // Recreate data file with header
    return ensureDataFileWithHeader();
}

bool SPIFFSStorage::format() {
    DEBUG_STORAGE_PRINTLN("Formatting SPIFFS...");

    SPIFFS.end();
    _mounted = false;

    if (!SPIFFS.format()) {
        DEBUG_STORAGE_PRINTLN("SPIFFS format failed");
        return false;
    }

    // Remount
    return begin();
}

bool SPIFFSStorage::flush() {
    // SPIFFS doesn't require explicit flush
    // Data is written immediately
    return true;
}

String SPIFFSStorage::getCSVHeader() const {
    return "millis,timestamp_utc,latitude,longitude,altitude,gps_sats,gps_hdop,"
           "sensor_type,sensor_model,sensor_serial,sensor_instance,calibration_date,"
           "value,unit,quality,"
           "wind_speed_true_ms,wind_angle_true_deg,wind_speed_app_ms,wind_angle_app_deg,"
           "water_depth_m,stw_ms,water_temp_ext_c,air_temp_c,baro_pressure_pa,"
           "humidity_pct,cog_deg,sog_ms,heading_deg,pitch_deg,roll_deg";
}

// Helper: format float for CSV, empty string if NaN
static String csvFloat(float v, int decimals) {
    if (isnan(v)) return "";
    return String(v, decimals);
}

String SPIFFSStorage::recordToCSV(const DataRecord& record) const {
    String csv = "";
    csv += String(record.millis);
    csv += ",";
    csv += record.timestampUTC.length() > 0 ? record.timestampUTC : "";
    csv += ",";
    csv += String(record.latitude, 6);  // 6 decimal places for GPS coordinates
    csv += ",";
    csv += String(record.longitude, 6);
    csv += ",";
    csv += String(record.altitude, 1);
    csv += ",";
    csv += String(record.gps_satellites);
    csv += ",";
    csv += String(record.gps_hdop, 1);
    csv += ",";
    csv += record.sensorType;
    csv += ",";
    csv += record.sensorModel;
    csv += ",";
    csv += record.sensorSerial;
    csv += ",";
    csv += String(record.sensorInstance);
    csv += ",";
    csv += record.calibrationDate;
    csv += ",";
    csv += String(record.value, 2);
    csv += ",";
    csv += record.unit;
    csv += ",";
    csv += record.quality;
    // NMEA2000 environmental fields (empty if NaN/unavailable)
    csv += "," + csvFloat(record.windSpeedTrue, 2);
    csv += "," + csvFloat(record.windAngleTrue, 1);
    csv += "," + csvFloat(record.windSpeedApparent, 2);
    csv += "," + csvFloat(record.windAngleApparent, 1);
    csv += "," + csvFloat(record.waterDepth, 2);
    csv += "," + csvFloat(record.speedThroughWater, 2);
    csv += "," + csvFloat(record.waterTempExternal, 2);
    csv += "," + csvFloat(record.airTemp, 2);
    csv += "," + csvFloat(record.baroPressure, 0);
    csv += "," + csvFloat(record.humidity, 1);
    csv += "," + csvFloat(record.cogTrue, 1);
    csv += "," + csvFloat(record.sog, 2);
    csv += "," + csvFloat(record.heading, 1);
    csv += "," + csvFloat(record.pitch, 1);
    csv += "," + csvFloat(record.roll, 1);
    return csv;
}

unsigned long SPIFFSStorage::getLastUploadedMillis() const {
    return _metadata.lastUploadedMillis;
}

bool SPIFFSStorage::setLastUploadedMillis(unsigned long millis) {
    _metadata.lastUploadedMillis = millis;
    return saveMetadata();
}

// ============================================================================
// Private Helper Methods
// ============================================================================

bool SPIFFSStorage::loadMetadata() {
    if (!SPIFFS.exists(METADATA_FILE)) {
        return false;
    }

    File file = SPIFFS.open(METADATA_FILE, FILE_READ);
    if (!file) {
        return false;
    }

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        DEBUG_STORAGE_PRINT("Metadata parse error: ");
        DEBUG_STORAGE_PRINTLN(error.c_str());
        return false;
    }

    _metadata.lastUploadedMillis = doc["lastUploadedMillis"] | 0UL;
    _metadata.totalRecordsWritten = doc["totalRecordsWritten"] | 0U;

    DEBUG_STORAGE_PRINTLN("Metadata loaded");
    return true;
}

bool SPIFFSStorage::saveMetadata() {
    File file = SPIFFS.open(METADATA_FILE, FILE_WRITE);
    if (!file) {
        DEBUG_STORAGE_PRINTLN("Failed to open metadata file for writing");
        return false;
    }

    StaticJsonDocument<256> doc;
    doc["lastUploadedMillis"] = _metadata.lastUploadedMillis;
    doc["totalRecordsWritten"] = _metadata.totalRecordsWritten;

    serializeJson(doc, file);
    file.flush();
    file.close();

    DEBUG_STORAGE_PRINTLN("Metadata saved");
    return true;
}

uint32_t SPIFFSStorage::countRecords() const {
    if (!_mounted || !SPIFFS.exists(DATA_FILE)) {
        return 0;
    }

    File file = SPIFFS.open(DATA_FILE, FILE_READ);
    if (!file) {
        return 0;
    }

    uint32_t count = 0;
    while (file.available()) {
        file.readStringUntil('\n');
        count++;
    }
    file.close();

    // Subtract 1 for header line
    return count > 0 ? count - 1 : 0;
}

bool SPIFFSStorage::trimOldRecords() {
    if (!_mounted) {
        return false;
    }

    DEBUG_STORAGE_PRINTLN("Trimming old records...");

    // Read all records
    std::vector<DataRecord> allRecords = readRecords(0, 65535);

    if (allRecords.size() <= _maxRecords) {
        return true;  // Nothing to trim
    }

    // Keep only the most recent _maxRecords
    size_t startIndex = allRecords.size() - _maxRecords;

    // Write to temporary file
    File tempFile = SPIFFS.open(TEMP_FILE, FILE_WRITE);
    if (!tempFile) {
        DEBUG_STORAGE_PRINTLN("Failed to create temp file");
        return false;
    }

    // Write header
    tempFile.println(getCSVHeader());

    // Write recent records
    for (size_t i = startIndex; i < allRecords.size(); i++) {
        tempFile.println(recordToCSV(allRecords[i]));
    }

    tempFile.flush();
    tempFile.close();

    // Replace original file with temp file
    SPIFFS.remove(DATA_FILE);
    SPIFFS.rename(TEMP_FILE, DATA_FILE);

    DEBUG_STORAGE_PRINT("Trimmed to ");
    DEBUG_STORAGE_PRINT(_maxRecords);
    DEBUG_STORAGE_PRINTLN(" records");

    return true;
}

// Helper: parse a CSV field as float, returning NaN for empty fields
static float parseOptionalFloat(const String& field) {
    if (field.length() == 0) return NAN;
    return field.toFloat();
}

bool SPIFFSStorage::parseCSVLine(const String& line, DataRecord& record) const {
    // Parse CSV: millis,timestamp_utc,latitude,longitude,altitude,gps_sats,gps_hdop,
    //            sensor_type,sensor_model,sensor_serial,sensor_instance,
    //            calibration_date,value,unit,quality,
    //            [env fields 15-29...]

    // Initialize environmental fields to NaN (backwards compat with old CSV)
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

    int fieldIndex = 0;
    int lastComma = -1;
    int nextComma = 0;

    for (int i = 0; i <= (int)line.length(); i++) {
        if (i == (int)line.length() || line[i] == ',') {
            nextComma = i;
            String field = line.substring(lastComma + 1, nextComma);
            field.trim();

            switch (fieldIndex) {
                case 0: record.millis = field.toInt(); break;
                case 1: record.timestampUTC = field; break;
                case 2: record.latitude = field.toDouble(); break;
                case 3: record.longitude = field.toDouble(); break;
                case 4: record.altitude = field.toDouble(); break;
                case 5: record.gps_satellites = field.toInt(); break;
                case 6: record.gps_hdop = field.toDouble(); break;
                case 7: record.sensorType = field; break;
                case 8: record.sensorModel = field; break;
                case 9: record.sensorSerial = field; break;
                case 10: record.sensorInstance = field.toInt(); break;
                case 11: record.calibrationDate = field; break;
                case 12: record.value = field.toFloat(); break;
                case 13: record.unit = field; break;
                case 14: record.quality = field; break;
                // NMEA2000 environmental fields (optional)
                case 15: record.windSpeedTrue = parseOptionalFloat(field); break;
                case 16: record.windAngleTrue = parseOptionalFloat(field); break;
                case 17: record.windSpeedApparent = parseOptionalFloat(field); break;
                case 18: record.windAngleApparent = parseOptionalFloat(field); break;
                case 19: record.waterDepth = parseOptionalFloat(field); break;
                case 20: record.speedThroughWater = parseOptionalFloat(field); break;
                case 21: record.waterTempExternal = parseOptionalFloat(field); break;
                case 22: record.airTemp = parseOptionalFloat(field); break;
                case 23: record.baroPressure = parseOptionalFloat(field); break;
                case 24: record.humidity = parseOptionalFloat(field); break;
                case 25: record.cogTrue = parseOptionalFloat(field); break;
                case 26: record.sog = parseOptionalFloat(field); break;
                case 27: record.heading = parseOptionalFloat(field); break;
                case 28: record.pitch = parseOptionalFloat(field); break;
                case 29: record.roll = parseOptionalFloat(field); break;
            }

            fieldIndex++;
            lastComma = nextComma;
        }
    }

    // Support old format (15 fields) and new format (30 fields)
    return (fieldIndex >= 10);
}

bool SPIFFSStorage::ensureDataFileWithHeader() {
    if (SPIFFS.exists(DATA_FILE)) {
        return true;  // File already exists
    }

    File file = SPIFFS.open(DATA_FILE, FILE_WRITE);
    if (!file) {
        return false;
    }

    file.println(getCSVHeader());
    file.flush();
    file.close();

    DEBUG_STORAGE_PRINTLN("Created data file with header");
    return true;
}
