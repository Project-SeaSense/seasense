/**
 * SeaSense Logger - Web Server Implementation
 */

#include "WebServer.h"
#include "../sensors/EZO_RTD.h"
#include "../sensors/EZO_EC.h"
#include "../config/ConfigManager.h"
#include "../../config/hardware_config.h"
#include "../../config/secrets.h"
#include "../system/SystemHealth.h"
#include "../sensors/GPSModule.h"
#include "../api/APIUploader.h"
#if FEATURE_NMEA2000
#include "../sensors/NMEA2000Environment.h"
#endif
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
      _lastReconnectAttempt(0),
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
    _server->on("/api/sensor/read", std::bind(&SeaSenseWebServer::handleApiSensorRead, this));
    _server->on("/api/calibrate", std::bind(&SeaSenseWebServer::handleApiCalibrate, this));
    _server->on("/api/calibrate/status", std::bind(&SeaSenseWebServer::handleApiCalibrateStatus, this));
    _server->on("/api/data/list", std::bind(&SeaSenseWebServer::handleApiDataList, this));
    _server->on("/api/data/download", std::bind(&SeaSenseWebServer::handleApiDataDownload, this));
    _server->on("/api/data/clear", std::bind(&SeaSenseWebServer::handleApiDataClear, this));
    _server->on("/api/data/records", std::bind(&SeaSenseWebServer::handleApiDataRecords, this));
    _server->on("/api/upload/force", std::bind(&SeaSenseWebServer::handleApiUploadForce, this));
    _server->on("/api/upload/history", std::bind(&SeaSenseWebServer::handleApiUploadHistory, this));
    _server->on("/api/device/regenerate-guid", std::bind(&SeaSenseWebServer::handleApiDeviceRegenerateGuid, this));
    _server->on("/api/config", std::bind(&SeaSenseWebServer::handleApiConfig, this));
    _server->on("/api/config/update", std::bind(&SeaSenseWebServer::handleApiConfigUpdate, this));
    _server->on("/api/status", std::bind(&SeaSenseWebServer::handleApiStatus, this));
    _server->on("/api/pump/status", std::bind(&SeaSenseWebServer::handleApiPumpStatus, this));
    _server->on("/api/pump/control", std::bind(&SeaSenseWebServer::handleApiPumpControl, this));
    _server->on("/api/pump/config", std::bind(&SeaSenseWebServer::handleApiPumpConfig, this));
    _server->on("/api/pump/config/update", std::bind(&SeaSenseWebServer::handleApiPumpConfigUpdate, this));
    _server->on("/api/config/reset", std::bind(&SeaSenseWebServer::handleApiConfigReset, this));
    _server->on("/api/environment", std::bind(&SeaSenseWebServer::handleApiEnvironment, this));
    _server->on("/api/measurement", std::bind(&SeaSenseWebServer::handleApiMeasurement, this));
    _server->on("/api/system/restart", std::bind(&SeaSenseWebServer::handleApiSystemRestart, this));
    _server->on("/api/system/clear-safe-mode", std::bind(&SeaSenseWebServer::handleApiClearSafeMode, this));

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

void SeaSenseWebServer::checkWiFiReconnect() {
    // Only attempt if station credentials are configured
    String ssid = String(WIFI_STATION_SSID);
    if (ssid.length() == 0) return;

    // Already connected — update state if needed
    if (WiFi.status() == WL_CONNECTED) {
        if (!_stationConnected) {
            _stationConnected = true;
            Serial.print("[WIFI] Reconnected! IP: ");
            Serial.println(WiFi.localIP());
        }
        return;
    }

    // Not connected — rate-limit reconnection attempts
    unsigned long now = millis();
    if (now - _lastReconnectAttempt < WIFI_STATION_RECONNECT_INTERVAL_MS) return;
    _lastReconnectAttempt = now;

    _stationConnected = false;
    Serial.println("[WIFI] Station disconnected, attempting reconnection...");
    WiFi.disconnect();
    WiFi.begin(WIFI_STATION_SSID, WIFI_STATION_PASSWORD);
    // Non-blocking on ESP32 AP+STA mode — status checked next iteration
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
    <meta charset="UTF-8">
    <title>Project SeaSense</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif; background: #e8f4f8; color: #1a4d5e; }

        /* Header */
        .header { background: linear-gradient(135deg, #0a4f66 0%, #0e7fa3 100%); color: white; padding: 12px 15px; display: flex; align-items: center; box-shadow: 0 2px 8px rgba(0,0,0,0.15); position: sticky; top: 0; z-index: 100; }
        .header-left { display: flex; align-items: center; gap: 12px; }
        .hamburger { background: none; border: none; color: white; font-size: 28px; cursor: pointer; padding: 5px; line-height: 1; font-family: Arial, sans-serif; }
        .hamburger:hover { opacity: 0.8; }
        .title { font-size: 18px; font-weight: 600; white-space: nowrap; }

        /* Sidebar */
        .sidebar { position: fixed; left: -250px; top: 0; width: 250px; height: 100%; background: white; box-shadow: 2px 0 10px rgba(0,0,0,0.1); transition: left 0.3s; z-index: 201; pointer-events: auto; }
        .sidebar.open { left: 0; }
        .sidebar-header { background: #0a4f66; color: white; padding: 15px; font-weight: 600; }
        .sidebar-nav { list-style: none; }
        .sidebar-nav a { display: block; padding: 12px 20px; color: #1a4d5e; text-decoration: none; border-bottom: 1px solid #e0e0e0; transition: background 0.2s; }
        .sidebar-nav a:hover { background: #e8f4f8; }
        .sidebar-nav a.active { background: #d0e8f0; font-weight: 600; }
        .overlay { position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.5); display: none; z-index: 200; pointer-events: auto; cursor: pointer; }
        .overlay.show { display: block; }

        /* Main content */
        .container { padding: 15px; max-width: 600px; margin: 0 auto; }

        /* Sensors */
        .sensors-grid { display: grid; gap: 12px; }
        .sensor-card { background: white; border-radius: 8px; padding: 15px; box-shadow: 0 2px 4px rgba(0,0,0,0.08); border-left: 4px solid #0e7fa3; }
        .sensor-name { font-size: 14px; font-weight: 600; color: #0a4f66; text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 10px; }
        .sensor-value { font-size: 32px; font-weight: 700; color: #1a4d5e; line-height: 1.2; }
        .sensor-unit { font-size: 16px; font-weight: 400; color: #666; margin-left: 4px; }
        .sensor-meta { margin-top: 8px; font-size: 12px; color: #888; }

        /* Environment section */
        .section-title { font-size: 13px; font-weight: 600; color: #0a4f66; text-transform: uppercase; letter-spacing: 1px; margin: 20px 0 10px; padding-bottom: 6px; border-bottom: 2px solid #d0e8f0; }
        .env-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
        .env-card { background: white; border-radius: 8px; padding: 12px; box-shadow: 0 2px 4px rgba(0,0,0,0.08); border-left: 4px solid #2d8659; }
        .env-card.stale { opacity: 0.4; }
        .env-label { font-size: 11px; font-weight: 600; color: #2d6b4a; text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 6px; }
        .env-value { font-size: 22px; font-weight: 700; color: #1a4d5e; line-height: 1.2; }
        .env-unit { font-size: 12px; font-weight: 400; color: #666; margin-left: 3px; }
        .env-none { text-align: center; padding: 15px; color: #aaa; font-size: 13px; grid-column: 1 / -1; }

        /* Loading state */
        .loading-pulse { animation: pulse 1.5s ease-in-out infinite; }
        @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.5; } }

        /* Status message */
        .status-msg { text-align: center; padding: 30px; color: #888; font-size: 14px; }

        /* Measurement control bar */
        .measure-bar { display: flex; align-items: center; justify-content: space-between; background: white; border-radius: 8px; padding: 10px 15px; margin: 10px 0; box-shadow: 0 2px 4px rgba(0,0,0,0.08); }
        .countdown { font-size: 13px; color: #0a4f66; font-weight: 600; font-variant-numeric: tabular-nums; }
        .toggle-btn { padding: 7px 14px; border: none; border-radius: 20px; font-size: 12px; font-weight: 600; cursor: pointer; background: #e0e0e0; color: #555; transition: all 0.2s; }
        .toggle-btn.active { background: #0e7fa3; color: white; }

        /* Upload status bar */
        .upload-bar { background: white; border-radius: 8px; padding: 8px 15px; margin: 0 0 10px; box-shadow: 0 2px 4px rgba(0,0,0,0.08); font-size: 12px; color: #888; display: flex; flex-wrap: wrap; align-items: center; gap: 6px; min-height: 34px; }
        .up-state { font-weight: 700; }
        .up-state.ok { color: #2e7d32; }
        .up-state.err { color: #c62828; }
        .up-state.busy { color: #e65100; }
        .up-nostore { color: #e65100; font-weight: 600; font-style: italic; }
        .up-sep { color: #ccc; }
    </style>
</head>
<body>
    <div class="overlay" id="overlay" onclick="closeMenu()"></div>

    <div class="sidebar" id="sidebar">
        <div class="sidebar-header">Menu</div>
        <ul class="sidebar-nav">
            <li><a href="/dashboard" class="active">Dashboard</a></li>
            <li><a href="/settings">Settings</a></li>
            <li><a href="/calibrate">Calibration</a></li>
            <li><a href="/data">Data</a></li>
        </ul>
    </div>

    <div class="header">
        <button class="hamburger" onclick="toggleMenu()">&#9776;</button>
        <div class="title">Project SeaSense</div>
    </div>

    <div class="container">
        <div class="measure-bar">
            <span class="countdown" id="countdownLabel">Next pump &amp; measurement starts in --:--</span>
            <button class="toggle-btn" id="toggleBtn" onclick="toggleContinuous()">Continuous: OFF</button>
        </div>
        <div class="upload-bar" id="uploadBar">
            <span id="uploadStateSpan" class="up-state">--</span>
            <span class="up-sep">&middot;</span>
            <span id="uploadPendingSpan">-- pending</span>
            <span class="up-sep">&middot;</span>
            <span>Last: <span id="uploadLastSpan">--</span></span>
            <span class="up-sep">&middot;</span>
            <span>Next: <span id="uploadNextSpan">--</span></span>
        </div>
        <div class="sensors-grid" id="sensors">
            <div class="status-msg">Loading sensor data...</div>
        </div>
        <div class="section-title">Environment (NMEA2000)</div>
        <div class="env-grid" id="environment">
            <div class="env-none">Waiting for data...</div>
        </div>
    </div>

    <script>
        let autoUpdate = true;
        let continuousMode = false;
        let countdownMs = null;
        let pumpPhaseLabel = '';

        function toggleMenu() {
            document.getElementById('sidebar').classList.toggle('open');
            document.getElementById('overlay').classList.toggle('show');
        }

        function closeMenu() {
            document.getElementById('sidebar').classList.remove('open');
            document.getElementById('overlay').classList.remove('show');
        }

        // Prevent clicks on sidebar from closing the menu
        document.addEventListener('DOMContentLoaded', function() {
            const sidebar = document.getElementById('sidebar');
            if (sidebar) {
                sidebar.addEventListener('click', function(e) { e.stopPropagation(); });
            }
        });

        // Countdown ticker — runs every 100ms locally to avoid excess requests
        setInterval(function() {
            const label = document.getElementById('countdownLabel');
            if (!label) return;
            if (pumpPhaseLabel) { label.textContent = pumpPhaseLabel; return; }
            if (continuousMode) { label.textContent = 'Measuring every 2s'; return; }
            if (countdownMs === null) return;
            countdownMs = Math.max(0, countdownMs - 100);
            const s = Math.floor(countdownMs / 1000);
            const m = Math.floor(s / 60);
            label.textContent = 'Next pump & measurement starts in ' + m + ':' + String(s % 60).padStart(2, '0');
        }, 100);

        let _pollTimer = null;
        function _startPolling(intervalMs) {
            if (_pollTimer) clearInterval(_pollTimer);
            _pollTimer = setInterval(() => { if (autoUpdate) { update(); updateEnv(); updateMeasurement(); } }, intervalMs);
        }

        function updateMeasurement() {
            fetch('/api/measurement')
                .then(r => r.json())
                .then(d => {
                    const wasContinuous = continuousMode;
                    continuousMode = d.mode === 'continuous';
                    countdownMs = d.next_read_in_ms;
                    pumpPhaseLabel = d.pump_phase_label || '';
                    const btn = document.getElementById('toggleBtn');
                    if (btn) {
                        btn.textContent = continuousMode ? 'Continuous: ON' : 'Continuous: OFF';
                        btn.className = continuousMode ? 'toggle-btn active' : 'toggle-btn';
                    }
                    // Adjust polling rate when mode changes
                    if (wasContinuous !== continuousMode) {
                        _startPolling(continuousMode ? 1000 : 3000);
                        updateUploadStatus();  // Immediately refresh bar on mode change
                    }
                })
                .catch(() => {});
        }

        function toggleContinuous() {
            fetch('/api/measurement', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({mode: continuousMode ? 'normal' : 'continuous'})
            }).then(() => updateMeasurement()).catch(() => {});
        }

        function fmtMs(ms) {
            const s = Math.floor(ms / 1000);
            if (s < 60) return s + 's';
            return Math.floor(s / 60) + 'm ' + String(s % 60).padStart(2, '0') + 's';
        }

        function fmtAgo(elapsedMs) {
            const s = Math.floor(elapsedMs / 1000);
            if (s < 5) return 'just now';
            if (s < 60) return s + 's ago';
            if (s < 3600) return Math.floor(s / 60) + 'm ago';
            return Math.floor(s / 3600) + 'h ago';
        }

        function updateUploadStatus() {
            const bar = document.getElementById('uploadBar');
            if (!bar) return;
            if (continuousMode) {
                bar.innerHTML = '<span class="up-nostore">&#9888; Continuous mode &mdash; data not saved</span>';
                return;
            }
            fetch('/api/status')
                .then(r => r.json())
                .then(d => {
                    const u = d.upload || {};
                    const uptimeMs = d.uptime_ms || 0;
                    const status = u.status || '--';
                    const stateClass = status.startsWith('ERROR') ? 'err'
                        : (status === 'SUCCESS' || status === 'IDLE') ? 'ok' : 'busy';
                    const pending = (u.pending_records != null) ? u.pending_records + ' pending' : '--';
                    const lastMs = u.last_success_ms || 0;
                    const lastStr = (lastMs > 0 && uptimeMs > 0) ? fmtAgo(uptimeMs - lastMs) : 'never';
                    const nextMs = u.next_upload_ms || 0;
                    const nextStr = nextMs > 0 ? fmtMs(nextMs) : '--';
                    let html = '<span class="up-state ' + stateClass + '">' + status + '</span>'
                        + '<span class="up-sep">&middot;</span>'
                        + '<span>' + pending + '</span>'
                        + '<span class="up-sep">&middot;</span>'
                        + '<span>Last: ' + lastStr + '</span>'
                        + '<span class="up-sep">&middot;</span>'
                        + '<span>Next: ' + nextStr + '</span>';
                    if (u.retry_count > 0) {
                        html += '<span class="up-sep">&middot;</span>'
                            + '<span style="color:#c62828">Retry #' + u.retry_count + '</span>';
                    }
                    bar.innerHTML = html;
                })
                .catch(() => {});
        }

        function update() {
            fetch('/api/sensors')
                .then(r => r.json())
                .then(data => {
                    let html = '';
                    if (data.sensors && data.sensors.length > 0) {
                        data.sensors.forEach(s => {
                            // Format value based on sensor type
                            let valueFormatted;
                            if (s.type.toLowerCase().includes('temperature')) {
                                valueFormatted = s.value.toFixed(3); // 3 decimals for temperature
                            } else if (s.type.toLowerCase().includes('salinity')) {
                                valueFormatted = s.value.toFixed(2); // 2 decimals for salinity
                            } else {
                                valueFormatted = s.value.toFixed(0); // No decimals for conductivity
                            }

                            html += `<div class="sensor-card">
                                <div class="sensor-name">${s.type}</div>
                                <div class="sensor-value">
                                    ${valueFormatted}<span class="sensor-unit">${s.unit}</span>
                                </div>
                                ${s.serial ? `<div class="sensor-meta">Serial: ${s.serial}</div>` : ''}
                            </div>`;
                        });
                    } else {
                        html = '<div class="status-msg">No sensor data available</div>';
                    }
                    document.getElementById('sensors').innerHTML = html;
                })
                .catch(err => {
                    document.getElementById('sensors').innerHTML = '<div class="status-msg">Error loading sensors</div>';
                });
        }

        function envCard(label, value, unit) {
            if (value === undefined) return '';
            return `<div class="env-card"><div class="env-label">${label}</div><div class="env-value">${value}<span class="env-unit">${unit}</span></div></div>`;
        }

        function updateEnv() {
            fetch('/api/environment')
                .then(r => r.json())
                .then(d => {
                    if (!d.has_any) {
                        document.getElementById('environment').innerHTML = '<div class="env-none">No NMEA2000 data</div>';
                        return;
                    }
                    let h = '';
                    if (d.wind) {
                        h += envCard('True Wind', d.wind.speed_true, 'm/s');
                        h += envCard('Wind Angle', d.wind.angle_true, '\u00B0');
                        h += envCard('App Wind', d.wind.speed_app, 'm/s');
                        h += envCard('App Angle', d.wind.angle_app, '\u00B0');
                    }
                    if (d.water) {
                        h += envCard('Depth', d.water.depth, 'm');
                        h += envCard('Speed TW', d.water.stw, 'm/s');
                        h += envCard('Water Temp', d.water.temp_ext, '\u00B0C');
                    }
                    if (d.atmosphere) {
                        h += envCard('Air Temp', d.atmosphere.air_temp, '\u00B0C');
                        h += envCard('Pressure', d.atmosphere.pressure_hpa, 'hPa');
                        h += envCard('Humidity', d.atmosphere.humidity, '%');
                    }
                    if (d.navigation) {
                        h += envCard('COG', d.navigation.cog, '\u00B0');
                        h += envCard('SOG', d.navigation.sog, 'm/s');
                        h += envCard('Heading', d.navigation.heading, '\u00B0');
                    }
                    if (d.attitude) {
                        h += envCard('Pitch', d.attitude.pitch, '\u00B0');
                        h += envCard('Roll', d.attitude.roll, '\u00B0');
                    }
                    document.getElementById('environment').innerHTML = h || '<div class="env-none">No NMEA2000 data</div>';
                })
                .catch(() => {});
        }

        update();
        updateEnv();
        updateMeasurement();
        updateUploadStatus();
        _startPolling(3000);
        setInterval(updateUploadStatus, 10000);
    </script>
</body>
</html>
)HTML";

    _server->send(200, "text/html", html);
}

void SeaSenseWebServer::handleCalibrate() {
    String html = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Calibration - Project SeaSense</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif; background: #e8f4f8; color: #1a4d5e; }

        /* Header */
        .header { background: linear-gradient(135deg, #0a4f66 0%, #0e7fa3 100%); color: white; padding: 12px 15px; display: flex; align-items: center; box-shadow: 0 2px 8px rgba(0,0,0,0.15); position: sticky; top: 0; z-index: 100; }
        .header-left { display: flex; align-items: center; gap: 12px; }
        .hamburger { background: none; border: none; color: white; font-size: 28px; cursor: pointer; padding: 5px; line-height: 1; font-family: Arial, sans-serif; }
        .hamburger:hover { opacity: 0.8; }
        .title { font-size: 18px; font-weight: 600; white-space: nowrap; }

        /* Sidebar */
        .sidebar { position: fixed; left: -250px; top: 0; width: 250px; height: 100%; background: white; box-shadow: 2px 0 10px rgba(0,0,0,0.1); transition: left 0.3s; z-index: 201; pointer-events: auto; }
        .sidebar.open { left: 0; }
        .sidebar-header { background: #0a4f66; color: white; padding: 15px; font-weight: 600; }
        .sidebar-nav { list-style: none; }
        .sidebar-nav a { display: block; padding: 12px 20px; color: #1a4d5e; text-decoration: none; border-bottom: 1px solid #e0e0e0; transition: background 0.2s; }
        .sidebar-nav a:hover { background: #e8f4f8; }
        .sidebar-nav a.active { background: #d0e8f0; font-weight: 600; }
        .overlay { position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.5); display: none; z-index: 200; pointer-events: auto; cursor: pointer; }
        .overlay.show { display: block; }

        /* Main content */
        .container { padding: 15px; max-width: 600px; margin: 0 auto; }

        /* Cards */
        .cal-card { background: white; border-radius: 8px; padding: 20px; margin-bottom: 15px; box-shadow: 0 2px 4px rgba(0,0,0,0.08); border-left: 4px solid #0e7fa3; }
        .cal-header { font-size: 16px; font-weight: 600; color: #0a4f66; margin-bottom: 15px; text-transform: uppercase; letter-spacing: 0.5px; }
        .cal-info { background: #e8f4f8; padding: 12px; border-radius: 4px; margin-bottom: 15px; font-size: 13px; color: #1a4d5e; }
        .cal-section { margin: 15px 0; }
        .cal-section-title { font-size: 14px; font-weight: 600; color: #0a4f66; margin-bottom: 10px; }

        /* Form elements */
        .form-group { margin: 12px 0; }
        .form-group label { display: block; font-size: 13px; font-weight: 600; color: #1a4d5e; margin-bottom: 5px; }
        .form-group input, .form-group select { width: 100%; padding: 10px; border: 2px solid #d0e8f0; border-radius: 4px; font-size: 14px; transition: border 0.2s; }
        .form-group input:focus, .form-group select:focus { outline: none; border-color: #0e7fa3; }
        .form-group small { display: block; margin-top: 5px; font-size: 12px; color: #888; }

        /* Buttons */
        .btn-group { display: flex; gap: 10px; margin-top: 15px; }
        .btn { padding: 10px 20px; border: none; border-radius: 4px; font-size: 14px; font-weight: 600; cursor: pointer; transition: all 0.2s; flex: 1; }
        .btn-primary { background: #0e7fa3; color: white; }
        .btn-primary:hover { background: #0a4f66; }
        .btn-primary:disabled { background: #ccc; cursor: not-allowed; }
        .btn-secondary { background: #e0e0e0; color: #333; }
        .btn-secondary:hover { background: #d0d0d0; }
        .btn-danger { background: #f44336; color: white; flex: none; }
        .btn-danger:hover { background: #c62828; }
        .btn-sm { padding: 6px 12px; font-size: 12px; flex: none; }

        /* Status messages */
        .alert { padding: 12px; border-radius: 4px; margin: 15px 0; font-size: 13px; }
        .alert-success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
        .alert-error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
        .alert-info { background: #d1ecf1; color: #0c5460; border: 1px solid #bee5eb; }
        .hidden { display: none; }

        /* Status badge */
        .status-current { display: inline-block; padding: 4px 10px; border-radius: 12px; font-size: 12px; font-weight: 600; margin-left: 10px; }
        .status-calibrated { background: #4CAF50; color: white; }
        .status-not-calibrated { background: #F44336; color: white; }
    </style>
</head>
<body>
    <div class="overlay" id="overlay" onclick="closeMenu()"></div>

    <div class="sidebar" id="sidebar">
        <div class="sidebar-header">Menu</div>
        <ul class="sidebar-nav">
            <li><a href="/dashboard">Dashboard</a></li>
            <li><a href="/settings">Settings</a></li>
            <li><a href="/calibrate" class="active">Calibration</a></li>
            <li><a href="/data">Data</a></li>
        </ul>
    </div>

    <div class="header">
        <button class="hamburger" onclick="toggleMenu()">&#9776;</button>
        <div class="title">Project SeaSense</div>
    </div>

    <div class="container">
        <div id="alertBox" class="alert hidden"></div>

        <!-- Temperature Calibration -->
        <div class="cal-card">
            <div class="cal-header">Temperature Sensor <span class="status-current status-calibrated" id="tempStatus">Calibrated</span></div>
            <div class="cal-info">
                <strong>EZO-RTD Temperature Sensor</strong><br>
                Single-point calibration recommended. Use ice water (0&deg;C) or room temperature with accurate thermometer.
            </div>

            <div class="cal-section">
                <div class="cal-section-title">Current Reading</div>
                <div style="font-size: 24px; font-weight: 700; color: #0a4f66; margin: 10px 0;">
                    <span id="tempReading">--</span> &deg;C
                </div>
            </div>

            <div class="form-group">
                <label>Calibration Type</label>
                <select id="tempCalType">
                    <option value="clear">Clear Calibration</option>
                    <option value="single">Single Point</option>
                </select>
            </div>

            <div class="form-group" id="tempValueGroup">
                <label>Reference Temperature (&deg;C)</label>
                <input type="number" id="tempValue" step="0.1" placeholder="e.g. 0.0 for ice water">
                <small>Enter the actual temperature of your calibration solution</small>
            </div>

            <div class="btn-group">
                <button class="btn btn-secondary" onclick="readTemp()">Read Sensor</button>
                <button class="btn btn-primary" onclick="calibrateTemp()">Calibrate</button>
            </div>
        </div>

        <!-- Conductivity Calibration -->
        <div class="cal-card">
            <div class="cal-header">Conductivity Sensor <span class="status-current status-calibrated" id="ecStatus">Calibrated</span></div>
            <div class="cal-info">
                <strong>EZO-EC Conductivity Sensor</strong><br>
                Multi-point calibration recommended for best accuracy. Use standard calibration solutions (e.g. 1413 &micro;S/cm, 12880 &micro;S/cm).
            </div>

            <div class="cal-section">
                <div class="cal-section-title">Current Reading</div>
                <div style="font-size: 24px; font-weight: 700; color: #0a4f66; margin: 10px 0;">
                    <span id="ecReading">--</span> &micro;S/cm
                </div>
            </div>

            <div class="form-group">
                <label>Calibration Type</label>
                <select id="ecCalType">
                    <option value="clear">Clear Calibration</option>
                    <option value="dry">Dry Calibration</option>
                    <option value="single">Single Point</option>
                    <option value="low">Two-Point (Low)</option>
                    <option value="high">Two-Point (High)</option>
                </select>
                <small>For two-point: calibrate low point first, then high point</small>
            </div>

            <div class="form-group" id="ecValueGroup">
                <label>Reference Conductivity (&micro;S/cm)</label>
                <input type="number" id="ecValue" step="1" placeholder="e.g. 1413">
                <small>Enter the value from your calibration solution bottle</small>
            </div>

            <div class="btn-group">
                <button class="btn btn-secondary" onclick="readEC()">Read Sensor</button>
                <button class="btn btn-primary" onclick="calibrateEC()">Calibrate</button>
            </div>
        </div>
    </div>

    <script>
        function toggleMenu() {
            document.getElementById('sidebar').classList.toggle('open');
            document.getElementById('overlay').classList.toggle('show');
        }

        function closeMenu() {
            document.getElementById('sidebar').classList.remove('open');
            document.getElementById('overlay').classList.remove('show');
        }

        // Prevent clicks on sidebar from closing the menu
        document.addEventListener('DOMContentLoaded', function() {
            const sidebar = document.getElementById('sidebar');
            if (sidebar) {
                sidebar.addEventListener('click', function(e) {
                    e.stopPropagation();
                });
            }
        });

        function showAlert(message, type) {
            const alert = document.getElementById('alertBox');
            alert.className = 'alert alert-' + type;
            alert.textContent = message;
            setTimeout(() => alert.classList.add('hidden'), 5000);
        }

        function updateReadings() {
            fetch('/api/sensors')
                .then(r => r.json())
                .then(data => {
                    if (!data.sensors) return;
                    data.sensors.forEach(s => {
                        const t = s.type.toLowerCase();
                        if (t.includes('temperature')) {
                            document.getElementById('tempReading').textContent = s.value.toFixed(3);
                        } else if (t.includes('conductivity')) {
                            document.getElementById('ecReading').textContent = s.value.toFixed(0);
                        }
                    });
                })
                .catch(() => {});
        }

        function readTemp() {
            fetch('/api/sensor/reading?type=temperature')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('tempReading').textContent = data.value.toFixed(3);
                })
                .catch(err => showAlert('Error reading temperature sensor', 'error'));
        }

        function readEC() {
            fetch('/api/sensor/reading?type=conductivity')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('ecReading').textContent = data.value.toFixed(0);
                })
                .catch(err => showAlert('Error reading conductivity sensor', 'error'));
        }

        updateReadings();
        setInterval(updateReadings, 3000);

        function calibrateTemp() {
            const type = document.getElementById('tempCalType').value;
            const value = parseFloat(document.getElementById('tempValue').value);

            if (type !== 'clear' && !value && value !== 0) {
                showAlert('Please enter a reference temperature value', 'error');
                return;
            }

            const data = {
                sensor: 'temperature',
                type: type,
                value: value || 0
            };

            fetch('/api/calibrate', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(data)
            })
            .then(r => r.json())
            .then(result => {
                if (result.success) {
                    showAlert('Temperature calibration successful!', 'success');
                    setTimeout(readTemp, 1000);
                } else {
                    showAlert('Calibration failed: ' + (result.error || 'Unknown error'), 'error');
                }
            })
            .catch(err => showAlert('Error during calibration', 'error'));
        }

        function calibrateEC() {
            const type = document.getElementById('ecCalType').value;
            const value = parseFloat(document.getElementById('ecValue').value);

            if (type !== 'clear' && type !== 'dry' && !value && value !== 0) {
                showAlert('Please enter a reference conductivity value', 'error');
                return;
            }

            const data = {
                sensor: 'conductivity',
                type: type,
                value: value || 0
            };

            fetch('/api/calibrate', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(data)
            })
            .then(r => r.json())
            .then(result => {
                if (result.success) {
                    showAlert('Conductivity calibration successful!', 'success');
                    setTimeout(readEC, 1000);
                } else {
                    showAlert('Calibration failed: ' + (result.error || 'Unknown error'), 'error');
                }
            })
            .catch(err => showAlert('Error during calibration', 'error'));
        }

        // Toggle value input visibility based on calibration type
        document.getElementById('tempCalType').addEventListener('change', function() {
            document.getElementById('tempValueGroup').style.display = this.value === 'clear' ? 'none' : 'block';
        });

        document.getElementById('ecCalType').addEventListener('change', function() {
            document.getElementById('ecValueGroup').style.display = (this.value === 'clear' || this.value === 'dry') ? 'none' : 'block';
        });

        // Initial read
        readTemp();
        readEC();
    </script>
</body>
</html>
)HTML";

    _server->send(200, "text/html", html);
}

void SeaSenseWebServer::handleData() {
    String html = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Data - Project SeaSense</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif; background: #e8f4f8; color: #1a4d5e; }
        .header { background: linear-gradient(135deg, #0a4f66 0%, #0e7fa3 100%); color: white; padding: 12px 15px; display: flex; align-items: center; box-shadow: 0 2px 8px rgba(0,0,0,0.15); position: sticky; top: 0; z-index: 100; }
        .hamburger { background: none; border: none; color: white; font-size: 28px; cursor: pointer; padding: 5px; line-height: 1; }
        .hamburger:hover { opacity: 0.8; }
        .title { font-size: 18px; font-weight: 600; margin-left: 12px; }
        .sidebar { position: fixed; left: -250px; top: 0; width: 250px; height: 100%; background: white; box-shadow: 2px 0 10px rgba(0,0,0,0.1); transition: left 0.3s; z-index: 201; }
        .sidebar.open { left: 0; }
        .sidebar-header { background: #0a4f66; color: white; padding: 15px; font-weight: 600; }
        .sidebar-nav { list-style: none; }
        .sidebar-nav a { display: block; padding: 12px 20px; color: #1a4d5e; text-decoration: none; border-bottom: 1px solid #e0e0e0; }
        .sidebar-nav a:hover { background: #e8f4f8; }
        .sidebar-nav a.active { background: #d0e8f0; font-weight: 600; }
        .overlay { position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.5); display: none; z-index: 200; cursor: pointer; }
        .overlay.show { display: block; }
        .container { padding: 15px; max-width: 700px; margin: 0 auto; }
        .card { background: white; border-radius: 8px; padding: 16px; margin-bottom: 14px; box-shadow: 0 2px 4px rgba(0,0,0,0.08); }
        .card-title { font-size: 13px; font-weight: 700; color: #0a4f66; text-transform: uppercase; letter-spacing: 0.8px; margin-bottom: 12px; display: flex; align-items: center; justify-content: space-between; }
        .stat-row { display: flex; flex-wrap: wrap; gap: 12px; margin-bottom: 10px; }
        .stat { flex: 1; min-width: 100px; }
        .stat-label { font-size: 11px; color: #888; text-transform: uppercase; letter-spacing: 0.5px; }
        .stat-value { font-size: 20px; font-weight: 700; color: #1a4d5e; }
        .stat-sub { font-size: 11px; color: #aaa; }
        .progress-bar { height: 6px; background: #e0e8f0; border-radius: 3px; margin: 4px 0 8px; overflow: hidden; }
        .progress-fill { height: 100%; background: #0e7fa3; border-radius: 3px; transition: width 0.4s; }
        .progress-fill.warn { background: #ff9800; }
        .progress-fill.danger { background: #f44336; }
        .badge { display: inline-block; padding: 2px 8px; border-radius: 10px; font-size: 11px; font-weight: 700; }
        .badge-ok { background: #e8f5e9; color: #2e7d32; }
        .badge-err { background: #ffebee; color: #c62828; }
        .badge-idle { background: #f5f5f5; color: #666; }
        .badge-busy { background: #fff3e0; color: #e65100; }
        .btn { padding: 8px 16px; border: none; border-radius: 5px; font-size: 13px; font-weight: 600; cursor: pointer; transition: all 0.2s; }
        .btn-primary { background: #0e7fa3; color: white; }
        .btn-primary:hover { background: #0a4f66; }
        .btn-primary:disabled { background: #b0c8d4; cursor: not-allowed; }
        .btn-danger { background: #f44336; color: white; }
        .btn-danger:hover { background: #c62828; }
        .btn-sm { padding: 5px 10px; font-size: 12px; }
        .btn-outline { background: white; border: 1.5px solid #0e7fa3; color: #0e7fa3; }
        .btn-outline:hover { background: #e8f4f8; }
        table { width: 100%; border-collapse: collapse; font-size: 13px; }
        th { text-align: left; padding: 6px 8px; border-bottom: 2px solid #d0e8f0; font-size: 11px; text-transform: uppercase; color: #888; letter-spacing: 0.5px; }
        td { padding: 7px 8px; border-bottom: 1px solid #f0f0f0; }
        tr:last-child td { border-bottom: none; }
        tr:hover td { background: #f8fbfd; }
        .empty-row { text-align: center; color: #bbb; padding: 20px; font-size: 13px; }
        .pagination { display: flex; align-items: center; gap: 8px; justify-content: flex-end; margin-top: 10px; }
        .page-info { font-size: 12px; color: #888; }
        .danger-zone { border: 2px solid #ffcdd2; background: #fff8f8; }
        .danger-zone .card-title { color: #c62828; }
        .confirm-box { display: none; background: #ffebee; border-radius: 6px; padding: 12px; margin-top: 10px; font-size: 13px; color: #c62828; }
        .confirm-box.show { display: block; }
        .confirm-actions { display: flex; gap: 8px; margin-top: 10px; }
        .alert { padding: 10px 14px; border-radius: 5px; font-size: 13px; margin-bottom: 10px; display: none; }
        .alert.show { display: block; }
        .alert-success { background: #e8f5e9; color: #2e7d32; }
        .alert-error { background: #ffebee; color: #c62828; }
        .type-temp { color: #e65100; }
        .type-ec   { color: #1565c0; }
        .type-ph   { color: #558b2f; }
        .type-do   { color: #6a1b9a; }
    </style>
</head>
<body>
    <div class="overlay" id="overlay" onclick="closeMenu()"></div>
    <div class="sidebar" id="sidebar">
        <div class="sidebar-header">Menu</div>
        <ul class="sidebar-nav">
            <li><a href="/dashboard">Dashboard</a></li>
            <li><a href="/settings">Settings</a></li>
            <li><a href="/calibrate">Calibration</a></li>
            <li><a href="/data" class="active">Data</a></li>
        </ul>
    </div>
    <div class="header">
        <button class="hamburger" onclick="toggleMenu()">&#9776;</button>
        <div class="title">Data &amp; Uploads</div>
    </div>

    <div class="container">
        <div id="globalAlert" class="alert"></div>

        <!-- Storage Stats -->
        <div class="card">
            <div class="card-title">Storage <button class="btn btn-sm btn-outline" onclick="loadStats()">Refresh</button></div>
            <div class="stat-row" id="statsRow">
                <div class="stat"><div class="stat-label">Records</div><div class="stat-value" id="statRecords">--</div><div class="stat-sub" id="statPending">-- pending upload</div></div>
                <div class="stat"><div class="stat-label">SPIFFS Used</div><div class="stat-value" id="statSpiffs">--</div><div class="progress-bar"><div class="progress-fill" id="spiffsBar" style="width:0%"></div></div></div>
                <div class="stat"><div class="stat-label">SD Card</div><div class="stat-value" id="statSD">--</div><div class="progress-bar"><div class="progress-fill" id="sdBar" style="width:0%"></div></div></div>
            </div>
        </div>

        <!-- Upload Control -->
        <div class="card">
            <div class="card-title">Upload Control</div>
            <div class="stat-row">
                <div class="stat"><div class="stat-label">Status</div><div class="stat-value" style="font-size:15px;padding-top:3px;" id="upStatus"><span class="badge badge-idle">--</span></div></div>
                <div class="stat"><div class="stat-label">Pending</div><div class="stat-value" id="upPending">--</div><div class="stat-sub">records</div></div>
                <div class="stat"><div class="stat-label">Last Upload</div><div class="stat-value" style="font-size:14px;padding-top:4px;" id="upLast">--</div></div>
                <div class="stat"><div class="stat-label">Next Upload</div><div class="stat-value" style="font-size:14px;padding-top:4px;" id="upNext">--</div></div>
                <div class="stat"><div class="stat-label">Session Bandwidth</div><div class="stat-value" id="upBandwidth">--</div><div class="stat-sub">this session</div></div>
            </div>
            <button class="btn btn-primary" id="forceBtn" onclick="forceUpload()">Force Upload Now</button>
        </div>

        <!-- Upload History -->
        <div class="card">
            <div class="card-title">Upload History <button class="btn btn-sm btn-outline" onclick="loadHistory()">Refresh</button></div>
            <table>
                <thead><tr><th>Time</th><th>Result</th><th>Records</th><th>Size</th><th>Duration</th></tr></thead>
                <tbody id="historyBody"><tr><td colspan="5" class="empty-row">Loading...</td></tr></tbody>
            </table>
        </div>

        <!-- Data Records -->
        <div class="card">
            <div class="card-title">Stored Records</div>
            <table>
                <thead><tr><th>Time</th><th>Type</th><th>Value</th><th>Quality</th></tr></thead>
                <tbody id="recordsBody"><tr><td colspan="4" class="empty-row">Loading...</td></tr></tbody>
            </table>
            <div class="pagination">
                <span class="page-info" id="pageInfo">Page 1</span>
                <button class="btn btn-sm btn-outline" id="prevBtn" onclick="changePage(-1)" disabled>&#8592; Older</button>
                <button class="btn btn-sm btn-outline" id="nextBtn" onclick="changePage(1)" disabled>Newer &#8594;</button>
            </div>
        </div>

        <!-- Danger Zone -->
        <div class="card danger-zone">
            <div class="card-title">Danger Zone</div>
            <p style="font-size:13px;color:#555;margin-bottom:12px;">Permanently delete all stored sensor records from SPIFFS and SD card. This cannot be undone.</p>
            <button class="btn btn-danger" onclick="showFlushConfirm()">Flush All Data</button>
            <div class="confirm-box" id="confirmBox">
                <strong>Are you sure?</strong> This will delete all <span id="confirmCount">--</span> records permanently.
                <div class="confirm-actions">
                    <button class="btn btn-danger" onclick="confirmFlush()">Yes, Delete Everything</button>
                    <button class="btn btn-outline" onclick="hideFlushConfirm()">Cancel</button>
                </div>
            </div>
        </div>
    </div>

    <script>
        let currentPage = 0;
        const PAGE_SIZE = 20;
        let totalRecords = 0;
        let uptimeMs = 0;

        function toggleMenu() { document.getElementById('sidebar').classList.toggle('open'); document.getElementById('overlay').classList.toggle('show'); }
        function closeMenu()  { document.getElementById('sidebar').classList.remove('open'); document.getElementById('overlay').classList.remove('show'); }
        document.addEventListener('DOMContentLoaded', () => { document.getElementById('sidebar').addEventListener('click', e => e.stopPropagation()); });

        function fmtBytes(b) {
            if (b < 1024) return b + ' B';
            if (b < 1048576) return (b/1024).toFixed(1) + ' KB';
            return (b/1048576).toFixed(2) + ' MB';
        }
        function fmtAgo(elapsedMs) {
            const s = Math.floor(elapsedMs / 1000);
            if (s < 5)    return 'just now';
            if (s < 60)   return s + 's ago';
            if (s < 3600) return Math.floor(s/60) + 'm ago';
            return Math.floor(s/3600) + 'h ago';
        }
        function fmtMs(ms) {
            const s = Math.floor(ms/1000);
            if (s < 60) return s + 's';
            return Math.floor(s/60) + 'm ' + String(s%60).padStart(2,'0') + 's';
        }
        function fmtDur(ms) { return ms < 1000 ? ms + 'ms' : (ms/1000).toFixed(1) + 's'; }
        function typeClass(t) {
            t = (t||'').toLowerCase();
            if (t.includes('temp')) return 'type-temp';
            if (t.includes('cond')) return 'type-ec';
            if (t.includes('ph'))   return 'type-ph';
            if (t.includes('oxy'))  return 'type-do';
            return '';
        }
        function fmtValue(v, t) {
            t = (t||'').toLowerCase();
            if (t.includes('temp')) return v.toFixed(3);
            if (t.includes('salin')) return v.toFixed(2);
            return v.toFixed(0);
        }
        function showAlert(msg, type) {
            const el = document.getElementById('globalAlert');
            el.className = 'alert alert-' + type + ' show';
            el.textContent = msg;
            setTimeout(() => el.className = 'alert', 4000);
        }

        function loadStats() {
            fetch('/api/status')
                .then(r => r.json())
                .then(d => {
                    uptimeMs = d.uptime_ms || 0;
                    const u = d.upload || {};
                    totalRecords = u.pending_records != null ? u.pending_records : 0;

                    document.getElementById('statRecords').textContent = '--';
                    document.getElementById('statPending').textContent = (u.pending_records != null ? u.pending_records : '--') + ' pending upload';

                    const status = u.status || '--';
                    const cls = status.startsWith('ERROR') ? 'badge-err' : (status === 'Success' || status === 'Idle' || status === 'No data') ? 'badge-ok' : 'badge-busy';
                    document.getElementById('upStatus').innerHTML = '<span class="badge ' + cls + '">' + status + '</span>';
                    document.getElementById('upPending').textContent = u.pending_records != null ? u.pending_records : '--';

                    const lastMs = u.last_success_ms || 0;
                    document.getElementById('upLast').textContent = (lastMs > 0 && uptimeMs > 0) ? fmtAgo(uptimeMs - lastMs) : 'never';
                    const nextMs = u.next_upload_ms || 0;
                    document.getElementById('upNext').textContent = nextMs > 0 ? fmtMs(nextMs) : '--';
                })
                .catch(() => {});
            // Get storage stats separately
            fetch('/api/data/list')
                .then(r => r.json())
                .then(d => {
                    document.getElementById('statRecords').textContent = d.totalRecords || 0;
                    totalRecords = d.totalRecords || 0;
                    document.getElementById('confirmCount').textContent = totalRecords;
                    const spPct = d.totalBytes > 0 ? Math.min(100, Math.round(d.usedBytes * 100 / d.totalBytes)) : 0;
                    document.getElementById('statSpiffs').textContent = fmtBytes(d.usedBytes || 0) + ' / ' + fmtBytes(d.totalBytes || 0);
                    const bar = document.getElementById('spiffsBar');
                    bar.style.width = spPct + '%';
                    bar.className = 'progress-fill' + (spPct > 90 ? ' danger' : spPct > 70 ? ' warn' : '');
                    document.getElementById('statSD').textContent = 'N/A';
                    document.getElementById('sdBar').style.width = '0%';
                })
                .catch(() => {});
        }

        function loadHistory() {
            fetch('/api/upload/history')
                .then(r => r.json())
                .then(d => {
                    const bandwidth = d.total_bytes_sent || 0;
                    document.getElementById('upBandwidth').textContent = fmtBytes(bandwidth);
                    const tbody = document.getElementById('historyBody');
                    if (!d.history || d.history.length === 0) {
                        tbody.innerHTML = '<tr><td colspan="5" class="empty-row">No uploads yet this session</td></tr>';
                        return;
                    }
                    tbody.innerHTML = d.history.map(e => {
                        const cls = e.success ? 'badge-ok' : 'badge-err';
                        const lbl = e.success ? 'OK' : 'FAIL';
                        const time = (e.start_ms > 0 && uptimeMs > 0) ? fmtAgo(uptimeMs - e.start_ms) : (e.start_ms / 1000).toFixed(0) + 's uptime';
                        return '<tr><td>' + time + '</td>'
                            + '<td><span class="badge ' + cls + '">' + lbl + '</span></td>'
                            + '<td>' + (e.record_count || 0) + '</td>'
                            + '<td>' + fmtBytes(e.payload_bytes || 0) + '</td>'
                            + '<td>' + fmtDur(e.duration_ms || 0) + '</td></tr>';
                    }).join('');
                })
                .catch(() => { document.getElementById('historyBody').innerHTML = '<tr><td colspan="5" class="empty-row">Error loading history</td></tr>'; });
        }

        function loadRecords() {
            const tbody = document.getElementById('recordsBody');
            tbody.innerHTML = '<tr><td colspan="4" class="empty-row">Loading...</td></tr>';
            fetch('/api/data/records?page=' + currentPage + '&limit=' + PAGE_SIZE)
                .then(r => r.json())
                .then(d => {
                    if (!d.records || d.records.length === 0) {
                        tbody.innerHTML = '<tr><td colspan="4" class="empty-row">No records stored yet</td></tr>';
                    } else {
                        tbody.innerHTML = d.records.map(r => {
                            const tc = typeClass(r.type);
                            let timeStr;
                            if (r.time) {
                                timeStr = r.time;
                            } else if (uptimeMs > 0 && r.millis > 0) {
                                timeStr = fmtAgo(uptimeMs - r.millis);
                            } else {
                                const s = Math.floor((r.millis || 0) / 1000);
                                timeStr = 'Boot +' + Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
                            }
                            return '<tr><td style="font-size:11px;color:#888;">' + timeStr + '</td>'
                                + '<td class="' + tc + '">' + r.type + '</td>'
                                + '<td>' + fmtValue(r.value, r.type) + ' <span style="color:#aaa;font-size:11px;">' + r.unit + '</span></td>'
                                + '<td style="font-size:11px;color:#888;">' + (r.quality||'--') + '</td></tr>';
                        }).join('');
                    }
                    const maxPage = Math.floor((d.total - 1) / PAGE_SIZE);
                    document.getElementById('pageInfo').textContent = 'Page ' + (currentPage + 1) + ' of ' + (maxPage + 1);
                    document.getElementById('prevBtn').disabled = currentPage >= maxPage;
                    document.getElementById('nextBtn').disabled = currentPage <= 0;
                })
                .catch(() => { tbody.innerHTML = '<tr><td colspan="4" class="empty-row">Error loading records</td></tr>'; });
        }

        function changePage(dir) {
            currentPage = Math.max(0, currentPage - dir);  // page 0 = most recent
            loadRecords();
        }

        function forceUpload() {
            const btn = document.getElementById('forceBtn');
            btn.disabled = true; btn.textContent = 'Scheduling...';
            fetch('/api/upload/force', { method: 'POST' })
                .then(r => r.json())
                .then(d => {
                    showAlert('Upload scheduled — check history in a moment', 'success');
                    btn.textContent = 'Force Upload Now';
                    btn.disabled = false;
                    setTimeout(() => { loadStats(); loadHistory(); }, 3000);
                })
                .catch(() => { btn.textContent = 'Force Upload Now'; btn.disabled = false; showAlert('Request failed', 'error'); });
        }

        function showFlushConfirm() {
            document.getElementById('confirmCount').textContent = totalRecords;
            document.getElementById('confirmBox').classList.add('show');
        }
        function hideFlushConfirm() { document.getElementById('confirmBox').classList.remove('show'); }
        function confirmFlush() {
            fetch('/api/data/clear', { method: 'POST' })
                .then(r => r.json())
                .then(d => {
                    hideFlushConfirm();
                    showAlert('All data flushed successfully', 'success');
                    currentPage = 0;
                    setTimeout(() => { loadStats(); loadRecords(); }, 500);
                })
                .catch(() => { showAlert('Flush failed', 'error'); });
        }

        loadStats();
        loadHistory();
        loadRecords();
        setInterval(loadStats, 15000);
    </script>
</body>
</html>
)HTML";
    _server->send(200, "text/html", html);
}

void SeaSenseWebServer::handleSettings() {
    String html = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Settings - SeaSense Logger</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif; background: #e8f4f8; color: #1a4d5e; }

        /* Header */
        .header { background: linear-gradient(135deg, #0a4f66 0%, #0e7fa3 100%); color: white; padding: 12px 15px; display: flex; align-items: center; box-shadow: 0 2px 8px rgba(0,0,0,0.15); position: sticky; top: 0; z-index: 100; }
        .hamburger { background: none; border: none; color: white; font-size: 28px; cursor: pointer; padding: 5px; line-height: 1; font-family: Arial, sans-serif; }
        .hamburger:hover { opacity: 0.8; }
        .title { font-size: 18px; font-weight: 600; white-space: nowrap; }

        /* Sidebar */
        .sidebar { position: fixed; left: -250px; top: 0; width: 250px; height: 100%; background: white; box-shadow: 2px 0 10px rgba(0,0,0,0.1); transition: left 0.3s; z-index: 201; pointer-events: auto; }
        .sidebar.open { left: 0; }
        .sidebar-header { background: #0a4f66; color: white; padding: 15px; font-weight: 600; }
        .sidebar-nav { list-style: none; }
        .sidebar-nav a { display: block; padding: 12px 20px; color: #1a4d5e; text-decoration: none; border-bottom: 1px solid #e0e0e0; transition: background 0.2s; }
        .sidebar-nav a:hover { background: #e8f4f8; }
        .sidebar-nav a.active { background: #d0e8f0; font-weight: 600; }
        .overlay { position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.5); display: none; z-index: 200; pointer-events: auto; cursor: pointer; }
        .overlay.show { display: block; }

        /* Main content */
        .container { padding: 15px; max-width: 600px; margin: 0 auto; }

        /* Sections */
        .section { background: white; padding: 20px; margin: 15px 0; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.08); border-left: 4px solid #0e7fa3; }
        .section h2 { margin-top: 0; color: #0a4f66; border-bottom: 2px solid #0e7fa3; padding-bottom: 10px; font-size: 16px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; }
        .form-group { margin: 15px 0; }
        .form-group label { display: block; font-weight: 600; margin-bottom: 5px; color: #1a4d5e; font-size: 13px; }
        .form-group input, .form-group select { width: 100%; padding: 10px; border: 2px solid #d0e8f0; border-radius: 4px; box-sizing: border-box; font-size: 14px; transition: border 0.2s; }
        .form-group input:focus, .form-group select:focus { outline: none; border-color: #0e7fa3; }
        .form-group input[type="checkbox"] { width: auto; }
        .form-group small { color: #888; font-size: 12px; display: block; margin-top: 5px; }

        /* Buttons */
        .button { background: #0e7fa3; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; font-size: 14px; font-weight: 600; margin: 5px; transition: all 0.2s; }
        .button:hover { background: #0a4f66; }
        .button-danger { background: #d32f2f; }
        .button-danger:hover { background: #b71c1c; }
        .button-warning { background: #f57c00; }
        .button-warning:hover { background: #e65100; }

        /* Toast notifications */
        .toast { position: fixed; top: 70px; right: 20px; padding: 15px 20px; border-radius: 4px; color: white; display: none; z-index: 1000; box-shadow: 0 4px 8px rgba(0,0,0,0.2); }
        .toast-success { background: #4CAF50; }
        .toast-error { background: #F44336; }
        .toast-info { background: #0e7fa3; }

        .actions { text-align: center; margin-top: 20px; }
    </style>
</head>
<body>
    <div class="overlay" id="overlay" onclick="closeMenu()"></div>

    <div class="sidebar" id="sidebar">
        <div class="sidebar-header">Menu</div>
        <ul class="sidebar-nav">
            <li><a href="/dashboard">Dashboard</a></li>
            <li><a href="/settings" class="active">Settings</a></li>
            <li><a href="/calibrate">Calibration</a></li>
            <li><a href="/data">Data</a></li>
        </ul>
    </div>

    <div class="header">
        <button class="hamburger" onclick="toggleMenu()">&#9776;</button>
        <div class="title">Project SeaSense</div>
    </div>

    <div id="toast" class="toast"></div>

    <div class="container">
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

            <h3>Sampling Configuration</h3>
            <div class="form-group">
                <label>Sensor Reading Interval</label>
                <div style="display:flex;gap:10px;align-items:center;">
                    <div style="display:flex;align-items:center;gap:4px;">
                        <input type="number" id="sensor-interval-min" min="0" max="1439" step="1" value="15" style="width:70px;">
                        <span style="font-size:13px;color:#666;">min</span>
                    </div>
                    <div style="display:flex;align-items:center;gap:4px;">
                        <input type="number" id="sensor-interval-sec" min="0" max="59" step="1" value="0" style="width:60px;">
                        <span style="font-size:13px;color:#666;">sec</span>
                    </div>
                </div>
                <small id="interval-hint">How often to pump and read sensors. Default: 15 min.</small>
            </div>

            <h3>GPS Source</h3>
            <div class="form-group">
                <label>Position &amp; Time Source</label>
                <select id="gps-source" name="gps-source">
                    <option value="onboard">Onboard GPS (NEO-6M)</option>
                    <option value="nmea2000">NMEA2000 Network</option>
                </select>
                <small>Select NMEA2000 if the device is installed in the bilge without sky visibility. Requires a GPS chartplotter on the NMEA2000 bus.</small>
            </div>
            <div class="form-group">
                <label style="display:flex;align-items:center;gap:8px;cursor:pointer;">
                    <input type="checkbox" id="gps-fallback" name="gps-fallback" style="width:auto;margin:0;">
                    Fall back to onboard GPS if NMEA2000 has no fix
                </label>
            </div>
        </div>

        <!-- Device Configuration -->
        <div class="section">
            <h2>Device Configuration</h2>
            <div class="form-group">
                <label>Device GUID</label>
                <div style="display:flex;gap:8px;align-items:center;">
                    <input type="text" id="device-guid" name="device-guid" style="flex:1;">
                    <button type="button" class="btn btn-sm" onclick="showRegenConfirm()" style="white-space:nowrap;">Generate New</button>
                </div>
                <div id="regenConfirm" style="display:none;margin-top:8px;padding:10px;background:#fff3cd;border:1px solid #ffc107;border-radius:6px;font-size:13px;">
                    Generating a new GUID will change the device identity. Any existing data linked to the old GUID will be orphaned. Are you sure?
                    <div style="margin-top:8px;display:flex;gap:8px;">
                        <button type="button" class="btn btn-danger btn-sm" onclick="confirmRegen()">Yes, Generate New GUID</button>
                        <button type="button" class="btn btn-sm" onclick="cancelRegen()">Cancel</button>
                    </div>
                </div>
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

        <!-- Actions -->
        <div class="actions">
            <button type="submit" class="button">Save Configuration</button>
            <button type="button" class="button button-warning" onclick="resetConfig()">Reset to Defaults</button>
            <button type="button" class="button button-danger" onclick="restartDevice()">Restart Device</button>
        </div>
        </form>
    </div>

    <script>
        function toggleMenu() {
            document.getElementById('sidebar').classList.toggle('open');
            document.getElementById('overlay').classList.toggle('show');
        }

        function closeMenu() {
            document.getElementById('sidebar').classList.remove('open');
            document.getElementById('overlay').classList.remove('show');
        }

        // Prevent clicks on sidebar from closing the menu
        document.addEventListener('DOMContentLoaded', function() {
            const sidebar = document.getElementById('sidebar');
            if (sidebar) {
                sidebar.addEventListener('click', function(e) {
                    e.stopPropagation();
                });
            }
        });

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

                // Sampling
                if (config.sampling) {
                    const ms = config.sampling.sensor_interval_ms || 900000;
                    document.getElementById('sensor-interval-min').value = Math.floor(ms / 60000);
                    document.getElementById('sensor-interval-sec').value = Math.round((ms % 60000) / 1000);

                    // Show minimum derived from pump cycle
                    const minMs = config.sampling.min_sampling_ms || 5000;
                    const minMin = Math.floor(minMs / 60000);
                    const minSec = Math.round((minMs % 60000) / 1000);
                    const minStr = minMin > 0
                        ? (minMin + 'm ' + (minSec > 0 ? minSec + 's' : '')).trim()
                        : minSec + 's';
                    const hint = document.getElementById('interval-hint');
                    if (hint) hint.textContent = 'Minimum: ' + minStr + ' (full pump cycle). Default: 15 min.';

                    // Enforce minimum on the minutes input
                    document.getElementById('sensor-interval-min').min = minMin;
                    if (minMin === 0) {
                        document.getElementById('sensor-interval-sec').min = minSec;
                    }
                }

                // GPS source
                if (config.gps) {
                    document.getElementById('gps-source').value = config.gps.use_nmea2000 ? 'nmea2000' : 'onboard';
                    document.getElementById('gps-fallback').checked = config.gps.fallback_to_onboard !== false;
                }

                // Device
                document.getElementById('device-guid').value = config.device.device_guid || '';
                document.getElementById('partner-id').value = config.device.partner_id || '';
                document.getElementById('firmware-version').value = config.device.firmware_version || '';

            } catch (e) {
                showToast('Failed to load configuration: ' + e.message, 'error');
            }
        }

        async function saveConfig(event) {
            event.preventDefault();

            // Clamp interval to minimum (backend also clamps, but give immediate feedback)
            const minMs = parseInt(document.getElementById('sensor-interval-min').min || 0) * 60000
                + parseInt(document.getElementById('sensor-interval-sec').min || 0) * 1000;
            const enteredMs = (parseInt(document.getElementById('sensor-interval-min').value || 0) * 60
                + parseInt(document.getElementById('sensor-interval-sec').value || 0)) * 1000;
            if (minMs > 0 && enteredMs < minMs) {
                document.getElementById('sensor-interval-min').value = Math.floor(minMs / 60000);
                document.getElementById('sensor-interval-sec').value = Math.round((minMs % 60000) / 1000);
                showToast('Interval raised to minimum pump cycle duration.', 'error');
                return;
            }

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
                sampling: {
                    sensor_interval_ms: (parseInt(document.getElementById('sensor-interval-min').value || 0) * 60
                        + parseInt(document.getElementById('sensor-interval-sec').value || 0)) * 1000
                },
                gps: {
                    use_nmea2000: document.getElementById('gps-source').value === 'nmea2000',
                    fallback_to_onboard: document.getElementById('gps-fallback').checked
                },
                device: {
                    device_guid: document.getElementById('device-guid').value,
                    partner_id: document.getElementById('partner-id').value,
                    firmware_version: document.getElementById('firmware-version').value
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
                    const min = parseInt(document.getElementById('sensor-interval-min').value || 0);
                    const sec = parseInt(document.getElementById('sensor-interval-sec').value || 0);
                    const intervalStr = min > 0 ? (min + 'm ' + (sec > 0 ? sec + 's' : '')).trim() : sec + 's';
                    showToast('Saved. Sampling interval: ' + intervalStr + '. Dashboard updated.', 'success');
                } else {
                    showToast('Error: ' + (result.error || 'Unknown error'), 'error');
                }
            } catch (e) {
                showToast('Network error: ' + e.message, 'error');
            }
        }

        function showRegenConfirm() {
            document.getElementById('regenConfirm').style.display = 'block';
        }
        function cancelRegen() {
            document.getElementById('regenConfirm').style.display = 'none';
        }
        async function confirmRegen() {
            cancelRegen();
            try {
                const response = await fetch('/api/device/regenerate-guid', {method: 'POST'});
                const result = await response.json();
                if (response.ok && result.device_guid) {
                    document.getElementById('device-guid').value = result.device_guid;
                    showToast('New GUID generated and saved', 'success');
                } else {
                    showToast('Error generating GUID', 'error');
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

void SeaSenseWebServer::handleApiSensorRead() {
    if (_server->method() != HTTP_POST) {
        sendError("Method not allowed", 405);
        return;
    }

    // Force read both sensors
    bool tempSuccess = false;
    bool ecSuccess = false;

    if (_tempSensor && _tempSensor->isEnabled()) {
        tempSuccess = _tempSensor->read();

        // Set temperature compensation for EC sensor
        if (tempSuccess) {
            SensorData tempData = _tempSensor->getData();
            _ecSensor->setTemperatureCompensation(tempData.value);
        }
    }

    if (_ecSensor && _ecSensor->isEnabled()) {
        ecSuccess = _ecSensor->read();
    }

    sendJSON("{\"success\":true,\"temperature\":" + String(tempSuccess ? "true" : "false") + ",\"conductivity\":" + String(ecSuccess ? "true" : "false") + "}");
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
    JsonDocument doc;
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

    JsonDocument doc;

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

    JsonDocument doc;
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

void SeaSenseWebServer::handleApiDataRecords() {
    uint16_t limit = 20;
    uint16_t page  = 0;
    if (_server->hasArg("limit")) { limit = (uint16_t)_server->arg("limit").toInt(); if (limit > 50) limit = 50; }
    if (_server->hasArg("page"))  { page  = (uint16_t)_server->arg("page").toInt(); }

    StorageStats stats = _storage->getStats();
    uint32_t total = stats.totalRecords;

    // Read enough records to cover this page (most-recent-first)
    uint16_t needed = (page + 1) * limit;
    if (needed > 500) needed = 500;
    std::vector<DataRecord> recs = _storage->readRecords(0, needed);

    JsonDocument doc;
    doc["total"]  = total;
    doc["page"]   = page;
    doc["limit"]  = limit;
    JsonArray arr = doc["records"].to<JsonArray>();

    // Slice most-recent-first from tail of the read batch
    int startIdx = (int)recs.size() - 1 - (page * (int)limit);
    for (int i = startIdx; i > startIdx - (int)limit && i >= 0; i--) {
        JsonObject r = arr.add<JsonObject>();
        r["millis"]  = recs[i].millis;
        r["time"]    = recs[i].timestampUTC;  // empty string if no GPS/NTP fix yet
        r["type"]    = recs[i].sensorType;
        r["value"]   = recs[i].value;
        r["unit"]    = recs[i].unit;
        r["quality"] = recs[i].quality;
    }

    String json;
    serializeJson(doc, json);
    sendJSON(json);
}

void SeaSenseWebServer::handleApiUploadForce() {
    if (_server->method() != HTTP_POST) { sendError("POST required", 405); return; }
    extern APIUploader apiUploader;
    apiUploader.forceUpload();
    sendJSON("{\"success\":true,\"message\":\"Upload scheduled\"}");
}

void SeaSenseWebServer::handleApiUploadHistory() {
    extern APIUploader apiUploader;

    uint8_t count = 0;
    const UploadRecord* hist = apiUploader.getUploadHistory(count);
    uint8_t head = apiUploader.getHistoryHead();

    JsonDocument doc;
    doc["total_bytes_sent"] = apiUploader.getTotalBytesSent();
    JsonArray arr = doc["history"].to<JsonArray>();

    // Iterate most-recent-first: head-1, head-2, ... wrapping around
    for (uint8_t i = 0; i < count; i++) {
        uint8_t idx = (head + APIUploader::UPLOAD_HISTORY_SIZE - 1 - i) % APIUploader::UPLOAD_HISTORY_SIZE;
        JsonObject e = arr.add<JsonObject>();
        e["start_ms"]    = hist[idx].startMs;
        e["duration_ms"] = hist[idx].durationMs;
        e["success"]     = hist[idx].success;
        e["record_count"]  = hist[idx].recordCount;
        e["payload_bytes"] = hist[idx].payloadBytes;
    }

    String json;
    serializeJson(doc, json);
    sendJSON(json);
}

void SeaSenseWebServer::handleApiDeviceRegenerateGuid() {
    if (_server->method() != HTTP_POST) { sendError("POST required", 405); return; }
    if (!_configManager) { sendError("Configuration manager not available", 503); return; }
    String newGUID = _configManager->regenerateDeviceGUID();
    sendJSON("{\"device_guid\":\"" + newGUID + "\"}");
}

void SeaSenseWebServer::handleApiConfig() {
    if (!_configManager) {
        sendError("Configuration manager not available", 503);
        return;
    }

    JsonDocument doc;

    // WiFi config
    ConfigManager::WiFiConfig wifi = _configManager->getWiFiConfig();
    JsonObject wifiObj = doc["wifi"].to<JsonObject>();
    wifiObj["station_ssid"] = wifi.stationSSID;
    wifiObj["station_password"] = wifi.stationPassword;
    wifiObj["ap_password"] = wifi.apPassword;

    // API config
    ConfigManager::APIConfig api = _configManager->getAPIConfig();
    JsonObject apiObj = doc["api"].to<JsonObject>();
    apiObj["url"] = api.url;
    apiObj["api_key"] = api.apiKey;
    apiObj["upload_interval_ms"] = api.uploadInterval;
    apiObj["batch_size"] = api.batchSize;
    apiObj["max_retries"] = api.maxRetries;

    // Sampling config
    ConfigManager::SamplingConfig sampling = _configManager->getSamplingConfig();
    JsonObject samplingObj = doc["sampling"].to<JsonObject>();
    samplingObj["sensor_interval_ms"] = sampling.sensorIntervalMs;

    // Minimum sampling interval = full pump cycle duration (calculated from current pump config)
    {
        PumpConfig pc = _configManager->getPumpConfig();
        unsigned long minMs = (unsigned long)pc.pumpStartupDelayMs
            + pc.stabilityWaitMs
            + ((unsigned long)pc.measurementCount * pc.measurementIntervalMs)
            + pc.pumpStopDelayMs
            + pc.cooldownMs;
        samplingObj["min_sampling_ms"] = max(minMs, 5000UL);
    }

    // GPS config
    ConfigManager::GPSConfig gpsConfig = _configManager->getGPSConfig();
    JsonObject gpsObj = doc["gps"].to<JsonObject>();
    gpsObj["use_nmea2000"] = gpsConfig.useNMEA2000;
    gpsObj["fallback_to_onboard"] = gpsConfig.fallbackToOnboard;

    // Device config
    ConfigManager::DeviceConfig device = _configManager->getDeviceConfig();
    JsonObject deviceObj = doc["device"].to<JsonObject>();
    deviceObj["device_guid"] = device.deviceGUID;
    deviceObj["partner_id"] = device.partnerID;
    deviceObj["firmware_version"] = device.firmwareVersion;

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
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _server->arg("plain"));

    if (error) {
        sendError("Invalid JSON", 400);
        return;
    }

    // Update WiFi config
    if (doc["wifi"].is<JsonObject>()) {
        ConfigManager::WiFiConfig wifi;
        wifi.stationSSID = doc["wifi"]["station_ssid"] | "";
        wifi.stationPassword = doc["wifi"]["station_password"] | "";
        wifi.apPassword = doc["wifi"]["ap_password"] | "protectplanet!";
        _configManager->setWiFiConfig(wifi);
    }

    // Update API config
    if (doc["api"].is<JsonObject>()) {
        ConfigManager::APIConfig api;
        api.url = doc["api"]["url"] | "";
        api.apiKey = doc["api"]["api_key"] | "";
        api.uploadInterval = doc["api"]["upload_interval_ms"] | 300000;
        api.batchSize = doc["api"]["batch_size"] | 100;
        api.maxRetries = doc["api"]["max_retries"] | 5;
        _configManager->setAPIConfig(api);
    }

    // Update sampling config
    if (doc["sampling"].is<JsonObject>()) {
        // Calculate minimum from current pump config
        PumpConfig pc = _configManager->getPumpConfig();
        unsigned long minSamplingMs = (unsigned long)pc.pumpStartupDelayMs
            + pc.stabilityWaitMs
            + ((unsigned long)pc.measurementCount * pc.measurementIntervalMs)
            + pc.pumpStopDelayMs
            + pc.cooldownMs;
        minSamplingMs = max(minSamplingMs, 5000UL);

        ConfigManager::SamplingConfig sampling;
        sampling.sensorIntervalMs = doc["sampling"]["sensor_interval_ms"] | 900000UL;
        sampling.sensorIntervalMs = max(sampling.sensorIntervalMs, minSamplingMs);
        _configManager->setSamplingConfig(sampling);

        // Apply immediately: update globals so dashboard countdown reflects new interval
        extern unsigned long sensorSamplingIntervalMs;
        extern unsigned long nextSensorReadAt;
        sensorSamplingIntervalMs = sampling.sensorIntervalMs;
        nextSensorReadAt = millis() + sampling.sensorIntervalMs;  // reschedule from now
    }

    // Update GPS config
    if (doc["gps"].is<JsonObject>()) {
        ConfigManager::GPSConfig gps;
        gps.useNMEA2000 = doc["gps"]["use_nmea2000"] | false;
        gps.fallbackToOnboard = doc["gps"]["fallback_to_onboard"] | true;
        _configManager->setGPSConfig(gps);
        // Update global flag so main loop uses new source immediately
        extern bool useNMEA2000GPS;
        useNMEA2000GPS = gps.useNMEA2000;
    }

    // Update device config
    if (doc["device"].is<JsonObject>()) {
        ConfigManager::DeviceConfig device;
        device.deviceGUID = doc["device"]["device_guid"] | "";
        device.partnerID = doc["device"]["partner_id"] | "";
        device.firmwareVersion = doc["device"]["firmware_version"] | "2.0.0";
        _configManager->setDeviceConfig(device);
    }

    // Update deployment metadata (purchase_date, depth_cm — deploy_date is auto-set)
    if (doc["deployment"].is<JsonObject>()) {
        ConfigManager::DeploymentConfig dep = _configManager->getDeploymentConfig();
        if (doc["deployment"]["purchase_date"].is<const char*>()) {
            dep.purchaseDate = doc["deployment"]["purchase_date"].as<String>();
        }
        if (!doc["deployment"]["depth_cm"].isNull()) {
            dep.depthCm = doc["deployment"]["depth_cm"] | 0.0f;
        }
        // deploy_date is NOT settable via API — it's auto-stamped on first boot
        _configManager->setDeploymentConfig(dep);
    }

    // Save to SPIFFS
    if (_configManager->save()) {
        sendJSON("{\"success\":true,\"message\":\"Configuration saved. Restart device to apply WiFi and API changes.\"}");
    } else {
        sendError("Failed to save configuration", 500);
    }
}

void SeaSenseWebServer::handleApiStatus() {
    extern SystemHealth systemHealth;

    JsonDocument doc;

    doc["uptime_ms"] = millis();

    // WiFi
    doc["wifi"]["ap_ssid"] = _apSSID;
    doc["wifi"]["ap_ip"] = getAPIP();
    doc["wifi"]["station_connected"] = isWiFiConnected();
    if (isWiFiConnected()) {
        doc["wifi"]["station_ip"] = getStationIP();
        doc["wifi"]["rssi"] = WiFi.RSSI();
    }

    // Storage
    doc["storage"]["status"] = _storage->getStatusString();
    doc["storage"]["spiffs_mounted"] = _storage->isSPIFFSMounted();
    doc["storage"]["sd_mounted"] = _storage->isSDMounted();

    // System health
    doc["system"]["free_heap"] = ESP.getFreeHeap();
    doc["system"]["min_free_heap"] = ESP.getMinFreeHeap();
    doc["system"]["reset_reason"] = systemHealth.getResetReasonString();
    doc["system"]["reboot_count"] = systemHealth.getRebootCount();
    doc["system"]["consecutive_reboots"] = systemHealth.getConsecutiveReboots();
    doc["system"]["safe_mode"] = systemHealth.isInSafeMode();

    // Error counters
    doc["errors"]["sensor"] = systemHealth.getErrorCount(ErrorType::SENSOR);
    doc["errors"]["sd"] = systemHealth.getErrorCount(ErrorType::SD);
    doc["errors"]["api"] = systemHealth.getErrorCount(ErrorType::API);
    doc["errors"]["wifi"] = systemHealth.getErrorCount(ErrorType::WIFI);

    // GPS status (via extern globals from main sketch)
    extern bool activeGPSHasValidFix();
    extern GPSData activeGPSGetData();
    extern bool useNMEA2000GPS;
    doc["gps"]["has_fix"] = activeGPSHasValidFix();
    doc["gps"]["source"] = useNMEA2000GPS ? "nmea2000" : "onboard";
    if (activeGPSHasValidFix()) {
        GPSData gd = activeGPSGetData();
        doc["gps"]["satellites"] = gd.satellites;
        doc["gps"]["hdop"] = gd.hdop;
    }

    // Upload status (via extern to apiUploader)
    extern APIUploader apiUploader;
    doc["upload"]["status"] = apiUploader.getStatusString();
    doc["upload"]["pending_records"] = apiUploader.getPendingRecords();
    doc["upload"]["last_success_ms"] = apiUploader.getLastUploadTime();
    doc["upload"]["retry_count"] = apiUploader.getRetryCount();
    doc["upload"]["next_upload_ms"] = apiUploader.getTimeUntilNext();

    // Deployment metadata
    if (_configManager) {
        ConfigManager::DeploymentConfig dep = _configManager->getDeploymentConfig();
        doc["deployment"]["deploy_date"] = dep.deployDate;
        doc["deployment"]["purchase_date"] = dep.purchaseDate;
        doc["deployment"]["depth_cm"] = dep.depthCm;
    }

    String json;
    serializeJson(doc, json);
    sendJSON(json);
}

void SeaSenseWebServer::handleApiEnvironment() {
#if FEATURE_NMEA2000
    extern NMEA2000Environment n2kEnv;

    N2kEnvironmentData env = n2kEnv.getSnapshot();
    JsonDocument doc;
    doc["has_any"] = n2kEnv.hasAnyData();

    // Wind
    if (env.hasWind) {
        JsonObject wind = doc["wind"].to<JsonObject>();
        if (!isnan(env.windSpeedTrue))     wind["speed_true"] = serialized(String(env.windSpeedTrue, 1));
        if (!isnan(env.windAngleTrue))     wind["angle_true"] = serialized(String(env.windAngleTrue, 0));
        if (!isnan(env.windSpeedApparent)) wind["speed_app"] = serialized(String(env.windSpeedApparent, 1));
        if (!isnan(env.windAngleApparent)) wind["angle_app"] = serialized(String(env.windAngleApparent, 0));
    }

    // Water
    if (env.hasDepth || env.hasSpeedThroughWater || env.hasWaterTempExternal) {
        JsonObject water = doc["water"].to<JsonObject>();
        if (!isnan(env.waterDepth))        water["depth"] = serialized(String(env.waterDepth, 1));
        if (!isnan(env.speedThroughWater)) water["stw"] = serialized(String(env.speedThroughWater, 1));
        if (!isnan(env.waterTempExternal)) water["temp_ext"] = serialized(String(env.waterTempExternal, 1));
    }

    // Atmosphere
    if (env.hasAirTemp || env.hasBaroPressure || env.hasHumidity) {
        JsonObject atmo = doc["atmosphere"].to<JsonObject>();
        if (!isnan(env.airTemp))      atmo["air_temp"] = serialized(String(env.airTemp, 1));
        if (!isnan(env.baroPressure)) atmo["pressure_hpa"] = serialized(String(env.baroPressure / 100.0f, 1));
        if (!isnan(env.humidity))     atmo["humidity"] = serialized(String(env.humidity, 0));
    }

    // Navigation
    if (env.hasCOGSOG || env.hasHeading) {
        JsonObject nav = doc["navigation"].to<JsonObject>();
        if (!isnan(env.cogTrue)) nav["cog"] = serialized(String(env.cogTrue, 0));
        if (!isnan(env.sog))    nav["sog"] = serialized(String(env.sog, 1));
        if (!isnan(env.heading)) nav["heading"] = serialized(String(env.heading, 0));
    }

    // Attitude
    if (env.hasAttitude) {
        JsonObject att = doc["attitude"].to<JsonObject>();
        if (!isnan(env.pitch)) att["pitch"] = serialized(String(env.pitch, 1));
        if (!isnan(env.roll))  att["roll"] = serialized(String(env.roll, 1));
    }

    String json;
    serializeJson(doc, json);
    sendJSON(json);
#else
    sendJSON("{\"has_any\":false}");
#endif
}

void SeaSenseWebServer::handleApiPumpStatus() {
    if (!_pumpController) {
        sendError("Pump controller not available", 503);
        return;
    }

    JsonDocument doc;
    doc["enabled"] = _pumpController->isEnabled();

    // Map PumpState enum to string
    PumpState state = _pumpController->getState();
    if (state == PumpState::IDLE) {
        doc["state"] = "IDLE";
    } else if (state == PumpState::PUMP_STARTING) {
        doc["state"] = "PUMP_STARTING";
    } else if (state == PumpState::STABILIZING) {
        doc["state"] = "STABILIZING";
    } else if (state == PumpState::MEASURING) {
        doc["state"] = "MEASURING";
    } else if (state == PumpState::PUMP_STOPPING) {
        doc["state"] = "PUMP_STOPPING";
    } else if (state == PumpState::COOLDOWN) {
        doc["state"] = "COOLDOWN";
    } else if (state == PumpState::ERROR) {
        doc["state"] = "ERROR";
    } else if (state == PumpState::PAUSED) {
        doc["state"] = "PAUSED";
    }

    doc["relay_on"] = _pumpController->isRelayOn();
    doc["cycle_progress"] = _pumpController->getCycleProgress();
    doc["cycle_elapsed_ms"] = _pumpController->getCycleElapsed();

    PumpConfig config = _configManager->getPumpConfig();
    doc["cycle_interval_ms"] = config.cycleIntervalMs;

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

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _server->arg("plain"));

    if (error) {
        sendError("Invalid JSON", 400);
        return;
    }

    String action = doc["action"] | "";
    action.toLowerCase();

    if (action == "start") {
        _pumpController->startPump();
        sendJSON("{\"success\":true,\"message\":\"Pump started\"}");
    } else if (action == "stop") {
        _pumpController->stopPump();
        sendJSON("{\"success\":true,\"message\":\"Pump stopped\"}");
    } else if (action == "pause") {
        _pumpController->pause();
        sendJSON("{\"success\":true,\"message\":\"Pump paused\"}");
    } else if (action == "resume") {
        _pumpController->resume();
        sendJSON("{\"success\":true,\"message\":\"Pump resumed\"}");
    } else if (action == "enable") {
        _pumpController->setEnabled(true);
        sendJSON("{\"success\":true,\"message\":\"Pump controller enabled\"}");
    } else if (action == "disable") {
        _pumpController->setEnabled(false);
        sendJSON("{\"success\":true,\"message\":\"Pump controller disabled\"}");
    } else {
        sendError("Unknown action: " + action, 400);
    }
}

void SeaSenseWebServer::handleApiPumpConfig() {
    if (!_pumpController || !_configManager) {
        sendError("Pump controller or config manager not available", 503);
        return;
    }

    PumpConfig config = _configManager->getPumpConfig();

    JsonDocument doc;
    doc["enabled"] = config.enabled;
    doc["relay_pin"] = config.relayPin;
    doc["cycle_interval_ms"] = config.cycleIntervalMs;
    doc["startup_delay_ms"] = config.pumpStartupDelayMs;
    doc["stability_wait_ms"] = config.stabilityWaitMs;
    doc["measurement_count"] = config.measurementCount;
    doc["measurement_interval_ms"] = config.measurementIntervalMs;
    doc["stop_delay_ms"] = config.pumpStopDelayMs;
    doc["cooldown_ms"] = config.cooldownMs;
    doc["max_on_time_ms"] = config.maxPumpOnTimeMs;

    if (config.method == StabilityMethod::VARIANCE_CHECK) {
        doc["method"] = "VARIANCE_CHECK";
    } else {
        doc["method"] = "FIXED_DELAY";
    }

    doc["temp_variance_threshold"] = config.tempVarianceThreshold;
    doc["ec_variance_threshold"] = config.ecVarianceThreshold;

    String json;
    serializeJson(doc, json);
    sendJSON(json);
}

void SeaSenseWebServer::handleApiPumpConfigUpdate() {
    if (!_pumpController || !_configManager) {
        sendError("Pump controller or config manager not available", 503);
        return;
    }

    if (_server->method() != HTTP_POST) {
        sendError("Method not allowed", 405);
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _server->arg("plain"));

    if (error) {
        sendError("Invalid JSON", 400);
        return;
    }

    // Update pump config
    PumpConfig config;
    config.enabled = doc["enabled"] | true;
    config.relayPin = doc["relay_pin"] | PUMP_RELAY_PIN;
    config.cycleIntervalMs = doc["cycle_interval_ms"] | PUMP_CYCLE_INTERVAL_MS;
    config.pumpStartupDelayMs = doc["startup_delay_ms"] | PUMP_STARTUP_DELAY_MS;
    config.stabilityWaitMs = doc["stability_wait_ms"] | PUMP_STABILITY_WAIT_MS;
    config.measurementCount = doc["measurement_count"] | PUMP_MEASUREMENT_COUNT;
    config.measurementIntervalMs = doc["measurement_interval_ms"] | PUMP_MEASUREMENT_INTERVAL_MS;
    config.pumpStopDelayMs = doc["stop_delay_ms"] | PUMP_STOP_DELAY_MS;
    config.cooldownMs = doc["cooldown_ms"] | PUMP_COOLDOWN_MS;
    config.maxPumpOnTimeMs = doc["max_on_time_ms"] | PUMP_MAX_ON_TIME_MS;

    String method = doc["method"] | "FIXED_DELAY";
    if (method == "VARIANCE_CHECK") {
        config.method = StabilityMethod::VARIANCE_CHECK;
    } else {
        config.method = StabilityMethod::FIXED_DELAY;
    }

    config.tempVarianceThreshold = doc["temp_variance_threshold"] | 0.1;
    config.ecVarianceThreshold = doc["ec_variance_threshold"] | 50.0;

    _configManager->setPumpConfig(config);

    // Save to SPIFFS
    if (_configManager->save()) {
        sendJSON("{\"success\":true,\"message\":\"Pump configuration saved\"}");
    } else {
        sendError("Failed to save pump configuration", 500);
    }
}

void SeaSenseWebServer::handleApiMeasurement() {
    extern bool continuousMeasurementMode;
    extern unsigned long nextSensorReadAt;
    extern unsigned long savedNextSensorReadAt;
    extern unsigned long sensorSamplingIntervalMs;

    if (_server->method() == HTTP_POST) {
        JsonDocument req;
        if (deserializeJson(req, _server->arg("plain")) == DeserializationError::Ok) {
            String mode = req["mode"] | "";
            if (mode == "continuous" && !continuousMeasurementMode) {
                savedNextSensorReadAt = nextSensorReadAt;
                continuousMeasurementMode = true;
                nextSensorReadAt = 0;  // fire first continuous read immediately
                // Pause pump so relay doesn't fire during display-only mode
                if (_pumpController) _pumpController->pause();
            } else if (mode == "normal" && continuousMeasurementMode) {
                continuousMeasurementMode = false;
                // Reschedule timer for pump-disabled fallback (avoid immediate write on exit)
                nextSensorReadAt = millis() + sensorSamplingIntervalMs;
                // Resume pump — it will wait for cycleIntervalMs before next cycle
                if (_pumpController) _pumpController->resume();
            }
        }
    }

    unsigned long now = millis();
    unsigned long remaining;
    if (!continuousMeasurementMode && _pumpController && _pumpController->isEnabled()) {
        remaining = _pumpController->getTimeUntilNextMeasurementMs();
    } else {
        long r = (long)nextSensorReadAt - (long)now;
        remaining = (unsigned long)(r < 0 ? 0 : r);
    }

    JsonDocument resp;
    resp["mode"] = continuousMeasurementMode ? "continuous" : "normal";
    resp["next_read_in_ms"] = remaining;
    resp["interval_ms"] = continuousMeasurementMode
        ? 2000UL
        : (_pumpController && _pumpController->isEnabled()
            ? _pumpController->getCycleInterval()
            : sensorSamplingIntervalMs);

    // Include pump cycle phase for dashboard status label (with countdown seconds)
    if (_pumpController) {
        PumpState ps = _pumpController->getState();
        const char* phaseName = nullptr;
        switch (ps) {
            case PumpState::PUMP_STARTING: phaseName = "Pump started";  break;
            case PumpState::STABILIZING:   phaseName = "Flushing pipe"; break;
            case PumpState::MEASURING:     phaseName = "Measuring";     break;
            case PumpState::PUMP_STOPPING: phaseName = "Pump stopping"; break;
            case PumpState::COOLDOWN:      phaseName = "Cooling down";  break;
            default: break;
        }
        if (phaseName) {
            unsigned long remMs = _pumpController->getPhaseRemainingMs();
            String label = "Measurement status: ";
            label += phaseName;
            if (remMs > 0) {
                label += " (";
                label += String((remMs + 999) / 1000);  // round up to whole seconds
                label += "s)";
            }
            resp["pump_phase_label"] = label;
        } else {
            resp["pump_phase_label"] = "";
        }
    }

    String json;
    serializeJson(resp, json);
    sendJSON(json);
}

// ============================================================================
// Helper Methods
// ============================================================================

void SeaSenseWebServer::sendJSON(const String& json, int statusCode) {
    _server->send(statusCode, "application/json; charset=utf-8", json);
}

void SeaSenseWebServer::sendError(const String& message, int statusCode) {
    JsonDocument doc;
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

    JsonDocument doc;
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
    JsonDocument doc;
    JsonArray sensors = doc["sensors"].to<JsonArray>();

    if (_tempSensor) {
        SensorData data = _tempSensor->getData();
        JsonObject sensor = sensors.add<JsonObject>();
        sensor["type"] = data.sensorType;
        sensor["model"] = data.sensorModel;
        sensor["value"] = data.value;
        sensor["unit"] = data.unit;
        sensor["quality"] = sensorQualityToString(data.quality);
    }

    if (_ecSensor) {
        SensorData data = _ecSensor->getData();
        JsonObject sensor = sensors.add<JsonObject>();
        sensor["type"] = data.sensorType;
        sensor["model"] = data.sensorModel;
        sensor["value"] = data.value;
        sensor["unit"] = data.unit;
        sensor["quality"] = sensorQualityToString(data.quality);

        // Add salinity
        float salinity = _ecSensor->getSalinity();
        JsonObject salinityObj = sensors.add<JsonObject>();
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

void SeaSenseWebServer::handleApiClearSafeMode() {
    if (_server->method() != HTTP_POST) {
        sendError("Method not allowed", 405);
        return;
    }

    extern SystemHealth systemHealth;
    systemHealth.clearSafeMode();

    sendJSON("{\"success\":true,\"message\":\"Safe mode cleared, restarting...\"}");
    delay(500);  // Let response send
    ESP.restart();
}
