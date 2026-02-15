/**
 * SeaSense Logger - Web Server Implementation
 */

#include "WebServer.h"
#include "../sensors/EZO_RTD.h"
#include "../sensors/EZO_EC.h"
#include "../../config/hardware_config.h"
#include "../../config/secrets.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>

// ============================================================================
// Constructor / Destructor
// ============================================================================

SeaSenseWebServer::SeaSenseWebServer(EZO_RTD* tempSensor, EZO_EC* ecSensor, StorageManager* storage, CalibrationManager* calibration, PumpController* pumpController)
    : _tempSensor(tempSensor),
      _ecSensor(ecSensor),
      _storage(storage),
      _calibration(calibration),
      _pumpController(pumpController),
      _apIP(192, 168, 4, 1),
      _stationConnected(false),
      _server(nullptr),
      _dnsServer(nullptr)
{
    _apSSID = generateAPSSID();
}

SeaSenseWebServer::~SeaSenseWebServer() {
    if (_server) delete _server;
    if (_dnsServer) delete _dnsServer;
}

// ============================================================================
// Public Methods
// ============================================================================

bool SeaSenseWebServer::begin() {
    DEBUG_WIFI_PRINTLN("Initializing web server...");

    // Start Access Point mode
    if (!startAP()) {
        Serial.println("[WIFI] Failed to start Access Point");
        return false;
    }

    // Attempt to connect to boat WiFi (Station mode)
    startStation();

    // Create web server
    _server = new WebServer(80);

    // Register page handlers
    _server->on("/", std::bind(&SeaSenseWebServer::handleRoot, this));
    _server->on("/dashboard", std::bind(&SeaSenseWebServer::handleDashboard, this));
    _server->on("/calibrate", std::bind(&SeaSenseWebServer::handleCalibrate, this));
    _server->on("/data", std::bind(&SeaSenseWebServer::handleData, this));
    _server->on("/settings", std::bind(&SeaSenseWebServer::handleSettings, this));

    // Register API handlers
    _server->on("/api/sensors", std::bind(&SeaSenseWebServer::handleApiSensors, this));
    _server->on("/api/sensor/reading", std::bind(&SeaSenseWebServer::handleApiSensorReading, this));
    _server->on("/api/calibrate", std::bind(&SeaSenseWebServer::handleApiCalibrate, this));
    _server->on("/api/calibrate/status", std::bind(&SeaSenseWebServer::handleApiCalibrateStatus, this));
    _server->on("/api/data/list", std::bind(&SeaSenseWebServer::handleApiDataList, this));
    _server->on("/api/data/download", std::bind(&SeaSenseWebServer::handleApiDataDownload, this));
    _server->on("/api/data/clear", std::bind(&SeaSenseWebServer::handleApiDataClear, this));
    _server->on("/api/config", std::bind(&SeaSenseWebServer::handleApiConfig, this));
    _server->on("/api/config/update", std::bind(&SeaSenseWebServer::handleApiConfigUpdate, this));
    _server->on("/api/status", std::bind(&SeaSenseWebServer::handleApiStatus, this));
    _server->on("/api/pump/status", std::bind(&SeaSenseWebServer::handleApiPumpStatus, this));
    _server->on("/api/pump/control", std::bind(&SeaSenseWebServer::handleApiPumpControl, this));
    _server->on("/api/pump/config", std::bind(&SeaSenseWebServer::handleApiPumpConfig, this));
    _server->on("/api/pump/config/update", std::bind(&SeaSenseWebServer::handleApiPumpConfigUpdate, this));

    _server->onNotFound(std::bind(&SeaSenseWebServer::handleNotFound, this));

    _server->begin();

    Serial.println("[WIFI] Web server started");
    Serial.print("[WIFI] Access Point: ");
    Serial.println(_apSSID);
    Serial.print("[WIFI] AP IP: http://");
    Serial.println(_apIP);

    if (_stationConnected) {
        Serial.print("[WIFI] Station IP: http://");
        Serial.println(WiFi.localIP());
    }

    return true;
}

void SeaSenseWebServer::handleClient() {
    if (_server) {
        _server->handleClient();
    }
    if (_dnsServer) {
        _dnsServer->processNextRequest();
    }
}

bool SeaSenseWebServer::isWiFiConnected() const {
    return _stationConnected && (WiFi.status() == WL_CONNECTED);
}

String SeaSenseWebServer::getWiFiStatus() const {
    if (isWiFiConnected()) {
        return "Connected to " + String(WIFI_STATION_SSID);
    }
    return "AP Mode Only";
}

String SeaSenseWebServer::getAPIP() const {
    return _apIP.toString();
}

String SeaSenseWebServer::getStationIP() const {
    if (isWiFiConnected()) {
        return WiFi.localIP().toString();
    }
    return "";
}

// ============================================================================
// WiFi Setup
// ============================================================================

bool SeaSenseWebServer::startAP() {
    DEBUG_WIFI_PRINTLN("Starting Access Point...");

    // Set up Access Point
    WiFi.mode(WIFI_AP_STA);  // AP + Station mode
    WiFi.softAPConfig(_apIP, _apIP, IPAddress(255, 255, 255, 0));

    if (!WiFi.softAP(_apSSID.c_str(), WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONNECTIONS)) {
        return false;
    }

    DEBUG_WIFI_PRINT("AP SSID: ");
    DEBUG_WIFI_PRINTLN(_apSSID);
    DEBUG_WIFI_PRINT("AP IP: ");
    DEBUG_WIFI_PRINTLN(_apIP);

    // Start DNS server for captive portal
    _dnsServer = new DNSServer();
    _dnsServer->start(53, "*", _apIP);

    return true;
}

bool SeaSenseWebServer::startStation() {
    // Check if WiFi credentials are configured
    String ssid = String(WIFI_STATION_SSID);
    if (ssid.length() == 0) {
        DEBUG_WIFI_PRINTLN("No WiFi credentials configured");
        return false;
    }

    DEBUG_WIFI_PRINTLN("Connecting to WiFi...");
    DEBUG_WIFI_PRINT("SSID: ");
    DEBUG_WIFI_PRINTLN(ssid);

    WiFi.begin(WIFI_STATION_SSID, WIFI_STATION_PASSWORD);

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - startTime < WIFI_STATION_CONNECT_TIMEOUT_MS) {
        delay(500);
        DEBUG_WIFI_PRINT(".");
    }
    DEBUG_WIFI_PRINTLN("");

    if (WiFi.status() == WL_CONNECTED) {
        _stationConnected = true;
        DEBUG_WIFI_PRINT("Connected! IP: ");
        DEBUG_WIFI_PRINTLN(WiFi.localIP());
        return true;
    } else {
        _stationConnected = false;
        DEBUG_WIFI_PRINTLN("Connection failed");
        return false;
    }
}

String SeaSenseWebServer::generateAPSSID() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "%s%02X%02X",
             WIFI_AP_SSID_PREFIX, mac[4], mac[5]);
    return String(ssid);
}

// ============================================================================
// HTTP Page Handlers
// ============================================================================

void SeaSenseWebServer::handleRoot() {
    // Redirect to dashboard
    _server->sendHeader("Location", "/dashboard");
    _server->send(302);
}

void SeaSenseWebServer::handleDashboard() {
    // For now, send simple HTML
    // TODO: Serve from SPIFFS
    String html = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <title>SeaSense Logger</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; margin: 20px; }
        .sensor { padding: 15px; margin: 10px 0; border: 1px solid #ddd; border-radius: 5px; }
        .value { font-size: 24px; font-weight: bold; }
        .unit { color: #666; }
        .quality { display: inline-block; padding: 3px 8px; border-radius: 3px; font-size: 12px; }
        .quality-good { background: #4CAF50; color: white; }
        .quality-fair { background: #FFC107; color: black; }
        .quality-error { background: #F44336; color: white; }
    </style>
</head>
<body>
    <h1>SeaSense Logger</h1>
    <div id="sensors"></div>
    <script>
        function update() {
            fetch('/api/sensors')
                .then(r => r.json())
                .then(data => {
                    let html = '';
                    data.sensors.forEach(s => {
                        html += `<div class="sensor">
                            <h3>${s.type}</h3>
                            <div class="value">${s.value.toFixed(2)} <span class="unit">${s.unit}</span></div>
                            <div><span class="quality quality-${s.quality}">${s.quality.toUpperCase()}</span></div>
                        </div>`;
                    });
                    document.getElementById('sensors').innerHTML = html;
                });
        }
        update();
        setInterval(update, 2000);
    </script>
</body>
</html>
)HTML";

    _server->send(200, "text/html", html);
}

void SeaSenseWebServer::handleCalibrate() {
    _server->send(200, "text/html", "<h1>Calibration</h1><p>Coming soon...</p>");
}

void SeaSenseWebServer::handleData() {
    _server->send(200, "text/html", "<h1>Data</h1><p>Coming soon...</p>");
}

void SeaSenseWebServer::handleSettings() {
    _server->send(200, "text/html", "<h1>Settings</h1><p>Coming soon...</p>");
}

void SeaSenseWebServer::handleNotFound() {
    sendError("Not Found", 404);
}

// ============================================================================
// API Handlers
// ============================================================================

void SeaSenseWebServer::handleApiSensors() {
    sendJSON(allSensorsToJSON());
}

void SeaSenseWebServer::handleApiSensorReading() {
    // Get sensor type from query parameter
    String sensorType = _server->arg("type");

    if (sensorType == "temperature" && _tempSensor) {
        sendJSON(sensorToJSON(_tempSensor));
    } else if (sensorType == "conductivity" && _ecSensor) {
        sendJSON(sensorToJSON(_ecSensor));
    } else {
        sendError("Unknown sensor type");
    }
}

void SeaSenseWebServer::handleApiCalibrate() {
    if (_server->method() != HTTP_POST) {
        sendError("Method not allowed", 405);
        return;
    }

    if (!_calibration) {
        sendError("Calibration manager not available");
        return;
    }

    // Parse request body
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, _server->arg("plain"));

    if (error) {
        sendError("Invalid JSON");
        return;
    }

    String sensorType = doc["sensor"] | "";
    String calType = doc["type"] | "";
    float referenceValue = doc["value"] | 0.0;

    // Map calibration type string to enum
    CalibrationType calibrationType = CalibrationType::NONE;

    if (sensorType == "temperature") {
        if (calType == "single") {
            calibrationType = CalibrationType::TEMPERATURE_SINGLE;
        }
    } else if (sensorType == "conductivity") {
        if (calType == "dry") {
            calibrationType = CalibrationType::EC_DRY;
        } else if (calType == "single") {
            calibrationType = CalibrationType::EC_SINGLE;
        } else if (calType == "two-low") {
            calibrationType = CalibrationType::EC_TWO_LOW;
        } else if (calType == "two-high") {
            calibrationType = CalibrationType::EC_TWO_HIGH;
        }
    }

    if (calibrationType == CalibrationType::NONE) {
        sendError("Invalid calibration type");
        return;
    }

    // Start calibration
    if (_calibration->startCalibration(sensorType, calibrationType, referenceValue)) {
        sendJSON("{\"success\":true,\"message\":\"Calibration started\"}");
    } else {
        sendError("Failed to start calibration");
    }
}

void SeaSenseWebServer::handleApiCalibrateStatus() {
    if (!_calibration) {
        sendJSON("{\"status\":\"idle\"}");
        return;
    }

    CalibrationState state = _calibration->getState();

    StaticJsonDocument<256> doc;

    // Map status enum to string
    switch (state.status) {
        case CalibrationStatus::IDLE:
            doc["status"] = "idle";
            break;
        case CalibrationStatus::PREPARING:
            doc["status"] = "preparing";
            break;
        case CalibrationStatus::WAITING_STABLE:
            doc["status"] = "waiting_stable";
            break;
        case CalibrationStatus::CALIBRATING:
            doc["status"] = "calibrating";
            break;
        case CalibrationStatus::COMPLETE:
            doc["status"] = "complete";
            break;
        case CalibrationStatus::ERROR:
            doc["status"] = "error";
            break;
    }

    doc["message"] = state.message;
    doc["currentReading"] = state.currentReading;
    doc["referenceValue"] = state.referenceValue;
    doc["success"] = state.success;

    String json;
    serializeJson(doc, json);
    sendJSON(json);
}

void SeaSenseWebServer::handleApiDataList() {
    StorageStats stats = _storage->getStats();

    StaticJsonDocument<256> doc;
    doc["totalRecords"] = stats.totalRecords;
    doc["usedBytes"] = stats.usedBytes;
    doc["totalBytes"] = stats.totalBytes;

    String json;
    serializeJson(doc, json);
    sendJSON(json);
}

void SeaSenseWebServer::handleApiDataDownload() {
    // TODO: Implement data download
    sendError("Not implemented yet");
}

void SeaSenseWebServer::handleApiDataClear() {
    if (_server->method() == HTTP_POST) {
        if (_storage->clear()) {
            sendJSON("{\"success\":true}");
        } else {
            sendError("Failed to clear data");
        }
    } else {
        sendError("Method not allowed", 405);
    }
}

void SeaSenseWebServer::handleApiConfig() {
    // TODO: Implement config retrieval
    sendJSON("{\"device_guid\":\"seasense-001\"}");
}

void SeaSenseWebServer::handleApiConfigUpdate() {
    // TODO: Implement config update
    sendError("Not implemented yet");
}

void SeaSenseWebServer::handleApiStatus() {
    StaticJsonDocument<512> doc;

    doc["uptime"] = millis();
    doc["wifi"]["ap_ssid"] = _apSSID;
    doc["wifi"]["ap_ip"] = getAPIP();
    doc["wifi"]["station_connected"] = isWiFiConnected();
    if (isWiFiConnected()) {
        doc["wifi"]["station_ip"] = getStationIP();
    }

    doc["storage"]["status"] = _storage->getStatusString();
    doc["storage"]["spiffs_mounted"] = _storage->isSPIFFSMounted();
    doc["storage"]["sd_mounted"] = _storage->isSDMounted();

    String json;
    serializeJson(doc, json);
    sendJSON(json);
}

// ============================================================================
// Helper Methods
// ============================================================================

void SeaSenseWebServer::sendJSON(const String& json, int statusCode) {
    _server->send(statusCode, "application/json", json);
}

void SeaSenseWebServer::sendError(const String& message, int statusCode) {
    StaticJsonDocument<128> doc;
    doc["error"] = message;

    String json;
    serializeJson(doc, json);
    sendJSON(json, statusCode);
}

void SeaSenseWebServer::serveHTML(const String& path) {
    if (SPIFFS.exists(path)) {
        File file = SPIFFS.open(path, "r");
        _server->streamFile(file, "text/html");
        file.close();
    } else {
        handleNotFound();
    }
}

String SeaSenseWebServer::sensorToJSON(ISensor* sensor) {
    if (!sensor) {
        return "{}";
    }

    StaticJsonDocument<256> doc;
    SensorData data = sensor->getData();

    doc["type"] = data.sensorType;
    doc["model"] = data.sensorModel;
    doc["serial"] = data.sensorSerial;
    doc["value"] = data.value;
    doc["unit"] = data.unit;
    doc["quality"] = sensorQualityToString(data.quality);
    doc["valid"] = data.valid;
    doc["timestamp"] = data.timestamp;

    String json;
    serializeJson(doc, json);
    return json;
}

String SeaSenseWebServer::allSensorsToJSON() {
    StaticJsonDocument<512> doc;
    JsonArray sensors = doc.createNestedArray("sensors");

    if (_tempSensor) {
        SensorData data = _tempSensor->getData();
        JsonObject sensor = sensors.createNestedObject();
        sensor["type"] = data.sensorType;
        sensor["model"] = data.sensorModel;
        sensor["value"] = data.value;
        sensor["unit"] = data.unit;
        sensor["quality"] = sensorQualityToString(data.quality);
    }

    if (_ecSensor) {
        SensorData data = _ecSensor->getData();
        JsonObject sensor = sensors.createNestedObject();
        sensor["type"] = data.sensorType;
        sensor["model"] = data.sensorModel;
        sensor["value"] = data.value;
        sensor["unit"] = data.unit;
        sensor["quality"] = sensorQualityToString(data.quality);

        // Add salinity
        float salinity = _ecSensor->getSalinity();
        JsonObject salinityObj = sensors.createNestedObject();
        salinityObj["type"] = "Salinity";
        salinityObj["model"] = "Calculated";
        salinityObj["value"] = salinity;
        salinityObj["unit"] = "PSU";
        salinityObj["quality"] = sensorQualityToString(data.quality);
    }

    String json;
    serializeJson(doc, json);
    return json;
}

// ============================================================================
// API Handlers - Pump
// ============================================================================

void SeaSenseWebServer::handleApiPumpStatus() {
    if (!_pumpController) {
        sendError("Pump controller not available", 503);
        return;
    }

    DynamicJsonDocument doc(512);
    doc["state"] = pumpStateToString(_pumpController->getState());
    doc["enabled"] = _pumpController->isEnabled();
    doc["relay_on"] = _pumpController->isRelayOn();
    doc["cycle_progress"] = _pumpController->getCycleProgress();
    doc["cycle_elapsed_s"] = _pumpController->getCycleElapsed() / 1000;
    doc["cycle_interval_s"] = _pumpController->getCycleInterval() / 1000;

    String lastError = _pumpController->getLastError();
    if (lastError.length() > 0) {
        doc["last_error"] = lastError;
    }

    String json;
    serializeJson(doc, json);
    sendJSON(json);
}

void SeaSenseWebServer::handleApiPumpControl() {
    if (!_pumpController) {
        sendError("Pump controller not available", 503);
        return;
    }

    if (_server->method() != HTTP_POST) {
        sendError("Method not allowed", 405);
        return;
    }

    // Parse request body
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, _server->arg("plain"));

    if (error) {
        sendError("Invalid JSON", 400);
        return;
    }

    String action = doc["action"].as<String>();
    action.toUpperCase();

    if (action == "START") {
        _pumpController->startPump();
        sendJSON("{\"success\":true,\"message\":\"Pump started\"}");
    }
    else if (action == "STOP") {
        _pumpController->stopPump();
        sendJSON("{\"success\":true,\"message\":\"Pump stopped\"}");
    }
    else if (action == "PAUSE") {
        _pumpController->pause();
        sendJSON("{\"success\":true,\"message\":\"Pump paused\"}");
    }
    else if (action == "RESUME") {
        _pumpController->resume();
        sendJSON("{\"success\":true,\"message\":\"Pump resumed\"}");
    }
    else if (action == "ENABLE") {
        _pumpController->setEnabled(true);
        sendJSON("{\"success\":true,\"message\":\"Pump controller enabled\"}");
    }
    else if (action == "DISABLE") {
        _pumpController->setEnabled(false);
        sendJSON("{\"success\":true,\"message\":\"Pump controller disabled\"}");
    }
    else {
        sendError("Unknown action: " + action, 400);
    }
}

void SeaSenseWebServer::handleApiPumpConfig() {
    if (!_pumpController) {
        sendError("Pump controller not available", 503);
        return;
    }

    const PumpConfig& config = _pumpController->getConfig();

    DynamicJsonDocument doc(512);
    doc["relay_pin"] = config.relayPin;
    doc["enabled"] = config.enabled;
    doc["pump_startup_delay_ms"] = config.pumpStartupDelayMs;
    doc["stability_wait_ms"] = config.stabilityWaitMs;
    doc["measurement_count"] = config.measurementCount;
    doc["measurement_interval_ms"] = config.measurementIntervalMs;
    doc["pump_stop_delay_ms"] = config.pumpStopDelayMs;
    doc["cooldown_ms"] = config.cooldownMs;
    doc["cycle_interval_ms"] = config.cycleIntervalMs;
    doc["max_pump_on_time_ms"] = config.maxPumpOnTimeMs;
    doc["stability_method"] = (config.method == StabilityMethod::FIXED_DELAY ? "FIXED_DELAY" : "VARIANCE");

    String json;
    serializeJson(doc, json);
    sendJSON(json);
}

void SeaSenseWebServer::handleApiPumpConfigUpdate() {
    if (!_pumpController) {
        sendError("Pump controller not available", 503);
        return;
    }

    if (_server->method() != HTTP_POST) {
        sendError("Method not allowed", 405);
        return;
    }

    // Parse request body
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, _server->arg("plain"));

    if (error) {
        sendError("Invalid JSON", 400);
        return;
    }

    // Get current config
    PumpConfig config = _pumpController->getConfig();

    // Update fields if present
    if (doc.containsKey("pump_startup_delay_ms")) {
        config.pumpStartupDelayMs = doc["pump_startup_delay_ms"];
    }
    if (doc.containsKey("stability_wait_ms")) {
        config.stabilityWaitMs = doc["stability_wait_ms"];
    }
    if (doc.containsKey("measurement_count")) {
        config.measurementCount = doc["measurement_count"];
    }
    if (doc.containsKey("measurement_interval_ms")) {
        config.measurementIntervalMs = doc["measurement_interval_ms"];
    }
    if (doc.containsKey("pump_stop_delay_ms")) {
        config.pumpStopDelayMs = doc["pump_stop_delay_ms"];
    }
    if (doc.containsKey("cooldown_ms")) {
        config.cooldownMs = doc["cooldown_ms"];
    }
    if (doc.containsKey("cycle_interval_ms")) {
        config.cycleIntervalMs = doc["cycle_interval_ms"];
    }
    if (doc.containsKey("max_pump_on_time_ms")) {
        config.maxPumpOnTimeMs = doc["max_pump_on_time_ms"];
    }
    if (doc.containsKey("enabled")) {
        config.enabled = doc["enabled"];
    }

    // Apply new configuration
    _pumpController->setConfig(config);

    sendJSON("{\"success\":true,\"message\":\"Configuration updated\"}");
}
