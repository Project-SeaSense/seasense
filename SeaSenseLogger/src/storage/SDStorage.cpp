/**
 * SeaSense Logger - SD Card Storage Implementation
 */

#include "SDStorage.h"
#include "../../config/hardware_config.h"
#include <ArduinoJson.h>

// File paths
const char* SDStorage::DATA_FILE = "/data.csv";
const char* SDStorage::METADATA_FILE = "/metadata.json";

// ============================================================================
// Constructor / Destructor
// ============================================================================

SDStorage::SDStorage(uint8_t csPin)
    : _csPin(csPin),
      _mounted(false)
{
    _metadata.lastUploadedMillis = 0;
}

SDStorage::~SDStorage() {
    if (_mounted) {
        SD.end();
    }
}

// ============================================================================
// IStorage Interface Implementation
// ============================================================================

bool SDStorage::begin() {
    DEBUG_STORAGE_PRINTLN("Initializing SD card storage...");

    // Initialize SPI
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, _csPin);

    // Mount SD card
    if (!SD.begin(_csPin, SPI, SD_SPI_FREQUENCY)) {
        DEBUG_STORAGE_PRINTLN("SD card mount failed");
        _mounted = false;
        return false;
    }

    _mounted = true;
    DEBUG_STORAGE_PRINTLN("SD card mounted successfully");

    // Print card info
    DEBUG_STORAGE_PRINT("Card type: ");
    DEBUG_STORAGE_PRINTLN(getCardType());
    DEBUG_STORAGE_PRINT("Card size: ");
    DEBUG_STORAGE_PRINT(getCardSize() / (1024 * 1024));
    DEBUG_STORAGE_PRINTLN(" MB");

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

    DEBUG_STORAGE_PRINT("SD card initialized, ");
    DEBUG_STORAGE_PRINT(countRecords());
    DEBUG_STORAGE_PRINTLN(" records");

    return true;
}

bool SDStorage::isMounted() const {
    return _mounted;
}

bool SDStorage::write(const SensorData& data) {
    DataRecord record = sensorDataToRecord(data);
    return writeRecord(record);
}

bool SDStorage::writeRecord(const DataRecord& record) {
    if (!_mounted) {
        DEBUG_STORAGE_PRINTLN("SD card not mounted");
        return false;
    }

    // Convert to CSV
    String csvLine = recordToCSV(record);

    // Safe write with power-loss protection
    return safeWrite(csvLine);
}

std::vector<DataRecord> SDStorage::readRecords(
    unsigned long startMillis,
    uint16_t maxRecords
) {
    std::vector<DataRecord> records;

    if (!_mounted) {
        return records;
    }

    File file = SD.open(DATA_FILE, FILE_READ);
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
    DEBUG_STORAGE_PRINTLN(" records from SD card");

    return records;
}

StorageStats SDStorage::getStats() const {
    StorageStats stats;
    stats.mounted = _mounted;

    if (_mounted) {
        stats.totalBytes = SD.totalBytes();
        stats.usedBytes = SD.usedBytes();
        stats.freeBytes = stats.totalBytes - stats.usedBytes;
        stats.totalRecords = countRecords();
        stats.recordsSinceUpload = 0;  // TODO: Calculate based on lastUploadedMillis
        stats.status = getStatus();
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

StorageStatus SDStorage::getStatus() const {
    if (!_mounted) {
        return StorageStatus::NOT_MOUNTED;
    }

    // Check if SD card is still present
    if (!isCardPresent()) {
        return StorageStatus::NOT_MOUNTED;
    }

    // Check if nearly full (>95%)
    uint64_t total = SD.totalBytes();
    uint64_t used = SD.usedBytes();
    if (used > (total * 0.95)) {
        return StorageStatus::FULL;
    }

    return StorageStatus::OK;
}

bool SDStorage::clear() {
    if (!_mounted) {
        return false;
    }

    DEBUG_STORAGE_PRINTLN("Clearing all SD card data");

    // Remove data file
    if (SD.exists(DATA_FILE)) {
        SD.remove(DATA_FILE);
    }

    // Reset metadata
    _metadata.lastUploadedMillis = 0;
    saveMetadata();

    // Recreate data file with header
    return ensureDataFileWithHeader();
}

bool SDStorage::format() {
    DEBUG_STORAGE_PRINTLN("SD card format not supported");
    DEBUG_STORAGE_PRINTLN("Please format SD card externally (FAT32)");
    return false;
}

bool SDStorage::flush() {
    // File is opened, written, flushed, and closed immediately in safeWrite()
    // No buffering, so nothing to flush
    return true;
}

String SDStorage::getCSVHeader() const {
    return "millis,timestamp_utc,latitude,longitude,altitude,gps_sats,gps_hdop,sensor_type,sensor_model,sensor_serial,sensor_instance,calibration_date,value,unit,quality";
}

String SDStorage::recordToCSV(const DataRecord& record) const {
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
    return csv;
}

unsigned long SDStorage::getLastUploadedMillis() const {
    return _metadata.lastUploadedMillis;
}

bool SDStorage::setLastUploadedMillis(unsigned long millis) {
    _metadata.lastUploadedMillis = millis;
    return saveMetadata();
}

// ============================================================================
// SD-Specific Methods
// ============================================================================

bool SDStorage::isCardPresent() const {
    if (!_mounted) {
        return false;
    }

    // Try to open root directory
    File root = SD.open("/");
    if (!root) {
        return false;
    }
    root.close();
    return true;
}

uint64_t SDStorage::getCardSize() const {
    if (!_mounted) {
        return 0;
    }
    return SD.cardSize();
}

String SDStorage::getCardType() const {
    if (!_mounted) {
        return "None";
    }

    uint8_t cardType = SD.cardType();
    switch (cardType) {
        case CARD_NONE:
            return "None";
        case CARD_MMC:
            return "MMC";
        case CARD_SD:
            return "SDSC";
        case CARD_SDHC:
            return "SDHC";
        default:
            return "Unknown";
    }
}

// ============================================================================
// Private Helper Methods
// ============================================================================

bool SDStorage::loadMetadata() {
    if (!SD.exists(METADATA_FILE)) {
        return false;
    }

    File file = SD.open(METADATA_FILE, FILE_READ);
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

    DEBUG_STORAGE_PRINTLN("Metadata loaded from SD card");
    return true;
}

bool SDStorage::saveMetadata() {
    if (!_mounted) {
        return false;
    }

    File file = SD.open(METADATA_FILE, FILE_WRITE);
    if (!file) {
        DEBUG_STORAGE_PRINTLN("Failed to open metadata file for writing");
        return false;
    }

    StaticJsonDocument<256> doc;
    doc["lastUploadedMillis"] = _metadata.lastUploadedMillis;

    serializeJson(doc, file);
    file.flush();
    file.close();

    DEBUG_STORAGE_PRINTLN("Metadata saved to SD card");
    return true;
}

uint32_t SDStorage::countRecords() const {
    if (!_mounted || !SD.exists(DATA_FILE)) {
        return 0;
    }

    File file = SD.open(DATA_FILE, FILE_READ);
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

bool SDStorage::parseCSVLine(const String& line, DataRecord& record) const {
    // Parse CSV: millis,timestamp_utc,latitude,longitude,altitude,gps_sats,gps_hdop,
    //            sensor_type,sensor_model,sensor_serial,sensor_instance,
    //            calibration_date,value,unit,quality

    int fieldIndex = 0;
    int lastComma = -1;
    int nextComma = 0;

    for (int i = 0; i <= line.length(); i++) {
        if (i == line.length() || line[i] == ',') {
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
            }

            fieldIndex++;
            lastComma = nextComma;
        }
    }

    // Support both old format (10 fields) and new format (15 fields) for backwards compatibility
    return (fieldIndex >= 10);
}

bool SDStorage::ensureDataFileWithHeader() {
    if (SD.exists(DATA_FILE)) {
        return true;  // File already exists
    }

    File file = SD.open(DATA_FILE, FILE_WRITE);
    if (!file) {
        return false;
    }

    file.println(getCSVHeader());
    file.flush();
    file.close();

    DEBUG_STORAGE_PRINTLN("Created data file with header on SD card");
    return true;
}

bool SDStorage::safeWrite(const String& data) {
    // CRITICAL: Power-loss safe write pattern
    // Open → Write → Flush → Close in single operation
    // NEVER keep file open between cycles

    File file = SD.open(DATA_FILE, FILE_APPEND);
    if (!file) {
        DEBUG_STORAGE_PRINTLN("Failed to open data file for writing");
        return false;
    }

    // Write data
    file.println(data);

    // Flush buffers to ensure data is written to SD card
    file.flush();

    // Close file immediately
    file.close();

    DEBUG_STORAGE_PRINT("Written to SD: ");
    DEBUG_STORAGE_PRINTLN(data);

    return true;
}
