/**
 * SeaSense Logger - Main Entry Point
 *
 * ESP32-based water quality logger for Atlas Scientific EZO sensors
 * Logs temperature and conductivity with full sensor metadata traceability
 *
 * Hardware:
 * - ESP32 DevKit
 * - Atlas Scientific EZO-RTD (temperature)
 * - Atlas Scientific EZO-EC (conductivity)
 * - SD card module
 *
 * Features:
 * - Dual storage (SPIFFS circular buffer + SD card permanent)
 * - Web UI for configuration and calibration
 * - Bandwidth-conscious API upload
 * - NMEA2000 PGN output
 * - Serial command interface
 */

#include <Wire.h>
#include <ArduinoJson.h>
#include <time.h>

// Configuration
#include "config/hardware_config.h"
#include "config/device_config.h"
#include "config/secrets.h"

// Sensors
#include "src/sensors/SensorInterface.h"
#include "src/sensors/EZOSensor.h"
#include "src/sensors/EZO_RTD.h"
#include "src/sensors/EZO_EC.h"
#include "src/sensors/EZO_pH.h"
#include "src/sensors/EZO_DO.h"
#include "src/sensors/GPSModule.h"
#include "src/sensors/NMEA2000GPS.h"
#include "src/sensors/NMEA2000Environment.h"

// Storage
#include "src/storage/StorageInterface.h"
#include "src/storage/SPIFFSStorage.h"
#include "src/storage/SDStorage.h"
#include "src/storage/StorageManager.h"

// Web UI
#include "src/webui/WebServer.h"

// Calibration
#include "src/calibration/CalibrationManager.h"

// API Upload
#include "src/api/APIUploader.h"

// Pump Controller
#include "src/pump/PumpController.h"

// Serial Commands
#include "src/commands/SerialCommands.h"

// Configuration Manager
#include "src/config/ConfigManager.h"

// System Health (watchdog, boot loop protection, error tracking)
#include "src/system/SystemHealth.h"

// ============================================================================
// Global Variables
// ============================================================================

// Sensors
EZO_RTD tempSensor;
EZO_EC ecSensor;
EZO_pH phSensor;
EZO_DO doSensor;
GPSModule gps(GPS_RX_PIN, GPS_TX_PIN);
NMEA2000GPS n2kGPS;
NMEA2000Environment n2kEnv;

// Storage
StorageManager storage(SPIFFS_CIRCULAR_BUFFER_SIZE, SD_CS_PIN);

// Configuration
ConfigManager configManager;

// Calibration
CalibrationManager calibration(&tempSensor, &ecSensor, &phSensor, &doSensor);

// Pump controller
PumpController pumpController(&tempSensor, &ecSensor);

// Web server
SeaSenseWebServer webServer(&tempSensor, &ecSensor, &storage, &calibration, &pumpController, &configManager, &phSensor, &doSensor);

// API Uploader
APIUploader apiUploader(&storage);

// Serial Commands
SerialCommands serialCommands(&tempSensor, &ecSensor, &gps, &storage, &apiUploader, &webServer, &pumpController);

// System Health
SystemHealth systemHealth;

// Device configuration
JsonDocument deviceConfigDoc;
bool configLoaded = false;

// Runtime sampling interval + movement gating (from ConfigManager)
unsigned long sensorSamplingIntervalMs = 900000;  // Default 15 minutes
bool skipMeasurementIfStationary = false;
float stationaryDeltaMeters = 100.0f;
bool nmeaOutputEnabled = false;  // outbound PGN emission gate (default off)

// Last persisted measurement position (for movement gating)
bool hasLastMeasurementGPS = false;
float lastMeasurementLat = NAN;
float lastMeasurementLon = NAN;

// Timestamp of last sensor read (elapsed-time pattern, rollover-safe)
unsigned long lastSensorReadAt = 0;

// Spinlock for shared timing globals accessed from both cores
portMUX_TYPE g_timerMux = portMUX_INITIALIZER_UNLOCKED;

// Mutex for I2C bus access (web sensor reads vs loop reads)
SemaphoreHandle_t g_i2cMutex = NULL;

// System epoch for calibration age checks (updated from GPS when fix available)
time_t g_systemEpoch = 0;

// Breadcrumbs for runtime diagnostics (/api/status)
volatile unsigned long g_lastLoopStartMs = 0;
volatile unsigned long g_maxLoopGapMs = 0;
const char* g_loopStage = "boot";

// ============================================================================
// Device Configuration Functions
// ============================================================================

bool parseDeviceConfig() {
    // First load compile-time defaults
    DeserializationError error = deserializeJson(deviceConfigDoc, DEVICE_CONFIG_JSON);
    if (error) {
        Serial.print("Failed to parse device config: ");
        Serial.println(error.c_str());
        return false;
    }
    configLoaded = true;

    // Overlay runtime calibration data from SPIFFS if available
    if (SPIFFS.exists("/device_config.json")) {
        File f = SPIFFS.open("/device_config.json", "r");
        if (f) {
            JsonDocument overlay;
            if (deserializeJson(overlay, f) == DeserializationError::Ok) {
                // Merge calibration arrays from overlay into deviceConfigDoc
                if (overlay["sensors"].is<JsonArray>() && deviceConfigDoc["sensors"].is<JsonArray>()) {
                    JsonArray overlaySensors = overlay["sensors"].as<JsonArray>();
                    JsonArray docSensors = deviceConfigDoc["sensors"].as<JsonArray>();
                    for (JsonObject ov : overlaySensors) {
                        String ovType = ov["type"].as<String>();
                        for (JsonObject ds : docSensors) {
                            if (ds["type"].as<String>() == ovType && ov["calibration"].is<JsonArray>()) {
                                ds["calibration"] = ov["calibration"];
                            }
                        }
                    }
                }
                Serial.println("Runtime calibration data merged from SPIFFS");
            }
            f.close();
        }
    }

    Serial.println("Device configuration loaded successfully");
    return true;
}

bool saveDeviceConfig() {
    if (!configLoaded) return false;
    File f = SPIFFS.open("/device_config.json", "w");
    if (!f) {
        Serial.println("[CONFIG] Failed to open /device_config.json for write");
        return false;
    }
    // Save only the sensors array (calibration data we need to persist)
    JsonDocument out;
    JsonArray outSensors = out["sensors"].to<JsonArray>();
    for (JsonObject s : deviceConfigDoc["sensors"].as<JsonArray>()) {
        JsonObject os = outSensors.add<JsonObject>();
        os["type"] = s["type"];
        os["calibration"] = s["calibration"];
    }
    bool ok = (serializeJson(out, f) > 0);
    f.close();
    if (ok) Serial.println("[CONFIG] Calibration data saved to SPIFFS");
    return ok;
}

bool updateSensorCalibration(
    const String& sensorType,
    const String& calibrationType,
    float calibrationValue,
    const String& note
) {
    if (!configLoaded) return false;

    // Build ISO 8601 timestamp
    String timestamp = "";
    if (g_systemEpoch > 0) {
        struct tm* t = gmtime(&g_systemEpoch);
        char buf[25];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);
        timestamp = String(buf);
    }

    // Find sensor in deviceConfigDoc and append calibration entry
    JsonArray sensors = deviceConfigDoc["sensors"].as<JsonArray>();
    for (JsonObject sensor : sensors) {
        if (sensor["type"].as<String>() == sensorType) {
            JsonArray cal = sensor["calibration"].as<JsonArray>();
            JsonObject entry = cal.add<JsonObject>();
            entry["date"] = timestamp.length() > 0 ? timestamp : "unknown";
            entry["type"] = calibrationType;
            if (calibrationValue != 0) entry["value"] = calibrationValue;
            if (note.length() > 0)     entry["note"]  = note;

            // Update in-memory calibration date on the matching sensor object
            if      (sensorType == "Temperature")        tempSensor.setCalibrationDate(timestamp);
            else if (sensorType == "Conductivity")       ecSensor.setCalibrationDate(timestamp);
            else if (sensorType == "pH")                 phSensor.setCalibrationDate(timestamp);
            else if (sensorType == "Dissolved Oxygen")   doSensor.setCalibrationDate(timestamp);

            // Persist to SPIFFS
            saveDeviceConfig();

            Serial.print("[CONFIG] Calibration recorded for ");
            Serial.print(sensorType);
            Serial.print(" (");
            Serial.print(calibrationType);
            Serial.println(")");
            return true;
        }
    }

    Serial.print("[CONFIG] Sensor not found for calibration update: ");
    Serial.println(sensorType);
    return false;
}

String getDeviceGUID() {
    if (!configLoaded) return "";
    return deviceConfigDoc["device_guid"].as<String>();
}

String getPartnerID() {
    if (!configLoaded) return "";
    return deviceConfigDoc["partner_id"].as<String>();
}

String getFirmwareVersion() {
    if (!configLoaded) return "";
    return deviceConfigDoc["firmware_version"].as<String>();
}

JsonObject getSensorMetadata(const String& sensorType) {
    if (!configLoaded) {
        return JsonObject();
    }

    JsonArray sensors = deviceConfigDoc["sensors"].as<JsonArray>();
    for (JsonObject sensor : sensors) {
        if (sensor["type"].as<String>() == sensorType) {
            return sensor;
        }
    }

    return JsonObject();
}

JsonArray getEnabledSensors() {
    // TODO: Filter and return only enabled sensors
    if (!configLoaded) {
        return JsonArray();
    }
    return deviceConfigDoc["sensors"].as<JsonArray>();
}

String getLastCalibrationDate(const String& sensorType) {
    JsonObject metadata = getSensorMetadata(sensorType);
    if (metadata.isNull()) return "";

    JsonArray calibrations = metadata["calibration"].as<JsonArray>();
    if (calibrations.size() == 0) return "";

    JsonObject lastCal = calibrations[calibrations.size() - 1];
    return lastCal["date"].as<String>();
}

bool isSensorEnabled(const String& sensorType) {
    JsonObject metadata = getSensorMetadata(sensorType);
    if (metadata.isNull()) return false;

    return metadata["enabled"].as<bool>();
}

// ============================================================================
// GPS Source Selection Helpers
// ============================================================================

// GPS source: prefer NMEA2000 network, fall back to onboard NEO-6M
bool activeGPSHasValidFix() {
    if (n2kGPS.hasValidFix()) return true;
    return gps.hasValidFix();
}

GPSData activeGPSGetData() {
    if (n2kGPS.hasValidFix()) return n2kGPS.getData();
    return gps.getData();
}

String activeGPSGetTimeUTC() {
    if (n2kGPS.hasValidFix()) return n2kGPS.getTimeUTC();
    return gps.getTimeUTC();
}

// Best available UTC timestamp: GPS first, then NTP, then empty
String getSystemTimeUTC() {
    String gpsTime = activeGPSGetTimeUTC();
    if (gpsTime.length() > 0) return gpsTime;

    // NTP-synced system clock (available after WiFi connects and APIUploader syncs)
    time_t now = time(nullptr);
    if (now > 1000000000UL) {
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        char buf[25];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
        return String(buf);
    }

    return "";
}

// ============================================================================
// NMEA2000 message forward callback (bridges GPS handler → Environment handler)
// ============================================================================

void n2kMsgForward(const tN2kMsg& msg) {
    n2kEnv.handleMsg(msg);
}

// ============================================================================
// Web Server Task (Core 0)
// Runs independently so sensor/GPS/upload work on Core 1 never blocks the UI.
// ============================================================================

void webServerTask(void* pvParameters) {
    for (;;) {
        webServer.handleClient();
        webServer.checkWiFiReconnect();
        vTaskDelay(1);  // 1ms yield — ~1000 handle cycles/sec
    }
}

// ============================================================================
// Setup
// ============================================================================

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(1000);

    Serial.println();
    Serial.println("===========================================");
    Serial.println("   SeaSense Logger - Starting Up");
    Serial.println("===========================================");

    // Initialize LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // Initialize system health (watchdog + error tracking)
    systemHealth.begin(WDT_TIMEOUT_MS, 255, BOOT_LOOP_WINDOW_MS);  // threshold=255 disables safe mode

    // Load device configuration
    Serial.println("\n[CONFIG] Loading device configuration...");
    if (!parseDeviceConfig()) {
        Serial.println("[ERROR] Failed to load device configuration!");
        while(1) {
            systemHealth.feedWatchdog();  // Keep watchdog fed during error blink
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
            delay(LED_BLINK_ERROR);
        }
    }

    Serial.print("[CONFIG] Device GUID: ");
    Serial.println(getDeviceGUID());
    Serial.print("[CONFIG] Partner ID: ");
    Serial.println(getPartnerID());
    Serial.print("[CONFIG] Firmware: ");
    Serial.println(getFirmwareVersion());

    // Initialize I2C
    Serial.println("\n[I2C] Initializing I2C bus...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_FREQUENCY);
    Serial.println("[I2C] I2C bus initialized");

    // Initialize sensors
    Serial.println("\n[SENSORS] Initializing sensors...");

    // Respect enabled flags from device config
    tempSensor.setEnabled(isSensorEnabled("Temperature"));
    ecSensor.setEnabled(isSensorEnabled("Conductivity"));

    if (tempSensor.isEnabled()) {
        if (tempSensor.begin()) {
            Serial.println("[SENSORS] EZO-RTD Temperature sensor initialized");
            Serial.print("[SENSORS] - Serial: ");
            Serial.println(tempSensor.getSerialNumber());
            Serial.print("[SENSORS] - Calibration: ");
            Serial.println(tempSensor.getLastCalibrationDate());
        } else {
            Serial.println("[ERROR] Failed to initialize EZO-RTD sensor");
        }
    } else {
        Serial.println("[SENSORS] Temperature sensor disabled by config");
    }

    if (ecSensor.isEnabled()) {
        if (ecSensor.begin()) {
            Serial.println("[SENSORS] EZO-EC Conductivity sensor initialized");
            Serial.print("[SENSORS] - Serial: ");
            Serial.println(ecSensor.getSerialNumber());
            Serial.print("[SENSORS] - Calibration: ");
            Serial.println(ecSensor.getLastCalibrationDate());
        } else {
            Serial.println("[ERROR] Failed to initialize EZO-EC sensor");
        }
    } else {
        Serial.println("[SENSORS] Conductivity sensor disabled by config");
    }

    if (phSensor.begin()) {
        Serial.println("[SENSORS] EZO-pH sensor initialized");
        Serial.print("[SENSORS] - Serial: ");
        Serial.println(phSensor.getSerialNumber());
        Serial.print("[SENSORS] - Calibration: ");
        Serial.println(phSensor.getLastCalibrationDate());
    } else {
        phSensor.setEnabled(false);  // Prevent blocking read attempts on missing sensor
        Serial.println("[SENSORS] EZO-pH not detected - disabled");
    }

    if (doSensor.begin()) {
        Serial.println("[SENSORS] EZO-DO Dissolved Oxygen sensor initialized");
        Serial.print("[SENSORS] - Serial: ");
        Serial.println(doSensor.getSerialNumber());
        Serial.print("[SENSORS] - Calibration: ");
        Serial.println(doSensor.getLastCalibrationDate());
    } else {
        doSensor.setEnabled(false);  // Prevent blocking read attempts on missing sensor
        Serial.println("[SENSORS] EZO-DO not detected - disabled");
    }

    // Initialize GPS
    Serial.println("\n[GPS] Initializing GPS module...");
    if (gps.begin(GPS_BAUD_RATE)) {
        Serial.println("[GPS] GPS module initialized");
        Serial.println("[GPS] Waiting for GPS fix (this may take 30-60 seconds outdoors)...");
    } else {
        Serial.println("[ERROR] Failed to initialize GPS module");
    }

    // Initialize configuration manager
    Serial.println("\n[CONFIG] Loading runtime configuration...");
    if (!configManager.begin()) {
        Serial.println("[WARNING] Failed to load config from SPIFFS, using defaults");
    }

    // Load sampling configuration
    ConfigManager::SamplingConfig samplingCfg = configManager.getSamplingConfig();
    sensorSamplingIntervalMs = samplingCfg.sensorIntervalMs;
    skipMeasurementIfStationary = samplingCfg.skipIfStationary;
    stationaryDeltaMeters = samplingCfg.stationaryDeltaMeters;
    Serial.print("[CONFIG] Sensor sampling interval: ");
    Serial.print(sensorSamplingIntervalMs / 1000);
    Serial.println(" seconds");
    Serial.print("[CONFIG] Skip stationary cycles: ");
    Serial.println(skipMeasurementIfStationary ? "enabled" : "disabled");

    // Load NMEA output config
    ConfigManager::NMEAConfig nmeaCfg = configManager.getNMEAConfig();
    nmeaOutputEnabled = nmeaCfg.outputEnabled;
    Serial.print("[CONFIG] NMEA2000 outbound PGN output: ");
    Serial.println(nmeaOutputEnabled ? "enabled" : "disabled");

    // Initialize storage
    if (!storage.begin()) {
        Serial.println("[ERROR] No storage systems available!");
        Serial.println("[WARNING] Data will not be saved!");
    }

    // Initialize WiFi and web server
    if (!webServer.begin()) {
        Serial.println("[ERROR] Failed to start web server!");
    }

    // Pin web server to Core 0 so sensor/upload work on Core 1 never blocks the UI
    xTaskCreatePinnedToCore(webServerTask, "WebServer", WEB_SERVER_TASK_STACK_SIZE, NULL, 1, NULL, 0);
    Serial.println("[WIFI] Web server task pinned to Core 0");

    // Initialize API uploader
    Serial.println("\n[API] Initializing API uploader...");
    ConfigManager::APIConfig apiConfig = configManager.getAPIConfig();
    ConfigManager::DeviceConfig deviceConfig = configManager.getDeviceConfig();

    UploadConfig uploadConfig;
    uploadConfig.apiUrl = apiConfig.url;
    uploadConfig.apiKey = apiConfig.apiKey;
    uploadConfig.partnerID = deviceConfig.partnerID;
    uploadConfig.deviceGUID = deviceConfig.deviceGUID;
    uploadConfig.enabled = true;
    uploadConfig.intervalMs = apiConfig.uploadInterval;
    uploadConfig.batchSize = apiConfig.batchSize;
    uploadConfig.maxRetries = apiConfig.maxRetries;

    if (apiUploader.begin(uploadConfig)) {
        Serial.println("[API] API uploader initialized");
    } else {
        Serial.println("[WARNING] API uploader initialization failed");
    }

    // Initialize NMEA2000 GPS listener
    Serial.println("\n[N2K] Initializing NMEA2000 GPS listener...");
    if (n2kGPS.begin()) {
        Serial.println("[N2K] NMEA2000 GPS listener initialized");

        // Initialize NMEA2000 environmental listener (shares CAN bus with GPS)
        tNMEA2000* n2kInstance = n2kGPS.getN2kInstance();
        if (n2kInstance) {
            n2kEnv.begin(n2kInstance);
            n2kGPS.setMsgForwardCallback(n2kMsgForward);
            Serial.println("[N2K] NMEA2000 environmental listener initialized");
        }
    } else {
        Serial.println("[WARNING] NMEA2000 GPS init failed (CAN bus unavailable?)");
    }

    // Create I2C mutex for thread-safe sensor access (Core 0 web vs Core 1 loop)
    g_i2cMutex = xSemaphoreCreateMutex();

    // Initialize pump controller with saved config, then start first cycle
    pumpController.setConfig(configManager.getPumpConfig());
    pumpController.begin();
    pumpController.startPump();  // Begin first pump cycle without waiting for cycleIntervalMs

    Serial.println("\n===========================================");
    Serial.println("   SeaSense Logger - Ready");
    Serial.println("===========================================\n");

    digitalWrite(LED_PIN, LOW);
}

// ============================================================================
// Main Loop
// ============================================================================

// Stamp NMEA2000 environmental context onto a DataRecord
void stampEnvironmentData(DataRecord& record, const N2kEnvironmentData& env) {
    record.windSpeedTrue     = env.windSpeedTrue;
    record.windAngleTrue     = env.windAngleTrue;
    record.windSpeedApparent = env.windSpeedApparent;
    record.windAngleApparent = env.windAngleApparent;
    record.waterDepth        = env.waterDepth;
    record.speedThroughWater = env.speedThroughWater;
    record.waterTempExternal = env.waterTempExternal;
    record.airTemp           = env.airTemp;
    record.baroPressure      = env.baroPressure;
    record.humidity          = env.humidity;
    record.cogTrue           = env.cogTrue;
    record.sog               = env.sog;
    record.heading           = env.heading;
    record.pitch             = env.pitch;
    record.roll              = env.roll;
}

// I2C bus reset: toggles SCL to release stuck slaves
void resetI2CBus() {
    Wire.end();
    pinMode(I2C_SCL_PIN, OUTPUT);
    for (int i = 0; i < 9; i++) {
        digitalWrite(I2C_SCL_PIN, LOW);
        delayMicroseconds(5);
        digitalWrite(I2C_SCL_PIN, HIGH);
        delayMicroseconds(5);
    }
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_FREQUENCY);
    Serial.println("[I2C] Bus reset performed");
}

float distanceMeters(float lat1, float lon1, float lat2, float lon2) {
    // Haversine distance in meters
    const float R = 6371000.0f;
    const float toRad = PI / 180.0f;
    float p1 = lat1 * toRad;
    float p2 = lat2 * toRad;
    float dLat = (lat2 - lat1) * toRad;
    float dLon = (lon2 - lon1) * toRad;

    float a = sinf(dLat * 0.5f) * sinf(dLat * 0.5f)
            + cosf(p1) * cosf(p2) * sinf(dLon * 0.5f) * sinf(dLon * 0.5f);
    float c = 2.0f * atan2f(sqrtf(a), sqrtf(max(0.0f, 1.0f - a)));
    return R * c;
}

void loop() {
    unsigned long now = millis();
    if (g_lastLoopStartMs != 0) {
        unsigned long gap = now - g_lastLoopStartMs;
        if (gap > g_maxLoopGapMs) g_maxLoopGapMs = gap;
    }
    g_lastLoopStartMs = now;

    // Feed watchdog first — if anything below hangs, WDT will reboot
    g_loopStage = "loop:start";
    systemHealth.feedWatchdog();

    // Update GPS sources (must be called frequently)
    g_loopStage = "gps:update";
    gps.update();
    n2kGPS.update();

    // Keep system epoch current for calibration age checks
    if (activeGPSHasValidFix()) {
        GPSData gpsData = activeGPSGetData();
        if (gpsData.epoch > 0 && gpsData.epoch != g_systemEpoch) {
            g_systemEpoch = gpsData.epoch;
            EZOSensor::setSystemEpoch(g_systemEpoch);

            // Stamp deploy_date on first valid GPS time (persists across reboots)
            configManager.stampDeployDate(activeGPSGetTimeUTC());
        }
    } else {
        // No GPS fix — populate from NTP if available and not yet set
        time_t ntpNow = time(nullptr);
        if (ntpNow > 1000000000UL && g_systemEpoch == 0) {
            g_systemEpoch = ntpNow;
            EZOSensor::setSystemEpoch(g_systemEpoch);

            // Also stamp deploy_date from NTP when GPS is unavailable
            configManager.stampDeployDate(getSystemTimeUTC());
        }
    }

    // Advance pump state machine
    g_loopStage = "pump:update";
    pumpController.update();

    // Determine whether to read sensors and whether to persist the results.
    // Two modes:
    //   1. Pump-driven (default): reads gated on pump MEASURING state, always saved
    //   2. Pump disabled (fallback): configured interval timer, always saved
    bool doSensorRead = false;
    bool saveToStorage = false;

    if (pumpController.isEnabled()) {
        if (pumpController.shouldReadSensors()) {
            doSensorRead = true;
            saveToStorage = true;
        }
    } else {
        // Fallback: timer-based reads when pump is disabled (rollover-safe)
        unsigned long lastRead;
        portENTER_CRITICAL(&g_timerMux);
        lastRead = lastSensorReadAt;
        portEXIT_CRITICAL(&g_timerMux);
        if (now - lastRead >= sensorSamplingIntervalMs) {
            portENTER_CRITICAL(&g_timerMux);
            lastSensorReadAt = now;
            portEXIT_CRITICAL(&g_timerMux);
            doSensorRead = true;
            saveToStorage = true;
        }
    }

    if (doSensorRead) {
        g_loopStage = "sensors:cycle";
        // Blink LED to show activity
        digitalWrite(LED_PIN, HIGH);

        Serial.println("\n--- Sensor Reading ---");
        Serial.print("Time: ");
        Serial.print(now);
        Serial.println(" ms");

        // Get GPS data from active source (NMEA2000 preferred, onboard fallback)
        GPSData gpsData = activeGPSGetData();
        const bool gpsFromN2K = n2kGPS.hasValidFix();
        if (activeGPSHasValidFix()) {
            Serial.print("GPS [");
            Serial.print(gpsFromN2K ? "N2K" : "NEO");
            Serial.print("]: ");
            Serial.print(gpsData.latitude, 6);
            Serial.print("° N, ");
            Serial.print(gpsData.longitude, 6);
            Serial.print("° E");
            Serial.print(" (");
            Serial.print(gpsData.satellites);
            Serial.print(" sats, HDOP: ");
            Serial.print(gpsData.hdop, 1);
            Serial.println(")");
            Serial.print("GPS Time: ");
            Serial.println(activeGPSGetTimeUTC());
        } else {
            Serial.print("GPS: ");
            Serial.println(gps.getStatusString());
        }

        // Snapshot NMEA2000 environmental data from boat instruments
        N2kEnvironmentData envData = n2kEnv.getSnapshot();
        if (n2kEnv.hasAnyData()) {
            Serial.print("N2K Env: ");
            Serial.println(n2kEnv.getStatusString());
        }

        // Track consecutive sensor failures for I2C bus reset
        static uint8_t consecutiveSensorFails = 0;
        uint8_t sensorFailsThisCycle = 0;

        // GPS NaN guard helper
        const bool gpsValid = activeGPSHasValidFix()
            && !isnan(gpsData.latitude) && !isnan(gpsData.longitude);

        // Optional movement gate: skip storage cycle if boat has not moved enough
        bool skipByMotionGate = false;
        if (saveToStorage && skipMeasurementIfStationary) {
            if (gpsValid && hasLastMeasurementGPS) {
                float deltaM = distanceMeters(lastMeasurementLat, lastMeasurementLon, gpsData.latitude, gpsData.longitude);
                if (deltaM < stationaryDeltaMeters) {
                    skipByMotionGate = true;
                    Serial.print("[SAMPLING] Skipping cycle (stationary, Δ=");
                    Serial.print(deltaM, 1);
                    Serial.print("m < ");
                    Serial.print(stationaryDeltaMeters, 1);
                    Serial.println("m)");
                }
            } else if (!gpsValid) {
                Serial.println("[SAMPLING] Motion gate enabled, but GPS fix invalid — measuring anyway");
            }
        }

        if (skipByMotionGate) {
            // Complete pump cycle gracefully without reading/logging
            pumpController.notifyMeasurementComplete();
            digitalWrite(LED_PIN, LOW);
        } else {
            // Acquire I2C mutex for sensor reads (prevents collision with web server)
            bool i2cLocked = (g_i2cMutex != NULL) && xSemaphoreTake(g_i2cMutex, pdMS_TO_TICKS(2000));

        // Read temperature
        g_loopStage = "sensor:temp";
        systemHealth.feedWatchdog();
        if (tempSensor.isEnabled() && tempSensor.read()) {
            SensorData tempData = tempSensor.getData();

            Serial.print("Temperature: ");
            Serial.print(tempData.value, 2);
            Serial.print(" ");
            Serial.print(tempData.unit);
            Serial.print(" [");
            Serial.print(tempData.quality == SensorQuality::GOOD ? "GOOD" :
                        tempData.quality == SensorQuality::FAIR ? "FAIR" :
                        tempData.quality == SensorQuality::POOR ? "POOR" :
                        tempData.quality == SensorQuality::NOT_CALIBRATED ? "NOT_CAL" : "ERROR");
            Serial.println("]");

            // Set temperature compensation for EC, pH, and DO sensors
            ecSensor.setTemperatureCompensation(tempData.value);
            phSensor.setTemperatureCompensation(tempData.value);
            doSensor.setTemperatureCompensation(tempData.value);

            // Create DataRecord with GPS + environmental data
            DataRecord record = sensorDataToRecord(tempData, getSystemTimeUTC());
            if (gpsValid) {
                record.latitude = gpsData.latitude;
                record.longitude = gpsData.longitude;
                record.altitude = gpsData.altitude;
                record.gps_satellites = gpsData.satellites;
                record.gps_hdop = gpsData.hdop;
            }
            stampEnvironmentData(record, envData);

            // Log to storage (pump-driven and fallback modes only)
            if (saveToStorage && !storage.writeRecord(record)) {
                Serial.println("[STORAGE] Failed to log temperature");
            }
        } else {
            Serial.println("Temperature: READ FAILED");
            sensorFailsThisCycle++;
            systemHealth.recordError(ErrorType::SENSOR);
        }

        systemHealth.feedWatchdog();

        // Read conductivity
        g_loopStage = "sensor:ec";
        systemHealth.feedWatchdog();
        if (ecSensor.isEnabled() && ecSensor.read()) {
            SensorData ecData = ecSensor.getData();

            Serial.print("Conductivity: ");
            Serial.print(ecData.value, 0);
            Serial.print(" ");
            Serial.print(ecData.unit);
            Serial.print(" [");
            Serial.print(ecData.quality == SensorQuality::GOOD ? "GOOD" :
                        ecData.quality == SensorQuality::FAIR ? "FAIR" :
                        ecData.quality == SensorQuality::POOR ? "POOR" :
                        ecData.quality == SensorQuality::NOT_CALIBRATED ? "NOT_CAL" : "ERROR");
            Serial.println("]");

            // Calculate and display salinity
            float salinity = ecSensor.getSalinity();
            Serial.print("Salinity: ");
            Serial.print(salinity, 2);
            Serial.println(" PSU");

            // Create DataRecord with GPS + environmental data
            DataRecord ecRecord = sensorDataToRecord(ecData, getSystemTimeUTC());
            if (gpsValid) {
                ecRecord.latitude = gpsData.latitude;
                ecRecord.longitude = gpsData.longitude;
                ecRecord.altitude = gpsData.altitude;
                ecRecord.gps_satellites = gpsData.satellites;
                ecRecord.gps_hdop = gpsData.hdop;
            }
            stampEnvironmentData(ecRecord, envData);

            // Log to storage (pump-driven and fallback modes only)
            if (saveToStorage && !storage.writeRecord(ecRecord)) {
                Serial.println("[STORAGE] Failed to log conductivity");
            }
        } else {
            Serial.println("Conductivity: READ FAILED");
            sensorFailsThisCycle++;
            systemHealth.recordError(ErrorType::SENSOR);
        }

        systemHealth.feedWatchdog();

        // Read pH
        g_loopStage = "sensor:ph";
        systemHealth.feedWatchdog();
        if (phSensor.isEnabled() && phSensor.read()) {
            SensorData phData = phSensor.getData();

            Serial.print("pH: ");
            Serial.print(phData.value, 2);
            Serial.print(" ");
            Serial.print(phData.unit);
            Serial.print(" [");
            Serial.print(phData.quality == SensorQuality::GOOD ? "GOOD" :
                        phData.quality == SensorQuality::FAIR ? "FAIR" :
                        phData.quality == SensorQuality::POOR ? "POOR" :
                        phData.quality == SensorQuality::NOT_CALIBRATED ? "NOT_CAL" : "ERROR");
            Serial.println("]");

            // Create DataRecord with GPS + environmental data
            DataRecord phRecord = sensorDataToRecord(phData, getSystemTimeUTC());
            if (gpsValid) {
                phRecord.latitude = gpsData.latitude;
                phRecord.longitude = gpsData.longitude;
                phRecord.altitude = gpsData.altitude;
                phRecord.gps_satellites = gpsData.satellites;
                phRecord.gps_hdop = gpsData.hdop;
            }
            stampEnvironmentData(phRecord, envData);

            // Log to storage (pump-driven and fallback modes only)
            if (saveToStorage && !storage.writeRecord(phRecord)) {
                Serial.println("[STORAGE] Failed to log pH");
            }
        } else {
            Serial.println("pH: READ FAILED");
            sensorFailsThisCycle++;
            systemHealth.recordError(ErrorType::SENSOR);
        }

        systemHealth.feedWatchdog();

        // Read dissolved oxygen (set salinity compensation BEFORE read)
        g_loopStage = "sensor:do";
        systemHealth.feedWatchdog();
        if (doSensor.isEnabled()) {
            doSensor.setSalinityCompensation(ecSensor.getSalinity());
        }
        if (doSensor.isEnabled() && doSensor.read()) {
            SensorData doData = doSensor.getData();

            Serial.print("Dissolved Oxygen: ");
            Serial.print(doData.value, 2);
            Serial.print(" ");
            Serial.print(doData.unit);
            Serial.print(" [");
            Serial.print(doData.quality == SensorQuality::GOOD ? "GOOD" :
                        doData.quality == SensorQuality::FAIR ? "FAIR" :
                        doData.quality == SensorQuality::POOR ? "POOR" :
                        doData.quality == SensorQuality::NOT_CALIBRATED ? "NOT_CAL" : "ERROR");
            Serial.println("]");

            // Create DataRecord with GPS + environmental data
            DataRecord doRecord = sensorDataToRecord(doData, getSystemTimeUTC());
            if (gpsValid) {
                doRecord.latitude = gpsData.latitude;
                doRecord.longitude = gpsData.longitude;
                doRecord.altitude = gpsData.altitude;
                doRecord.gps_satellites = gpsData.satellites;
                doRecord.gps_hdop = gpsData.hdop;
            }
            stampEnvironmentData(doRecord, envData);

            // Log to storage (pump-driven and fallback modes only)
            if (saveToStorage && !storage.writeRecord(doRecord)) {
                Serial.println("[STORAGE] Failed to log dissolved oxygen");
            }
        } else {
            Serial.println("Dissolved Oxygen: READ FAILED");
            sensorFailsThisCycle++;
            systemHealth.recordError(ErrorType::SENSOR);
        }

        // Release I2C mutex after all sensor reads
        if (i2cLocked) {
            xSemaphoreGive(g_i2cMutex);
        }

        Serial.println("----------------------");

        // I2C bus reset if all sensors are consistently failing
        if (sensorFailsThisCycle > 0) {
            consecutiveSensorFails += sensorFailsThisCycle;
        } else {
            consecutiveSensorFails = 0;
        }

        if (consecutiveSensorFails >= I2C_BUS_RESET_THRESHOLD) {
            resetI2CBus();
            consecutiveSensorFails = 0;
        }

        // TODO: Generate NMEA2000 PGNs (when enabled)
        if (nmeaOutputEnabled) {
            // TODO: emit outbound NMEA2000 PGNs
        }

        // Notify pump that all sensors have been read this cycle
        if (saveToStorage) {
            pumpController.notifyMeasurementComplete();
            if (gpsValid) {
                hasLastMeasurementGPS = true;
                lastMeasurementLat = gpsData.latitude;
                lastMeasurementLon = gpsData.longitude;
            }
        }

        digitalWrite(LED_PIN, LOW);
        }
    }

    // Feed watchdog before upload path (resets budget after sensor reads)
    systemHealth.feedWatchdog();

    // Process API upload (non-blocking)
    g_loopStage = "upload:process";
    apiUploader.process();

    // Feed watchdog before calibration
    systemHealth.feedWatchdog();

    // Update calibration state machine
    g_loopStage = "calibration:update";
    calibration.update();

    // Handle serial commands
    g_loopStage = "serial:process";
    serialCommands.process();

    g_loopStage = "loop:idle";
}
