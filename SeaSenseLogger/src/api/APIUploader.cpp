/**
 * SeaSense Logger - API Uploader Implementation
 */

#include "APIUploader.h"
#include "../system/SystemHealth.h"
#include "../config/ConfigManager.h"
#include "../../config/hardware_config.h"
#include "../../config/secrets.h"
#include <ArduinoJson.h>

// Retry backoff intervals (milliseconds)
const unsigned long RETRY_INTERVALS[] = {
    60000,      // 1 minute
    120000,     // 2 minutes
    300000,     // 5 minutes
    600000,     // 10 minutes
    1800000     // 30 minutes
};
const uint8_t MAX_RETRY_INTERVALS = 5;

// ============================================================================
// Constructor
// ============================================================================

APIUploader::APIUploader(StorageManager* storage)
    : _storage(storage),
      _status(UploadStatus::IDLE),
      _lastUploadTime(0),
      _nextUploadTime(0),
      _retryCount(0),
      _timeSynced(false),
      _bootTimeEpoch(0)
{
}

// ============================================================================
// Public Methods
// ============================================================================

bool APIUploader::begin(const UploadConfig& config) {
    _config = config;

    if (!_config.enabled) {
        Serial.println("[API] Upload disabled in configuration");
        return true;
    }

    Serial.println("[API] Initializing API uploader...");
    Serial.print("[API] Endpoint: ");
    Serial.println(_config.apiUrl);
    Serial.print("[API] Interval: ");
    Serial.print(_config.intervalMs / 1000);
    Serial.println(" seconds");
    Serial.print("[API] Batch size: ");
    Serial.println(_config.batchSize);

    // Initial NTP sync attempt
    if (isWiFiConnected()) {
        if (syncNTP()) {
            Serial.println("[API] NTP time synchronized");
        } else {
            Serial.println("[API] Warning: NTP sync failed, will retry");
        }
    } else {
        Serial.println("[API] No WiFi connection, NTP sync skipped");
    }

    // Schedule first upload
    _nextUploadTime = millis() + _config.intervalMs;

    return true;
}

void APIUploader::process() {
    if (!_config.enabled) {
        return;
    }

    unsigned long now = millis();

    // Check if it's time for next upload
    if (now < _nextUploadTime) {
        return;
    }

    DEBUG_API_PRINTLN("Processing upload cycle...");

    // Check WiFi connection
    if (!isWiFiConnected()) {
        _status = UploadStatus::ERROR_NO_WIFI;
        DEBUG_API_PRINTLN("No WiFi connection, skipping upload");
        scheduleRetry();
        return;
    }

    // Sync NTP if not already synced
    if (!_timeSynced) {
        _status = UploadStatus::SYNCING_TIME;
        if (!syncNTP()) {
            _status = UploadStatus::ERROR_NO_TIME;
            Serial.println("[API] NTP sync failed, cannot upload without timestamps");
            scheduleRetry();
            return;
        }
    }

    // Query data from storage
    _status = UploadStatus::QUERYING_DATA;
    unsigned long lastUploaded = _storage->getLastUploadedMillis();
    std::vector<DataRecord> records = _storage->readRecords(lastUploaded, _config.batchSize);

    if (records.empty()) {
        _status = UploadStatus::ERROR_NO_DATA;
        DEBUG_API_PRINTLN("No new data to upload");
        _nextUploadTime = now + _config.intervalMs;
        return;
    }

    Serial.print("[API] Uploading ");
    Serial.print(records.size());
    Serial.println(" records...");

    // Build payload
    String payload = buildPayload(records);

    // Upload to API
    _status = UploadStatus::UPLOADING;
    if (uploadPayload(payload)) {
        _status = UploadStatus::SUCCESS;
        _lastUploadTime = now;

        // Update last uploaded timestamp
        unsigned long lastRecordMillis = records[records.size() - 1].millis;
        _storage->setLastUploadedMillis(lastRecordMillis);

        Serial.print("[API] Upload successful! ");
        Serial.print(records.size());
        Serial.println(" records uploaded");

        // Reset retry and schedule next upload
        resetRetry();
        _nextUploadTime = now + _config.intervalMs;
    } else {
        _status = UploadStatus::ERROR_API;
        Serial.println("[API] Upload failed");
        extern SystemHealth systemHealth;
        systemHealth.recordError(ErrorType::API);
        scheduleRetry();
    }
}

String APIUploader::getStatusString() const {
    switch (_status) {
        case UploadStatus::IDLE:            return "Idle";
        case UploadStatus::SYNCING_TIME:    return "Syncing time";
        case UploadStatus::QUERYING_DATA:   return "Querying data";
        case UploadStatus::UPLOADING:       return "Uploading";
        case UploadStatus::SUCCESS:         return "Success";
        case UploadStatus::ERROR_NO_WIFI:   return "No WiFi";
        case UploadStatus::ERROR_NO_TIME:   return "No time sync";
        case UploadStatus::ERROR_NO_DATA:   return "No data";
        case UploadStatus::ERROR_API:       return "API error";
        default:                            return "Unknown";
    }
}

uint32_t APIUploader::getPendingRecords() const {
    if (!_storage) return 0;

    StorageStats stats = _storage->getStats();

    // If we've never uploaded, everything is pending
    if (_lastUploadTime == 0) return stats.totalRecords;

    // Estimate: records written since last upload based on recordsSinceUpload if available
    return stats.recordsSinceUpload > 0 ? stats.recordsSinceUpload : stats.totalRecords;
}

unsigned long APIUploader::getTimeUntilNext() const {
    unsigned long now = millis();
    if (now >= _nextUploadTime) {
        return 0;
    }
    return _nextUploadTime - now;
}

void APIUploader::forceUpload() {
    _nextUploadTime = millis();
    Serial.println("[API] Forced upload scheduled");
}

// ============================================================================
// Private Methods
// ============================================================================

bool APIUploader::isWiFiConnected() const {
    return (WiFi.status() == WL_CONNECTED);
}

bool APIUploader::syncNTP() {
    DEBUG_API_PRINTLN("Syncing NTP...");

    configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    // Non-blocking wait for time sync (max 5 seconds)
    extern SystemHealth systemHealth;
    unsigned long deadline = millis() + 5000;
    while (millis() < deadline) {
        time_t now = time(nullptr);
        if (now > 1000000000) {  // Valid timestamp
            _bootTimeEpoch = now - (millis() / 1000);
            _timeSynced = true;

            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            DEBUG_API_PRINT("NTP synced: ");
            DEBUG_API_PRINTLN(asctime(&timeinfo));

            return true;
        }
        systemHealth.feedWatchdog();
        delay(100);
    }

    return false;
}

String APIUploader::millisToUTC(unsigned long millisTimestamp) const {
    if (!_timeSynced) {
        return "";
    }

    // Calculate absolute epoch time
    time_t epoch = _bootTimeEpoch + (millisTimestamp / 1000);

    // Convert to ISO 8601 format
    struct tm timeinfo;
    gmtime_r(&epoch, &timeinfo);

    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

    return String(buffer);
}

String APIUploader::buildPayload(const std::vector<DataRecord>& records) const {
    DynamicJsonDocument doc(16384);  // Large buffer for batch uploads (env fields add ~300 bytes/record)

    // Metadata
    JsonObject metadata = doc.createNestedObject("metadata");
    metadata["schema_version"] = "1.0";
    metadata["partner_id"] = _config.partnerID;
    metadata["device_guid"] = _config.deviceGUID;

    JsonObject collector = metadata.createNestedObject("collector");
    collector["device"] = "SeaSense ESP32 Logger";
    collector["firmware_version"] = "1.0.0";
    collector["export_generated_at_utc"] = millisToUTC(millis());

    // Device health telemetry (piggybacks on every upload)
    extern SystemHealth systemHealth;
    JsonObject health = metadata.createNestedObject("device_health");
    health["uptime_ms"] = millis();
    health["free_heap"] = ESP.getFreeHeap();
    health["reset_reason"] = systemHealth.getResetReasonString();
    health["reboot_count"] = systemHealth.getRebootCount();
    health["consecutive_reboots"] = systemHealth.getConsecutiveReboots();
    health["safe_mode"] = systemHealth.isInSafeMode();
    health["sensor_errors"] = systemHealth.getErrorCount(ErrorType::SENSOR);
    health["sd_errors"] = systemHealth.getErrorCount(ErrorType::SD);
    health["api_errors"] = systemHealth.getErrorCount(ErrorType::API);
    health["wifi_errors"] = systemHealth.getErrorCount(ErrorType::WIFI);

    // Deployment metadata
    extern ConfigManager configManager;
    ConfigManager::DeploymentConfig dep = configManager.getDeploymentConfig();
    if (dep.deployDate.length() > 0) {
        metadata["deploy_date"] = dep.deployDate;
    }
    if (dep.purchaseDate.length() > 0) {
        metadata["purchase_date"] = dep.purchaseDate;
    }
    if (dep.depthCm > 0) {
        metadata["depth_cm"] = dep.depthCm;
    }

    // Datapoints
    JsonArray datapoints = doc.createNestedArray("datapoints");

    for (const DataRecord& record : records) {
        JsonObject dp = datapoints.createNestedObject();

        // Timestamp (from GPS or NTP)
        String utcTime = record.timestampUTC.length() > 0 ? record.timestampUTC : millisToUTC(record.millis);
        dp["timestamp_utc"] = utcTime;

        // GPS location data (if available)
        if (record.latitude != 0.0 || record.longitude != 0.0) {
            dp["latitude"] = record.latitude;
            dp["longitude"] = record.longitude;
            dp["altitude"] = record.altitude;
            dp["hdop"] = record.gps_hdop;
        }

        // NMEA2000 device identification
        dp["manufacturer_code"] = NMEA2000_MANUFACTURER_CODE;
        dp["device_function"] = NMEA2000_DEVICE_FUNCTION;
        dp["device_class"] = NMEA2000_DEVICE_CLASS;
        dp["industry_group"] = NMEA2000_INDUSTRY_GROUP;

        // Sensor data - map sensor types to API field names
        if (record.sensorType == "Temperature") {
            dp["water_temperature_c"] = record.value;
        } else if (record.sensorType == "Conductivity") {
            dp["water_conductivity_us_cm"] = record.value;
        }

        // Metadata fields (forward compatibility)
        dp["sensor_model"] = record.sensorModel;
        dp["sensor_serial"] = record.sensorSerial;
        dp["sensor_instance"] = record.sensorInstance;
        dp["calibration_date"] = record.calibrationDate;

        // NMEA2000 environmental context (only include non-NaN fields)
        if (!isnan(record.windSpeedTrue))     dp["wind_speed_true_ms"] = record.windSpeedTrue;
        if (!isnan(record.windAngleTrue))     dp["wind_angle_true_deg"] = record.windAngleTrue;
        if (!isnan(record.windSpeedApparent)) dp["wind_speed_app_ms"] = record.windSpeedApparent;
        if (!isnan(record.windAngleApparent)) dp["wind_angle_app_deg"] = record.windAngleApparent;
        if (!isnan(record.waterDepth))        dp["water_depth_m"] = record.waterDepth;
        if (!isnan(record.speedThroughWater)) dp["speed_through_water_ms"] = record.speedThroughWater;
        if (!isnan(record.waterTempExternal)) dp["water_temp_external_c"] = record.waterTempExternal;
        if (!isnan(record.airTemp))           dp["air_temp_c"] = record.airTemp;
        if (!isnan(record.baroPressure))      dp["baro_pressure_pa"] = record.baroPressure;
        if (!isnan(record.humidity))          dp["humidity_pct"] = record.humidity;
        if (!isnan(record.cogTrue))           dp["cog_true_deg"] = record.cogTrue;
        if (!isnan(record.sog))              dp["sog_ms"] = record.sog;
        if (!isnan(record.heading))           dp["heading_true_deg"] = record.heading;
        if (!isnan(record.pitch))             dp["pitch_deg"] = record.pitch;
        if (!isnan(record.roll))              dp["roll_deg"] = record.roll;
    }

    String payload;
    serializeJson(doc, payload);
    return payload;
}

bool APIUploader::uploadPayload(const String& payload) {
    HTTPClient http;

    // Configure HTTP client
    http.begin(_config.apiUrl + "/v1/ingest/datapoints");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-Key", _config.apiKey);
    http.setConnectTimeout(API_CONNECT_TIMEOUT_MS);  // Fast DNS/connect failure
    http.setTimeout(10000);  // 10 second response timeout

    DEBUG_API_PRINT("Payload size: ");
    DEBUG_API_PRINT(payload.length());
    DEBUG_API_PRINTLN(" bytes");

    // Send POST request
    int httpCode = http.POST(payload);

    DEBUG_API_PRINT("HTTP response: ");
    DEBUG_API_PRINTLN(httpCode);

    bool success = false;

    if (httpCode == 201) {  // API returns 201 on success
        String response = http.getString();
        DEBUG_API_PRINT("Response: ");
        DEBUG_API_PRINTLN(response);
        success = true;
    } else if (httpCode > 0) {
        String response = http.getString();
        Serial.print("[API] Error response: ");
        Serial.println(response);
    } else {
        Serial.print("[API] HTTP error: ");
        Serial.println(http.errorToString(httpCode));
    }

    http.end();
    return success;
}

void APIUploader::scheduleRetry() {
    // Gentle retry with exponential backoff
    uint8_t intervalIndex = min(_retryCount, (uint8_t)(MAX_RETRY_INTERVALS - 1));
    unsigned long retryDelay = RETRY_INTERVALS[intervalIndex];

    _nextUploadTime = millis() + retryDelay;
    _retryCount++;

    DEBUG_API_PRINT("Retry scheduled in ");
    DEBUG_API_PRINT(retryDelay / 1000);
    DEBUG_API_PRINT(" seconds (attempt ");
    DEBUG_API_PRINT(_retryCount);
    DEBUG_API_PRINTLN(")");
}

void APIUploader::resetRetry() {
    _retryCount = 0;
}
