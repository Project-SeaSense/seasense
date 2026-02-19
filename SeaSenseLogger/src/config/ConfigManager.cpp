/**
 * SeaSense Logger - Configuration Manager Implementation
 */

#include "ConfigManager.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <esp_random.h>
#include "../../config/hardware_config.h"
#include "../../config/secrets.h"

// Static const definition
const char* ConfigManager::CONFIG_FILE = "/settings.json";

// ============================================================================
// Constructor
// ============================================================================

ConfigManager::ConfigManager() {
    // Constructor empty - initialization happens in begin()
}

// ============================================================================
// Public Methods
// ============================================================================

bool ConfigManager::begin() {
    Serial.println("[CONFIG] Initializing configuration manager...");

    // Set defaults from compile-time defines
    setDefaults();

    // Try to mount SPIFFS. On first boot after a clean flash or partition layout
    // change, format() is required. Formatting 12MB can take >20s, so temporarily
    // unsubscribe from the watchdog to avoid a spurious WDT reset.
    if (!SPIFFS.begin(false)) {
        Serial.println("[CONFIG] SPIFFS mount failed, formatting (may take ~20s)...");
        esp_task_wdt_delete(NULL);  // suspend this task from WDT during format
        bool ok = SPIFFS.format() && SPIFFS.begin(false);
        esp_task_wdt_add(NULL);     // re-subscribe
        if (!ok) {
            Serial.println("[CONFIG ERROR] Failed to mount SPIFFS after format");
            return false;
        }
        Serial.println("[CONFIG] SPIFFS formatted successfully");
    }

    // Load configuration file if it exists
    if (SPIFFS.exists(CONFIG_FILE)) {
        Serial.println("[CONFIG] Loading configuration from SPIFFS...");
        if (loadFromFile()) {
            Serial.println("[CONFIG] Configuration loaded successfully");
            if (ensureDeviceGUID()) saveToFile();  // persist if GUID was just generated
            return true;
        } else {
            Serial.println("[CONFIG WARNING] Failed to load config, using defaults");
            return false;
        }
    } else {
        Serial.println("[CONFIG] No config file found, using defaults");
        Serial.println("[CONFIG] Creating default configuration file...");
        ensureDeviceGUID();  // generate GUID on first boot
        saveToFile();
        return true;
    }
}

bool ConfigManager::save() {
    return saveToFile();
}

bool ConfigManager::reset() {
    Serial.println("[CONFIG] Resetting to defaults...");
    setDefaults();
    return saveToFile();
}

ConfigManager::WiFiConfig ConfigManager::getWiFiConfig() const {
    return _wifi;
}

void ConfigManager::setWiFiConfig(const WiFiConfig& config) {
    _wifi = config;
}

ConfigManager::APIConfig ConfigManager::getAPIConfig() const {
    return _api;
}

void ConfigManager::setAPIConfig(const APIConfig& config) {
    _api = config;
    clampConfig();
}

ConfigManager::DeviceConfig ConfigManager::getDeviceConfig() const {
    return _device;
}

void ConfigManager::setDeviceConfig(const DeviceConfig& config) {
    _device = config;
}

PumpConfig ConfigManager::getPumpConfig() const {
    return _pump;
}

void ConfigManager::setPumpConfig(const PumpConfig& config) {
    _pump = config;
    clampConfig();
}

ConfigManager::SamplingConfig ConfigManager::getSamplingConfig() const {
    return _sampling;
}

void ConfigManager::setSamplingConfig(const SamplingConfig& config) {
    _sampling = config;
    clampConfig();
}

ConfigManager::GPSConfig ConfigManager::getGPSConfig() const {
    return _gps;
}

void ConfigManager::setGPSConfig(const GPSConfig& config) {
    _gps = config;
}

ConfigManager::DeploymentConfig ConfigManager::getDeploymentConfig() const {
    return _deployment;
}

void ConfigManager::setDeploymentConfig(const DeploymentConfig& config) {
    _deployment = config;
}

bool ConfigManager::stampDeployDate(const String& utcTimestamp) {
    if (_deployment.deployDate.length() > 0) {
        return false;  // Already stamped
    }
    _deployment.deployDate = utcTimestamp;
    Serial.print("[CONFIG] Deploy date stamped: ");
    Serial.println(utcTimestamp);
    saveToFile();
    return true;
}

// ============================================================================
// Private Methods
// ============================================================================

bool ConfigManager::loadFromFile() {
    File file = SPIFFS.open(CONFIG_FILE, "r");
    if (!file) {
        Serial.println("[CONFIG ERROR] Failed to open config file");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.print("[CONFIG ERROR] Failed to parse config: ");
        Serial.println(error.c_str());
        return false;
    }

    // Load WiFi config
    if (doc["wifi"].is<JsonObject>()) {
        JsonObject wifi = doc["wifi"];
        _wifi.stationSSID = wifi["station_ssid"] | "";
        _wifi.stationPassword = wifi["station_password"] | "";
        _wifi.apPassword = wifi["ap_password"] | WIFI_AP_PASSWORD;
    }

    // Load API config
    if (doc["api"].is<JsonObject>()) {
        JsonObject api = doc["api"];
        _api.url = api["url"] | "";
        _api.apiKey = api["api_key"] | "";
        _api.uploadInterval = api["upload_interval_ms"] | 300000;
        _api.batchSize = api["batch_size"] | 100;
        _api.maxRetries = api["max_retries"] | 5;
    }

    // Load device config
    if (doc["device"].is<JsonObject>()) {
        JsonObject device = doc["device"];
        _device.deviceGUID = device["device_guid"] | "";
        _device.partnerID = device["partner_id"] | PARTNER_ID_DEFAULT;
        // Always use compiled-in version (reflects what's actually running)
        _device.firmwareVersion = FIRMWARE_VERSION;
    }

    // Load pump config
    if (doc["pump"].is<JsonObject>()) {
        JsonObject pump = doc["pump"];
        _pump.enabled = pump["enabled"] | true;
        _pump.relayPin = pump["relay_pin"] | PUMP_RELAY_PIN;
        _pump.cycleIntervalMs = pump["cycle_interval_ms"] | PUMP_CYCLE_INTERVAL_MS;
        _pump.maxPumpOnTimeMs = pump["max_on_time_ms"] | PUMP_MAX_ON_TIME_MS;

        // New fields — with backwards compat for old settings.json
        _pump.flushDurationMs = pump["flush_duration_ms"] |
            (uint16_t)((pump["startup_delay_ms"] | (uint16_t)0) + (pump["stability_wait_ms"] | (uint16_t)0));
        if (_pump.flushDurationMs == 0) _pump.flushDurationMs = PUMP_FLUSH_DURATION_MS;

        _pump.measureDurationMs = pump["measure_duration_ms"] |
            (uint16_t)(pump["measurement_interval_ms"] | (uint16_t)0);
        if (_pump.measureDurationMs == 0) _pump.measureDurationMs = PUMP_MEASURE_DURATION_MS;
    }

    // Load sampling config
    if (doc["sampling"].is<JsonObject>()) {
        JsonObject sampling = doc["sampling"];
        _sampling.sensorIntervalMs = sampling["sensor_interval_ms"] | 900000;
    }

    // Load GPS config
    if (doc["gps"].is<JsonObject>()) {
        JsonObject gps = doc["gps"];
        _gps.useNMEA2000 = gps["use_nmea2000"] | false;
        _gps.fallbackToOnboard = gps["fallback_to_onboard"] | true;
    }

    // Load deployment metadata
    if (doc["deployment"].is<JsonObject>()) {
        JsonObject dep = doc["deployment"];
        _deployment.deployDate = dep["deploy_date"] | "";
        _deployment.purchaseDate = dep["purchase_date"] | "";
        _deployment.depthCm = dep["depth_cm"] | 0.0f;
    }

    clampConfig();
    return true;
}

bool ConfigManager::saveToFile() {
    Serial.println("[CONFIG] Saving configuration to SPIFFS...");

    JsonDocument doc;

    // WiFi section
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["station_ssid"] = _wifi.stationSSID;
    wifi["station_password"] = _wifi.stationPassword;
    wifi["ap_password"] = _wifi.apPassword;

    // API section
    JsonObject api = doc["api"].to<JsonObject>();
    api["url"] = _api.url;
    api["api_key"] = _api.apiKey;
    api["upload_interval_ms"] = _api.uploadInterval;
    api["batch_size"] = _api.batchSize;
    api["max_retries"] = _api.maxRetries;

    // Device section
    JsonObject device = doc["device"].to<JsonObject>();
    device["device_guid"] = _device.deviceGUID;
    device["partner_id"] = _device.partnerID;
    device["firmware_version"] = _device.firmwareVersion;

    // Pump section
    JsonObject pump = doc["pump"].to<JsonObject>();
    pump["enabled"] = _pump.enabled;
    pump["relay_pin"] = _pump.relayPin;
    pump["flush_duration_ms"] = _pump.flushDurationMs;
    pump["measure_duration_ms"] = _pump.measureDurationMs;
    pump["cycle_interval_ms"] = _pump.cycleIntervalMs;
    pump["max_on_time_ms"] = _pump.maxPumpOnTimeMs;

    // Sampling section
    JsonObject sampling = doc["sampling"].to<JsonObject>();
    sampling["sensor_interval_ms"] = _sampling.sensorIntervalMs;

    // GPS section
    JsonObject gps = doc["gps"].to<JsonObject>();
    gps["use_nmea2000"] = _gps.useNMEA2000;
    gps["fallback_to_onboard"] = _gps.fallbackToOnboard;

    // Deployment metadata
    JsonObject dep = doc["deployment"].to<JsonObject>();
    dep["deploy_date"] = _deployment.deployDate;
    dep["purchase_date"] = _deployment.purchaseDate;
    dep["depth_cm"] = _deployment.depthCm;

    // Write to file
    File file = SPIFFS.open(CONFIG_FILE, "w");
    if (!file) {
        Serial.println("[CONFIG ERROR] Failed to create config file");
        return false;
    }

    if (serializeJson(doc, file) == 0) {
        Serial.println("[CONFIG ERROR] Failed to write config");
        file.close();
        return false;
    }

    file.close();
    Serial.println("[CONFIG] Configuration saved successfully");
    return true;
}

void ConfigManager::setDefaults() {
    Serial.println("[CONFIG] Setting default values...");

    // WiFi defaults from secrets.h
    #ifdef WIFI_STATION_SSID
        _wifi.stationSSID = WIFI_STATION_SSID;
    #else
        _wifi.stationSSID = "";
    #endif

    #ifdef WIFI_STATION_PASSWORD
        _wifi.stationPassword = WIFI_STATION_PASSWORD;
    #else
        _wifi.stationPassword = "";
    #endif

    #ifdef WIFI_AP_PASSWORD
        _wifi.apPassword = WIFI_AP_PASSWORD;
    #else
        _wifi.apPassword = "protectplanet!";
    #endif

    // API defaults from secrets.h
    #ifdef API_URL
        _api.url = API_URL;
    #else
        _api.url = "https://test-api.projectseasense.org";
    #endif

    #ifdef API_KEY
        _api.apiKey = API_KEY;
    #else
        _api.apiKey = "";
    #endif

    _api.uploadInterval = 300000;  // 5 minutes
    _api.batchSize = 100;
    _api.maxRetries = 5;

    // Device defaults
    _device.deviceGUID = "seasense-esp32";  // Will be overridden by device_config.h
    _device.partnerID = PARTNER_ID_DEFAULT;
    _device.firmwareVersion = FIRMWARE_VERSION;

    // Pump defaults from hardware_config.h
    _pump.flushDurationMs = PUMP_FLUSH_DURATION_MS;
    _pump.measureDurationMs = PUMP_MEASURE_DURATION_MS;
    _pump.cycleIntervalMs = PUMP_CYCLE_INTERVAL_MS;
    _pump.maxPumpOnTimeMs = PUMP_MAX_ON_TIME_MS;
    _pump.relayPin = PUMP_RELAY_PIN;
    _pump.enabled = true;

    // Sampling defaults
    _sampling.sensorIntervalMs = 900000;  // 15 minutes

    // GPS defaults - use onboard GPS, fall back if NMEA2000 has no fix
    _gps.useNMEA2000 = false;
    _gps.fallbackToOnboard = true;

    // Deployment defaults - empty until set
    _deployment.deployDate = "";
    _deployment.purchaseDate = "";
    _deployment.depthCm = 0.0f;
}

String ConfigManager::generateDeviceGUID() {
    uint8_t uuid[16];
    for (int i = 0; i < 4; i++) {
        uint32_t r = esp_random();
        uuid[i*4+0] = r & 0xFF;
        uuid[i*4+1] = (r >> 8) & 0xFF;
        uuid[i*4+2] = (r >> 16) & 0xFF;
        uuid[i*4+3] = (r >> 24) & 0xFF;
    }
    uuid[6] = (uuid[6] & 0x0F) | 0x40;  // version 4
    uuid[8] = (uuid[8] & 0x3F) | 0x80;  // variant 10xx

    char buf[47];  // "seasense-" (9) + UUID (36) + '\0'
    snprintf(buf, sizeof(buf),
        "seasense-%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0], uuid[1], uuid[2], uuid[3],
        uuid[4], uuid[5],
        uuid[6], uuid[7],
        uuid[8], uuid[9],
        uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
    return String(buf);
}

bool ConfigManager::ensureDeviceGUID() {
    bool isPlaceholder = _device.deviceGUID.isEmpty()
        || _device.deviceGUID == "seasense-esp32"
        || _device.deviceGUID == "seasense-v2-001"
        || _device.deviceGUID.length() < 20;
    if (!isPlaceholder) return false;

    _device.deviceGUID = generateDeviceGUID();
    Serial.print("[CONFIG] Generated device GUID: ");
    Serial.println(_device.deviceGUID);
    return true;
}

String ConfigManager::regenerateDeviceGUID() {
    _device.deviceGUID = generateDeviceGUID();
    saveToFile();
    Serial.print("[CONFIG] Regenerated device GUID: ");
    Serial.println(_device.deviceGUID);
    return _device.deviceGUID;
}

void ConfigManager::clampConfig() {
    // Sampling bounds: 5 seconds to 24 hours
    _sampling.sensorIntervalMs = constrain(_sampling.sensorIntervalMs, (uint32_t)5000, (uint32_t)86400000);

    // API upload bounds: 1 minute to 24 hours
    _api.uploadInterval = constrain(_api.uploadInterval, (uint32_t)60000, (uint32_t)86400000);
    _api.batchSize = constrain(_api.batchSize, (uint8_t)1, (uint8_t)255);
    _api.maxRetries = constrain(_api.maxRetries, (uint8_t)1, (uint8_t)20);

    // Pump bounds
    _pump.flushDurationMs = constrain(_pump.flushDurationMs, (uint16_t)1000, (uint16_t)30000);
    _pump.measureDurationMs = constrain(_pump.measureDurationMs, (uint16_t)1000, (uint16_t)30000);
    _pump.cycleIntervalMs = constrain(_pump.cycleIntervalMs, (uint32_t)10000, (uint32_t)3600000);
    _pump.maxPumpOnTimeMs = constrain(_pump.maxPumpOnTimeMs, (uint32_t)5000, (uint32_t)120000);
}
