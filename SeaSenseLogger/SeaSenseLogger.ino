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

// Configuration
#include "config/hardware_config.h"
#include "config/device_config.h"
#include "config/secrets.h"

// Sensors
#include "src/sensors/SensorInterface.h"
#include "src/sensors/EZOSensor.h"
#include "src/sensors/EZO_RTD.h"
#include "src/sensors/EZO_EC.h"
#include "src/sensors/GPSModule.h"

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

// Serial Commands
#include "src/commands/SerialCommands.h"

// Configuration Manager
#include "src/config/ConfigManager.h"

// ============================================================================
// Global Variables
// ============================================================================

// Sensors
EZO_RTD tempSensor;
EZO_EC ecSensor;
GPSModule gps(GPS_RX_PIN, GPS_TX_PIN);

// Storage
StorageManager storage(SPIFFS_CIRCULAR_BUFFER_SIZE, SD_CS_PIN);

// Configuration
ConfigManager configManager;

// Calibration
CalibrationManager calibration(&tempSensor, &ecSensor);

// Web server
SeaSenseWebServer webServer(&tempSensor, &ecSensor, &storage, &calibration, nullptr, &configManager);

// API Uploader
APIUploader apiUploader(&storage);

// Serial Commands
SerialCommands serialCommands(&tempSensor, &ecSensor, &gps, &storage, &apiUploader, &webServer, nullptr);

// Device configuration
StaticJsonDocument<4096> deviceConfigDoc;
bool configLoaded = false;

// ============================================================================
// Device Configuration Functions
// ============================================================================

bool parseDeviceConfig() {
    DeserializationError error = deserializeJson(deviceConfigDoc, DEVICE_CONFIG_JSON);

    if (error) {
        Serial.print("Failed to parse device config: ");
        Serial.println(error.c_str());
        return false;
    }

    configLoaded = true;
    Serial.println("Device configuration loaded successfully");
    return true;
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

    // Load device configuration
    Serial.println("\n[CONFIG] Loading device configuration...");
    if (!parseDeviceConfig()) {
        Serial.println("[ERROR] Failed to load device configuration!");
        while(1) {
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

    if (tempSensor.begin()) {
        Serial.println("[SENSORS] EZO-RTD Temperature sensor initialized");
        Serial.print("[SENSORS] - Serial: ");
        Serial.println(tempSensor.getSerialNumber());
        Serial.print("[SENSORS] - Calibration: ");
        Serial.println(tempSensor.getLastCalibrationDate());
    } else {
        Serial.println("[ERROR] Failed to initialize EZO-RTD sensor");
    }

    if (ecSensor.begin()) {
        Serial.println("[SENSORS] EZO-EC Conductivity sensor initialized");
        Serial.print("[SENSORS] - Serial: ");
        Serial.println(ecSensor.getSerialNumber());
        Serial.print("[SENSORS] - Calibration: ");
        Serial.println(ecSensor.getLastCalibrationDate());
    } else {
        Serial.println("[ERROR] Failed to initialize EZO-EC sensor");
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

    // Initialize storage
    if (!storage.begin()) {
        Serial.println("[ERROR] No storage systems available!");
        Serial.println("[WARNING] Data will not be saved!");
    }

    // Initialize WiFi and web server
    if (!webServer.begin()) {
        Serial.println("[ERROR] Failed to start web server!");
    }

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

    // TODO: Initialize NMEA2000

    Serial.println("\n===========================================");
    Serial.println("   SeaSense Logger - Ready");
    Serial.println("===========================================\n");

    digitalWrite(LED_PIN, LOW);
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
    unsigned long now = millis();

    // Update GPS (must be called frequently to process NMEA sentences)
    gps.update();

    // Read sensors at regular intervals
    static unsigned long lastSensorRead = 0;
    if (now - lastSensorRead >= SENSOR_SAMPLING_INTERVAL_MS) {
        lastSensorRead = now;
        // Blink LED to show activity
        digitalWrite(LED_PIN, HIGH);

        Serial.println("\n--- Sensor Reading ---");
        Serial.print("Time: ");
        Serial.print(now);
        Serial.println(" ms");

        // Get GPS data
        GPSData gpsData = gps.getData();
        if (gps.hasValidFix()) {
            Serial.print("GPS: ");
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
            Serial.println(gps.getTimeUTC());
        } else {
            Serial.print("GPS: ");
            Serial.println(gps.getStatusString());
        }

        // Read temperature
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

            // Set temperature compensation for EC sensor
            ecSensor.setTemperatureCompensation(tempData.value);

            // Create DataRecord with GPS data
            DataRecord record = sensorDataToRecord(tempData, gps.getTimeUTC());
            if (gps.hasValidFix()) {
                record.latitude = gpsData.latitude;
                record.longitude = gpsData.longitude;
                record.altitude = gpsData.altitude;
                record.gps_satellites = gpsData.satellites;
                record.gps_hdop = gpsData.hdop;
            }

            // Log to storage
            if (!storage.writeRecord(record)) {
                Serial.println("[STORAGE] Failed to log temperature");
            }
        } else {
            Serial.println("Temperature: READ FAILED");
        }

        // Read conductivity
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

            // Create DataRecord with GPS data
            DataRecord ecRecord = sensorDataToRecord(ecData, gps.getTimeUTC());
            if (gps.hasValidFix()) {
                ecRecord.latitude = gpsData.latitude;
                ecRecord.longitude = gpsData.longitude;
                ecRecord.altitude = gpsData.altitude;
                ecRecord.gps_satellites = gpsData.satellites;
                ecRecord.gps_hdop = gpsData.hdop;
            }

            // Log to storage
            if (!storage.writeRecord(ecRecord)) {
                Serial.println("[STORAGE] Failed to log conductivity");
            }
        } else {
            Serial.println("Conductivity: READ FAILED");
        }

        Serial.println("----------------------");

        // TODO: Generate NMEA2000 PGNs

        digitalWrite(LED_PIN, LOW);
    }

    // Process API upload (non-blocking)
    apiUploader.process();

    // Handle web server requests
    webServer.handleClient();

    // Update calibration state machine
    calibration.update();

    // Handle serial commands
    serialCommands.process();

    delay(10);  // Small delay to prevent watchdog issues
}
