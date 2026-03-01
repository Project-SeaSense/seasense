/**
 * SeaSense Logger - SD Card Storage Implementation
 */

#include "SDStorage.h"
#include "../../config/hardware_config.h"
#include "../system/SystemHealth.h"
#include <ArduinoJson.h>

// File paths
const char* SDStorage::DATA_FILE = "/data.csv";
const char* SDStorage::METADATA_FILE = "/metadata.json";

// ============================================================================
// Constructor / Destructor
// ============================================================================

SDStorage::SDStorage(uint8_t csPin)
    : _csPin(csPin),
      _mounted(false),
      _spi(HSPI)
{
    _metadata.lastUploadedMillis = 0;
    _metadata.recordsAtLastUpload = 0;
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
    Serial.println("[SD] Initializing SD card storage...");

    // Use dedicated HSPI bus (SPI3) to avoid conflicts with
    // octal PSRAM on ESP32-S3 N16R8 which shares internal SPI resources
    _spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, _csPin);
    Serial.printf("[SD] SPI bus: HSPI, SCK=%d MISO=%d MOSI=%d CS=%d @ %dHz\n",
                  SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, _csPin, SD_SPI_FREQUENCY);

    // Diagnostic: full SPI-mode SD card init sequence to pinpoint failure
    {
        pinMode(_csPin, OUTPUT);
        digitalWrite(_csPin, HIGH);
        _spi.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));

        // 80 clocks with CS high (SD spec requirement)
        for (int i = 0; i < 10; i++) _spi.transfer(0xFF);

        // Helper: send command, return R1 response
        auto sendCmd = [&](uint8_t cmd, uint32_t arg, uint8_t crc) -> uint8_t {
            digitalWrite(_csPin, LOW);
            _spi.transfer(0x40 | cmd);
            _spi.transfer((arg >> 24) & 0xFF);
            _spi.transfer((arg >> 16) & 0xFF);
            _spi.transfer((arg >> 8) & 0xFF);
            _spi.transfer(arg & 0xFF);
            _spi.transfer(crc);
            uint8_t r = 0xFF;
            for (int i = 0; i < 16 && r == 0xFF; i++) r = _spi.transfer(0xFF);
            return r;
        };
        auto deselect = [&]() {
            digitalWrite(_csPin, HIGH);
            _spi.transfer(0xFF);
        };

        // CMD0 — GO_IDLE_STATE
        uint8_t r1 = sendCmd(0, 0, 0x95);
        deselect();
        Serial.printf("[SD] CMD0: 0x%02X %s\n", r1, r1 == 0x01 ? "OK" : "FAIL");

        if (r1 == 0x01) {
            // CMD8 — SEND_IF_COND (required for SDHC)
            r1 = sendCmd(8, 0x000001AA, 0x87);
            uint8_t r7[4] = {};
            if (r1 <= 1) {
                for (int i = 0; i < 4; i++) r7[i] = _spi.transfer(0xFF);
            }
            deselect();
            Serial.printf("[SD] CMD8: 0x%02X [%02X %02X %02X %02X]\n", r1, r7[0], r7[1], r7[2], r7[3]);

            // ACMD41 — SD_SEND_OP_COND (with HCS bit for SDHC)
            uint8_t acmd41 = 0xFF;
            for (int tries = 0; tries < 100; tries++) {
                sendCmd(55, 0, 0x65);  // CMD55 (APP_CMD prefix)
                deselect();
                acmd41 = sendCmd(41, 0x40000000, 0x77);  // ACMD41 with HCS
                deselect();
                if (acmd41 == 0x00) break;  // Card ready
                delay(10);
            }
            Serial.printf("[SD] ACMD41: 0x%02X %s\n", acmd41, acmd41 == 0x00 ? "READY" : "FAIL/TIMEOUT");

            if (acmd41 == 0x00) {
                // CMD58 — READ_OCR
                r1 = sendCmd(58, 0, 0xFD);
                uint8_t ocr[4] = {};
                for (int i = 0; i < 4; i++) ocr[i] = _spi.transfer(0xFF);
                deselect();
                Serial.printf("[SD] CMD58: 0x%02X OCR=[%02X %02X %02X %02X] %s\n",
                    r1, ocr[0], ocr[1], ocr[2], ocr[3],
                    (ocr[0] & 0x40) ? "SDHC" : "SDSC");
            }
        }

        _spi.endTransaction();
    }

    // Re-init SPI bus for SD library
    _spi.end();
    _spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, _csPin);

    // Mount SD card — try multiple times, some cards need repeated init
    bool mounted = false;
    const uint32_t speeds[] = {SD_SPI_FREQUENCY, 1000000, 400000};
    for (int s = 0; s < 3 && !mounted; s++) {
        for (int attempt = 0; attempt < 2 && !mounted; attempt++) {
            Serial.printf("[SD] Mount attempt %d @ %luHz...\n", s * 2 + attempt + 1, speeds[s]);
            if (SD.begin(_csPin, _spi, speeds[s], "/sd", 5, false)) {
                mounted = true;
            } else {
                SD.end();
                delay(100);
            }
        }
        if (!mounted && s < 2) {
            _spi.end();
            _spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, _csPin);
        }
    }
    if (!mounted) {
        Serial.println("[SD] All mount attempts failed");
        Serial.printf("[SD] Card type after last attempt: %d\n", SD.cardType());
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
    uint16_t maxRecords,
    uint32_t skipRecords
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

    // Skip already-processed records (e.g. already-uploaded prefix)
    extern SystemHealth systemHealth;
    for (uint32_t i = 0; i < skipRecords && file.available(); i++) {
        file.readStringUntil('\n');
        if ((i & 99) == 99) {  // every 100 lines
            systemHealth.feedWatchdog();
        }
    }

    // Read records
    uint32_t parsed = 0;
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
        if ((++parsed & 49) == 49) {  // every 50 records
            systemHealth.feedWatchdog();
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
        stats.recordsSinceUpload = (stats.totalRecords > _metadata.recordsAtLastUpload)
            ? (stats.totalRecords - _metadata.recordsAtLastUpload)
            : stats.totalRecords;
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
    _metadata.recordsAtLastUpload = 0;
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
    return "millis,timestamp_utc,latitude,longitude,altitude,gps_sats,gps_hdop,"
           "sensor_type,sensor_model,sensor_serial,sensor_instance,calibration_date,"
           "value,unit,quality,"
           "wind_speed_true_ms,wind_angle_true_deg,wind_speed_app_ms,wind_angle_app_deg,"
           "water_depth_m,stw_ms,water_temp_ext_c,air_temp_c,baro_pressure_pa,"
           "humidity_pct,cog_deg,sog_ms,heading_deg,pitch_deg,roll_deg,"
           "wind_speed_corr_ms,wind_angle_corr_deg,"
           "lin_accel_x,lin_accel_y,lin_accel_z";
}

// Helper: format float for CSV, empty string if NaN
static String csvFloat(float v, int decimals) {
    if (isnan(v)) return "";
    return String(v, decimals);
}

String SDStorage::recordToCSV(const DataRecord& record) const {
    String csv = "";
    csv += String(record.millis);
    csv += ",";
    csv += record.timestampUTC.length() > 0 ? record.timestampUTC : "";
    csv += ",";
    csv += isnan(record.latitude)  ? "" : String(record.latitude,  6);
    csv += ",";
    csv += isnan(record.longitude) ? "" : String(record.longitude, 6);
    csv += ",";
    csv += isnan(record.altitude)  ? "" : String(record.altitude,  1);
    csv += ",";
    csv += String(record.gps_satellites);
    csv += ",";
    csv += isnan(record.gps_hdop)  ? "" : String(record.gps_hdop,  1);
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
    csv += "," + csvFloat(record.windSpeedCorrected, 2);
    csv += "," + csvFloat(record.windAngleCorrected, 1);
    csv += "," + csvFloat(record.linAccelX, 3);
    csv += "," + csvFloat(record.linAccelY, 3);
    csv += "," + csvFloat(record.linAccelZ, 3);
    return csv;
}

unsigned long SDStorage::getLastUploadedMillis() const {
    return _metadata.lastUploadedMillis;
}

bool SDStorage::setLastUploadedMillis(unsigned long millis) {
    _metadata.lastUploadedMillis = millis;
    _metadata.recordsAtLastUpload = countRecords();
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

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        DEBUG_STORAGE_PRINT("Metadata parse error: ");
        DEBUG_STORAGE_PRINTLN(error.c_str());
        return false;
    }

    _metadata.lastUploadedMillis = doc["lastUploadedMillis"] | 0UL;
    _metadata.recordsAtLastUpload = doc["recordsAtLastUpload"] | 0U;

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

    JsonDocument doc;
    doc["lastUploadedMillis"] = _metadata.lastUploadedMillis;
    doc["recordsAtLastUpload"] = _metadata.recordsAtLastUpload;

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

// Helper: parse a CSV field as float, returning NaN for empty fields
static float parseOptionalFloat(const String& field) {
    if (field.length() == 0) return NAN;
    return field.toFloat();
}

bool SDStorage::parseCSVLine(const String& line, DataRecord& record) const {
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
    record.windSpeedCorrected = NAN;
    record.windAngleCorrected = NAN;
    record.linAccelX = NAN;
    record.linAccelY = NAN;
    record.linAccelZ = NAN;

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
                case 2: record.latitude  = field.length() == 0 ? NAN : field.toDouble(); break;
                case 3: record.longitude = field.length() == 0 ? NAN : field.toDouble(); break;
                case 4: record.altitude  = field.length() == 0 ? NAN : field.toDouble(); break;
                case 5: record.gps_satellites = field.toInt(); break;
                case 6: record.gps_hdop  = field.length() == 0 ? NAN : field.toDouble(); break;
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
                case 30: record.windSpeedCorrected = parseOptionalFloat(field); break;
                case 31: record.windAngleCorrected = parseOptionalFloat(field); break;
                case 32: record.linAccelX = parseOptionalFloat(field); break;
                case 33: record.linAccelY = parseOptionalFloat(field); break;
                case 34: record.linAccelZ = parseOptionalFloat(field); break;
            }

            fieldIndex++;
            lastComma = nextComma;
        }
    }

    // Support old format (15 fields) and new format (30-35 fields)
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
