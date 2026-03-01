/**
 * SeaSense Logger - Web Server
 *
 * ESP32 web server for configuration and monitoring
 * - WiFi AP mode: "SeaSense-XXXX" for direct access
 * - WiFi Station mode: Connect to boat WiFi for internet
 * - REST API for sensor data and configuration
 * - Serves HTML/CSS/JS from SPIFFS
 */

#ifndef SEASENSE_WEBSERVER_H
#define SEASENSE_WEBSERVER_H

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "../sensors/SensorInterface.h"
#include "../storage/StorageManager.h"
#include "../calibration/CalibrationManager.h"
#include "../pump/PumpController.h"
#include "../ota/OTAManager.h"

// Forward declarations
class EZO_RTD;
class EZO_EC;
class EZO_pH;
class EZO_DO;
class ConfigManager;

class SeaSenseWebServer {
public:
    /**
     * Constructor
     * @param tempSensor Pointer to temperature sensor
     * @param ecSensor Pointer to conductivity sensor
     * @param storage Pointer to storage manager
     * @param calibration Pointer to calibration manager
     * @param pumpController Pointer to pump controller
     */
    SeaSenseWebServer(EZO_RTD* tempSensor, EZO_EC* ecSensor, StorageManager* storage, CalibrationManager* calibration, PumpController* pumpController = nullptr, ConfigManager* configManager = nullptr, EZO_pH* phSensor = nullptr, EZO_DO* doSensor = nullptr);

    ~SeaSenseWebServer();

    /**
     * Initialize WiFi and web server
     * @return true if successful
     */
    bool begin();

    /**
     * Handle incoming web requests
     * Call this in loop()
     */
    void handleClient();

    /**
     * Check if WiFi is connected (Station mode)
     * @return true if connected to WiFi
     */
    bool isWiFiConnected() const;

    /**
     * Get WiFi status string
     * @return Status description
     */
    String getWiFiStatus() const;

    /**
     * Get Access Point IP address
     * @return IP address string
     */
    String getAPIP() const;

    /**
     * Get Station mode IP address
     * @return IP address string (empty if not connected)
     */
    String getStationIP() const;

    /**
     * Non-blocking WiFi reconnection check.
     * Call from loop() — attempts reconnect every WIFI_STATION_RECONNECT_INTERVAL_MS.
     */
    void checkWiFiReconnect();

private:
    // Sensors
    EZO_RTD* _tempSensor;
    EZO_EC* _ecSensor;
    EZO_pH* _phSensor;
    EZO_DO* _doSensor;

    // Storage
    StorageManager* _storage;

    // Calibration
    CalibrationManager* _calibration;

    // Pump
    PumpController* _pumpController;

    // Configuration
    ConfigManager* _configManager;

    // OTA
    OTAManager _otaManager;

    // WiFi
    String _apSSID;
    IPAddress _apIP;
    bool _stationConnected;
    unsigned long _lastReconnectAttempt;

    // Web server
    WebServer* _server;
    DNSServer* _dnsServer;

    // ========================================================================
    // WiFi Setup
    // ========================================================================

    /**
     * Start Access Point mode
     * @return true if successful
     */
    bool startAP();

    /**
     * Start Station mode (connect to boat WiFi)
     * @return true if successful
     */
    bool startStation();

    /**
     * Generate unique AP SSID from MAC address
     * @return SSID string
     */
    String generateAPSSID();

    // ========================================================================
    // HTTP Request Handlers
    // ========================================================================

    // Pages
    void handleRoot();
    void handleDashboard();
    void handleCalibrate();
    void handleData();
    void handleSettings();
    void handleNotFound();

    // API - Sensors
    void handleApiSensors();
    void handleApiSensorReading();
    void handleApiSensorRead();

    // API - Calibration
    void handleApiCalibrate();
    void handleApiCalibrateStatus();
    void handleApiCalibrationInfo();

    // API - Storage
    void handleApiDataList();
    void handleApiDataLatest();
    void handleApiDataDownload();
    void handleApiDataClear();
    void handleApiDataRecords();

    // API - Upload control
    void handleApiUploadForce();
    void handleApiUploadHistory();

    // API - Configuration
    void handleApiConfig();
    void handleApiConfigUpdate();
    void handleApiDeviceRegenerateGuid();

    // API - System
    void handleApiStatus();

    // API - Environment (NMEA2000)
    void handleApiEnvironment();

    // API - Pump
    void handleApiPumpStatus();
    void handleApiPumpControl();
    void handleApiPumpConfig();
    void handleApiPumpConfigUpdate();

    // API - Measurement mode
    void handleApiMeasurement();

    // API - OTA
    void handleApiOtaUpload();
    void handleApiOtaStatus();
    void handleApiOtaCheck();
    void handleApiOtaInstall();

    // API - System
    void handleApiSystemRestart();
    void handleApiConfigReset();
    void handleApiClearSafeMode();
    void handleApiFactoryReset();

    // ========================================================================
    // Helper Methods
    // ========================================================================

    /**
     * Send JSON response
     * @param json JSON string
     * @param statusCode HTTP status code
     */
    void sendJSON(const String& json, int statusCode = 200);

    /**
     * Send error JSON response
     * @param message Error message
     * @param statusCode HTTP status code
     */
    void sendError(const String& message, int statusCode = 400);

    /**
     * Serve HTML file from SPIFFS
     * @param path File path
     */
    void serveHTML(const String& path);

    /**
     * Get sensor data as JSON
     * @param sensor Sensor interface
     * @return JSON string
     */
    String sensorToJSON(ISensor* sensor);

    /**
     * Get all sensors data as JSON array
     * @return JSON array string
     */
    String allSensorsToJSON();
};

#endif // SEASENSE_WEBSERVER_H
