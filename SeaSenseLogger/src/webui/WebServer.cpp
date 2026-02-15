/**
 * SeaSense Logger - Web Server Implementation
 */

#include "WebServer.h"
#include "../sensors/EZO_RTD.h"
#include "../sensors/EZO_EC.h"
#include "../config/ConfigManager.h"
#include "../../config/hardware_config.h"
#include "../../config/secrets.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>

// ============================================================================
// Constructor / Destructor
// ============================================================================

SeaSenseWebServer::SeaSenseWebServer(EZO_RTD* tempSensor, EZO_EC* ecSensor, StorageManager* storage, CalibrationManager* calibration, PumpController* pumpController, ConfigManager* configManager)
    : _tempSensor(tempSensor),
      _ecSensor(ecSensor),
      _storage(storage),
      _calibration(calibration),
      _pumpController(pumpController),
      _configManager(configManager),
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
    _server->on("/api/config/reset", std::bind(&SeaSenseWebServer::handleApiConfigReset, this));
    _server->on("/api/system/restart", std::bind(&SeaSenseWebServer::handleApiSystemRestart, this));

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
        body { font-family: Arial; margin: 20px; background: #f5f5f5; }
        .nav { background: white; padding: 10px; margin-bottom: 20px; border-radius: 5px; }
        .nav a { margin-right: 15px; text-decoration: none; color: #2196F3; }
        .nav a:hover { text-decoration: underline; }
        .sensor { padding: 15px; margin: 10px 0; background: white; border-radius: 5px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        .value { font-size: 24px; font-weight: bold; }
        .unit { color: #666; }
        .quality { display: inline-block; padding: 3px 8px; border-radius: 3px; font-size: 12px; }
        .quality-good { background: #4CAF50; color: white; }
        .quality-fair { background: #FFC107; color: black; }
        .quality-error { background: #F44336; color: white; }
    </style>
</head>
<body>
    <div class="nav">
        <a href="/dashboard">Dashboard</a>
        <a href="/settings">Settings</a>
        <a href="/calibrate">Calibration</a>
        <a href="/data">Data</a>
    </div>
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
    String html = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <title>Settings - SeaSense Logger</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; margin: 20px; background: #f5f5f5; }
        .header { background: #2196F3; color: white; padding: 15px; border-radius: 5px; margin-bottom: 20px; }
        .section { background: white; padding: 20px; margin: 15px 0; border-radius: 5px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        .section h2 { margin-top: 0; color: #333; border-bottom: 2px solid #2196F3; padding-bottom: 10px; }
        .form-group { margin: 15px 0; }
        .form-group label { display: block; font-weight: bold; margin-bottom: 5px; color: #555; }
        .form-group input, .form-group select { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 3px; box-sizing: border-box; }
        .form-group input[type="checkbox"] { width: auto; }
        .form-group small { color: #777; font-size: 12px; }
        .button { background: #2196F3; color: white; padding: 10px 20px; border: none; border-radius: 3px; cursor: pointer; font-size: 14px; margin: 5px; }
        .button:hover { background: #1976D2; }
        .button-danger { background: #F44336; }
        .button-danger:hover { background: #D32F2F; }
        .button-warning { background: #FF9800; }
        .button-warning:hover { background: #F57C00; }
        .toast { position: fixed; top: 20px; right: 20px; padding: 15px 20px; border-radius: 3px; color: white; display: none; z-index: 1000; }
        .toast-success { background: #4CAF50; }
        .toast-error { background: #F44336; }
        .toast-info { background: #2196F3; }
        .actions { text-align: center; margin-top: 20px; }
        .nav { background: white; padding: 10px; margin-bottom: 20px; border-radius: 5px; }
        .nav a { margin-right: 15px; text-decoration: none; color: #2196F3; }
        .nav a:hover { text-decoration: underline; }
    </style>
</head>
<body>
    <div class="nav">
        <a href="/dashboard">Dashboard</a>
        <a href="/settings">Settings</a>
        <a href="/calibrate">Calibration</a>
        <a href="/data">Data</a>
    </div>

    <div class="header">
        <h1 style="margin:0;">SeaSense Logger Settings</h1>
    </div>

    <div id="toast" class="toast"></div>

    <form id="configForm">
        <!-- WiFi Configuration -->
        <div class="section">
            <h2>WiFi Configuration</h2>
            <div class="form-group">
                <label>Station SSID (Boat WiFi)</label>
                <input type="text" id="wifi-ssid" name="wifi-ssid">
                <small>Leave empty for AP mode only</small>
            </div>
            <div class="form-group">
                <label>Station Password</label>
                <input type="password" id="wifi-password" name="wifi-password">
            </div>
            <div class="form-group">
                <label>AP Password</label>
                <input type="password" id="wifi-ap-password" name="wifi-ap-password">
                <small>Password for SeaSense-XXXX access point</small>
            </div>
        </div>

        <!-- API Configuration -->
        <div class="section">
            <h2>API Configuration</h2>
            <div class="form-group">
                <label>API URL</label>
                <input type="text" id="api-url" name="api-url">
                <small>Example: https://test-api.projectseasense.org</small>
            </div>
            <div class="form-group">
                <label>API Key</label>
                <input type="password" id="api-key" name="api-key">
            </div>
            <div class="form-group">
                <label>Upload Interval (minutes)</label>
                <input type="number" id="api-interval" name="api-interval" min="1" max="1440">
            </div>
            <div class="form-group">
                <label>Batch Size</label>
                <input type="number" id="api-batch" name="api-batch" min="1" max="1000">
                <small>Number of records per upload</small>
            </div>
            <div class="form-group">
                <label>Max Retries</label>
                <input type="number" id="api-retries" name="api-retries" min="1" max="10">
            </div>
        </div>

        <!-- Device Configuration -->
        <div class="section">
            <h2>Device Configuration</h2>
            <div class="form-group">
                <label>Device GUID</label>
                <input type="text" id="device-guid" name="device-guid">
            </div>
            <div class="form-group">
                <label>Partner ID</label>
                <input type="text" id="partner-id" name="partner-id">
            </div>
            <div class="form-group">
                <label>Firmware Version</label>
                <input type="text" id="firmware-version" name="firmware-version" readonly>
            </div>
        </div>

        <!-- Pump Configuration -->
        <div class="section">
            <h2>Pump Configuration</h2>
            <div class="form-group">
                <label>
                    <input type="checkbox" id="pump-enabled" name="pump-enabled">
                    Enable Pump Controller
                </label>
            </div>
            <div class="form-group">
                <label>Cycle Interval (seconds)</label>
                <input type="number" id="pump-cycle" name="pump-cycle" min="10" max="3600">
                <small>Time between measurement cycles</small>
            </div>
            <div class="form-group">
                <label>Startup Delay (milliseconds)</label>
                <input type="number" id="pump-startup" name="pump-startup" min="0" max="10000">
                <small>Wait for pump to start</small>
            </div>
            <div class="form-group">
                <label>Stability Wait (milliseconds)</label>
                <input type="number" id="pump-stability" name="pump-stability" min="0" max="10000">
                <small>Wait for stable readings</small>
            </div>
            <div class="form-group">
                <label>Measurement Count</label>
                <input type="number" id="pump-count" name="pump-count" min="1" max="10">
            </div>
            <div class="form-group">
                <label>Measurement Interval (milliseconds)</label>
                <input type="number" id="pump-measure-interval" name="pump-measure-interval" min="0" max="10000">
            </div>
            <div class="form-group">
                <label>Stop Delay (milliseconds)</label>
                <input type="number" id="pump-stop" name="pump-stop" min="0" max="5000">
                <small>Flush time after measurements</small>
            </div>
            <div class="form-group">
                <label>Cooldown (milliseconds)</label>
                <input type="number" id="pump-cooldown" name="pump-cooldown" min="0" max="300000">
            </div>
            <div class="form-group">
                <label>Max On Time (milliseconds)</label>
                <input type="number" id="pump-max-on" name="pump-max-on" min="1000" max="60000">
                <small>Safety cutoff</small>
            </div>
            <div class="form-group">
                <label>Stability Method</label>
                <select id="pump-method" name="pump-method">
                    <option value="FIXED_DELAY">Fixed Delay</option>
                    <option value="VARIANCE_CHECK">Variance Check</option>
                </select>
            </div>
        </div>

        <!-- Actions -->
        <div class="actions">
            <button type="submit" class="button">Save Configuration</button>
            <button type="button" class="button button-warning" onclick="resetConfig()">Reset to Defaults</button>
            <button type="button" class="button button-danger" onclick="restartDevice()">Restart Device</button>
        </div>
    </form>

    <script>
        async function loadConfig() {
            try {
                const config = await fetch('/api/config').then(r => r.json());

                // WiFi
                document.getElementById('wifi-ssid').value = config.wifi.station_ssid || '';
                document.getElementById('wifi-password').value = config.wifi.station_password || '';
                document.getElementById('wifi-ap-password').value = config.wifi.ap_password || '';

                // API
                document.getElementById('api-url').value = config.api.url || '';
                document.getElementById('api-key').value = config.api.api_key || '';
                document.getElementById('api-interval').value = (config.api.upload_interval_ms / 60000) || 5;
                document.getElementById('api-batch').value = config.api.batch_size || 100;
                document.getElementById('api-retries').value = config.api.max_retries || 5;

                // Device
                document.getElementById('device-guid').value = config.device.device_guid || '';
                document.getElementById('partner-id').value = config.device.partner_id || '';
                document.getElementById('firmware-version').value = config.device.firmware_version || '';

                // Pump
                document.getElementById('pump-enabled').checked = config.pump.enabled || false;
                document.getElementById('pump-cycle').value = (config.pump.cycle_interval_ms / 1000) || 60;
                document.getElementById('pump-startup').value = config.pump.startup_delay_ms || 2000;
                document.getElementById('pump-stability').value = config.pump.stability_wait_ms || 3000;
                document.getElementById('pump-count').value = config.pump.measurement_count || 1;
                document.getElementById('pump-measure-interval').value = config.pump.measurement_interval_ms || 2000;
                document.getElementById('pump-stop').value = config.pump.stop_delay_ms || 500;
                document.getElementById('pump-cooldown').value = config.pump.cooldown_ms || 55000;
                document.getElementById('pump-max-on').value = config.pump.max_on_time_ms || 30000;
                document.getElementById('pump-method').value = config.pump.method || 'FIXED_DELAY';

            } catch (e) {
                showToast('Failed to load configuration: ' + e.message, 'error');
            }
        }

        async function saveConfig(event) {
            event.preventDefault();

            const config = {
                wifi: {
                    station_ssid: document.getElementById('wifi-ssid').value,
                    station_password: document.getElementById('wifi-password').value,
                    ap_password: document.getElementById('wifi-ap-password').value
                },
                api: {
                    url: document.getElementById('api-url').value,
                    api_key: document.getElementById('api-key').value,
                    upload_interval_ms: parseInt(document.getElementById('api-interval').value) * 60000,
                    batch_size: parseInt(document.getElementById('api-batch').value),
                    max_retries: parseInt(document.getElementById('api-retries').value)
                },
                device: {
                    device_guid: document.getElementById('device-guid').value,
                    partner_id: document.getElementById('partner-id').value,
                    firmware_version: document.getElementById('firmware-version').value
                },
                pump: {
                    enabled: document.getElementById('pump-enabled').checked,
                    cycle_interval_ms: parseInt(document.getElementById('pump-cycle').value) * 1000,
                    startup_delay_ms: parseInt(document.getElementById('pump-startup').value),
                    stability_wait_ms: parseInt(document.getElementById('pump-stability').value),
                    measurement_count: parseInt(document.getElementById('pump-count').value),
                    measurement_interval_ms: parseInt(document.getElementById('pump-measure-interval').value),
                    stop_delay_ms: parseInt(document.getElementById('pump-stop').value),
                    cooldown_ms: parseInt(document.getElementById('pump-cooldown').value),
                    max_on_time_ms: parseInt(document.getElementById('pump-max-on').value),
                    method: document.getElementById('pump-method').value
                }
            };

            try {
                const response = await fetch('/api/config/update', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify(config)
                });

                const result = await response.json();

                if (response.ok) {
                    showToast(result.message || 'Configuration saved!', 'success');
                } else {
                    showToast('Error: ' + (result.error || 'Unknown error'), 'error');
                }
            } catch (e) {
                showToast('Network error: ' + e.message, 'error');
            }
        }

        async function resetConfig() {
            if (!confirm('Reset all settings to defaults?')) return;

            try {
                const response = await fetch('/api/config/reset', {method: 'POST'});
                const result = await response.json();

                if (response.ok) {
                    showToast('Configuration reset to defaults', 'success');
                    setTimeout(() => loadConfig(), 1000);
                } else {
                    showToast('Error: ' + (result.error || 'Unknown error'), 'error');
                }
            } catch (e) {
                showToast('Network error: ' + e.message, 'error');
            }
        }

        async function restartDevice() {
            if (!confirm('Restart the device? This will apply WiFi and API changes.')) return;

            try {
                await fetch('/api/system/restart', {method: 'POST'});
                showToast('Device restarting... Reconnect in 30 seconds.', 'info');
                setTimeout(() => {
                    document.body.innerHTML = '<div style="text-align:center;padding:50px;"><h2>Device Restarting...</h2><p>Please wait 30 seconds and refresh the page.</p></div>';
                }, 1000);
            } catch (e) {
                showToast('Restart command sent', 'info');
            }
        }

        function showToast(message, type) {
            const toast = document.getElementById('toast');
            toast.textContent = message;
            toast.className = 'toast toast-' + type;
            toast.style.display = 'block';
            setTimeout(() => { toast.style.display = 'none'; }, 5000);
        }

        document.getElementById('configForm').addEventListener('submit', saveConfig);
        loadConfig();
    </script>
</body>
</html>
)HTML";

    _server->send(200, "text/html", html);
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
    if (!_configManager) {
        sendError("Configuration manager not available", 503);
        return;
    }

    StaticJsonDocument<2048> doc;

    // WiFi config
    ConfigManager::WiFiConfig wifi = _configManager->getWiFiConfig();
    JsonObject wifiObj = doc.createNestedObject("wifi");
    wifiObj["station_ssid"] = wifi.stationSSID;
    wifiObj["station_password"] = wifi.stationPassword;
    wifiObj["ap_password"] = wifi.apPassword;

    // API config
    ConfigManager::APIConfig api = _configManager->getAPIConfig();
    JsonObject apiObj = doc.createNestedObject("api");
    apiObj["url"] = api.url;
    apiObj["api_key"] = api.apiKey;
    apiObj["upload_interval_ms"] = api.uploadInterval;
    apiObj["batch_size"] = api.batchSize;
    apiObj["max_retries"] = api.maxRetries;

    // Device config
    ConfigManager::DeviceConfig device = _configManager->getDeviceConfig();
    JsonObject deviceObj = doc.createNestedObject("device");
    deviceObj["device_guid"] = device.deviceGUID;
    deviceObj["partner_id"] = device.partnerID;
    deviceObj["firmware_version"] = device.firmwareVersion;

    // Pump config
    PumpConfig pump = _configManager->getPumpConfig();
    JsonObject pumpObj = doc.createNestedObject("pump");
    pumpObj["enabled"] = pump.enabled;
    pumpObj["relay_pin"] = pump.relayPin;
    pumpObj["cycle_interval_ms"] = pump.cycleIntervalMs;
    pumpObj["startup_delay_ms"] = pump.pumpStartupDelayMs;
    pumpObj["stability_wait_ms"] = pump.stabilityWaitMs;
    pumpObj["measurement_count"] = pump.measurementCount;
    pumpObj["measurement_interval_ms"] = pump.measurementIntervalMs;
    pumpObj["stop_delay_ms"] = pump.pumpStopDelayMs;
    pumpObj["cooldown_ms"] = pump.cooldownMs;
    pumpObj["max_on_time_ms"] = pump.maxPumpOnTimeMs;
    pumpObj["method"] = (pump.method == StabilityMethod::FIXED_DELAY ? "FIXED_DELAY" : "VARIANCE_CHECK");
    pumpObj["temp_variance_threshold"] = pump.tempVarianceThreshold;
    pumpObj["ec_variance_threshold"] = pump.ecVarianceThreshold;

    String json;
    serializeJson(doc, json);
    sendJSON(json);
}

void SeaSenseWebServer::handleApiConfigUpdate() {
    if (!_configManager) {
        sendError("Configuration manager not available", 503);
        return;
    }

    if (_server->method() != HTTP_POST) {
        sendError("Method not allowed", 405);
        return;
    }

    // Parse request body
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, _server->arg("plain"));

    if (error) {
        sendError("Invalid JSON", 400);
        return;
    }

    // Update WiFi config
    if (doc.containsKey("wifi")) {
        ConfigManager::WiFiConfig wifi;
        wifi.stationSSID = doc["wifi"]["station_ssid"] | "";
        wifi.stationPassword = doc["wifi"]["station_password"] | "";
        wifi.apPassword = doc["wifi"]["ap_password"] | "protectplanet!";
        _configManager->setWiFiConfig(wifi);
    }

    // Update API config
    if (doc.containsKey("api")) {
        ConfigManager::APIConfig api;
        api.url = doc["api"]["url"] | "";
        api.apiKey = doc["api"]["api_key"] | "";
        api.uploadInterval = doc["api"]["upload_interval_ms"] | 300000;
        api.batchSize = doc["api"]["batch_size"] | 100;
        api.maxRetries = doc["api"]["max_retries"] | 5;
        _configManager->setAPIConfig(api);
    }

    // Update device config
    if (doc.containsKey("device")) {
        ConfigManager::DeviceConfig device;
        device.deviceGUID = doc["device"]["device_guid"] | "";
        device.partnerID = doc["device"]["partner_id"] | "";
        device.firmwareVersion = doc["device"]["firmware_version"] | "2.0.0";
        _configManager->setDeviceConfig(device);
    }

    // Update pump config
    if (doc.containsKey("pump")) {
        PumpConfig pump = _configManager->getPumpConfig();

        if (doc["pump"].containsKey("enabled")) {
            pump.enabled = doc["pump"]["enabled"];
        }
        if (doc["pump"].containsKey("relay_pin")) {
            pump.relayPin = doc["pump"]["relay_pin"];
        }
        if (doc["pump"].containsKey("cycle_interval_ms")) {
            pump.cycleIntervalMs = doc["pump"]["cycle_interval_ms"];
        }
        if (doc["pump"].containsKey("startup_delay_ms")) {
            pump.pumpStartupDelayMs = doc["pump"]["startup_delay_ms"];
        }
        if (doc["pump"].containsKey("stability_wait_ms")) {
            pump.stabilityWaitMs = doc["pump"]["stability_wait_ms"];
        }
        if (doc["pump"].containsKey("measurement_count")) {
            pump.measurementCount = doc["pump"]["measurement_count"];
        }
        if (doc["pump"].containsKey("measurement_interval_ms")) {
            pump.measurementIntervalMs = doc["pump"]["measurement_interval_ms"];
        }
        if (doc["pump"].containsKey("stop_delay_ms")) {
            pump.pumpStopDelayMs = doc["pump"]["stop_delay_ms"];
        }
        if (doc["pump"].containsKey("cooldown_ms")) {
            pump.cooldownMs = doc["pump"]["cooldown_ms"];
        }
        if (doc["pump"].containsKey("max_on_time_ms")) {
            pump.maxPumpOnTimeMs = doc["pump"]["max_on_time_ms"];
        }
        if (doc["pump"].containsKey("method")) {
            String method = doc["pump"]["method"];
            if (method == "VARIANCE_CHECK") {
                pump.method = StabilityMethod::VARIANCE_CHECK;
            } else {
                pump.method = StabilityMethod::FIXED_DELAY;
            }
        }
        if (doc["pump"].containsKey("temp_variance_threshold")) {
            pump.tempVarianceThreshold = doc["pump"]["temp_variance_threshold"];
        }
        if (doc["pump"].containsKey("ec_variance_threshold")) {
            pump.ecVarianceThreshold = doc["pump"]["ec_variance_threshold"];
        }

        _configManager->setPumpConfig(pump);
    }

    // Save to SPIFFS
    if (_configManager->save()) {
        sendJSON("{\"success\":true,\"message\":\"Configuration saved. Restart device to apply WiFi and API changes.\"}");
    } else {
        sendError("Failed to save configuration", 500);
    }
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

// ============================================================================
// API Handlers - System
// ============================================================================

void SeaSenseWebServer::handleApiConfigReset() {
    if (!_configManager) {
        sendError("Configuration manager not available", 503);
        return;
    }

    if (_server->method() != HTTP_POST) {
        sendError("Method not allowed", 405);
        return;
    }

    if (_configManager->reset()) {
        sendJSON("{\"success\":true,\"message\":\"Configuration reset to defaults\"}");
    } else {
        sendError("Failed to reset configuration", 500);
    }
}

void SeaSenseWebServer::handleApiSystemRestart() {
    if (_server->method() != HTTP_POST) {
        sendError("Method not allowed", 405);
        return;
    }

    sendJSON("{\"success\":true,\"message\":\"Device restarting...\"}");
    delay(500);  // Let response send
    ESP.restart();
}
