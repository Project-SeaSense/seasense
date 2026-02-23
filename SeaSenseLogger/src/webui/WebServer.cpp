/**
 * SeaSense Logger - Web Server Implementation
 */

#include "WebServer.h"
#include "../sensors/EZO_RTD.h"
#include "../sensors/EZO_EC.h"
#include "../sensors/EZO_pH.h"
#include "../sensors/EZO_DO.h"
#include "../config/ConfigManager.h"
#include "../../config/hardware_config.h"
#include "../../config/secrets.h"
#include "../system/SystemHealth.h"
#include "../sensors/GPSModule.h"
#include "../sensors/NMEA2000GPS.h"
#include "../api/APIUploader.h"
#include "../sensors/NMEA2000Environment.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>

// ============================================================================
// Constructor / Destructor
// ============================================================================

SeaSenseWebServer::SeaSenseWebServer(EZO_RTD* tempSensor, EZO_EC* ecSensor, StorageManager* storage, CalibrationManager* calibration, PumpController* pumpController, ConfigManager* configManager, EZO_pH* phSensor, EZO_DO* doSensor)
    : _tempSensor(tempSensor),
      _ecSensor(ecSensor),
      _phSensor(phSensor),
      _doSensor(doSensor),
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

    // Generate AP SSID from device GUID (last 4 hex chars for uniqueness)
    _apSSID = generateAPSSID();

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
    _server->on("/api/data/latest", std::bind(&SeaSenseWebServer::handleApiDataLatest, this));
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
        return "Connected to " + WiFi.SSID();
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

    // Use saved AP password from ConfigManager, fall back to compile-time default
    const char* apPass = WIFI_AP_PASSWORD;
    String savedApPass;
    if (_configManager) {
        savedApPass = _configManager->getWiFiConfig().apPassword;
        if (savedApPass.length() > 0) apPass = savedApPass.c_str();
    }

    if (!WiFi.softAP(_apSSID.c_str(), apPass, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONNECTIONS)) {
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
    // Read WiFi credentials from ConfigManager (saved via web UI),
    // fall back to compile-time defaults from secrets.h
    String ssid, password;
    if (_configManager) {
        ConfigManager::WiFiConfig wc = _configManager->getWiFiConfig();
        ssid = wc.stationSSID;
        password = wc.stationPassword;
    }
    if (ssid.length() == 0) {
        ssid = WIFI_STATION_SSID;
        password = WIFI_STATION_PASSWORD;
    }

    if (ssid.length() == 0) {
        DEBUG_WIFI_PRINTLN("No WiFi credentials configured");
        return false;
    }

    DEBUG_WIFI_PRINTLN("Connecting to WiFi...");
    DEBUG_WIFI_PRINT("SSID: ");
    DEBUG_WIFI_PRINTLN(ssid);

    WiFi.setHostname(_apSSID.c_str());
    WiFi.begin(ssid.c_str(), password.c_str());

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
    // Read credentials from ConfigManager, fall back to compile-time defaults
    String ssid, password;
    if (_configManager) {
        ConfigManager::WiFiConfig wc = _configManager->getWiFiConfig();
        ssid = wc.stationSSID;
        password = wc.stationPassword;
    }
    if (ssid.length() == 0) {
        ssid = WIFI_STATION_SSID;
        password = WIFI_STATION_PASSWORD;
    }
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
    WiFi.begin(ssid.c_str(), password.c_str());
    // Non-blocking on ESP32 AP+STA mode — status checked next iteration
}

String SeaSenseWebServer::generateAPSSID() {
    String suffix = "0000";
    if (_configManager) {
        String guid = _configManager->getDeviceConfig().deviceGUID;
        if (guid.length() >= 4) {
            suffix = guid.substring(guid.length() - 4);
            suffix.toUpperCase();
        }
    }
    return String(WIFI_AP_SSID_PREFIX) + suffix;
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
    static const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Dashboard - Project SeaSense</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        :root { --bg:#060a13; --sf:#0c1221; --cd:#111a2e; --bd:#1a2744; --b2:#243352; --ac:#22d3ee; --a2:#2dd4bf; --ag:rgba(34,211,238,0.12); --tx:#e2e8f0; --t2:#94a3b8; --t3:#475569; --ok:#34d399; --wn:#fbbf24; --er:#f87171 }
        * { margin:0; padding:0; box-sizing:border-box }
        body { font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif; background:var(--sf); color:var(--tx); -webkit-font-smoothing:antialiased; min-height:100vh }
        .header { background:var(--bg); padding:0 16px; height:52px; display:flex; align-items:center; border-bottom:1px solid var(--bd); position:sticky; top:0; z-index:100; box-shadow:0 4px 24px rgba(0,0,0,0.3) }
        .header::after { content:''; position:absolute; bottom:-1px; left:0; right:0; height:1px; background:linear-gradient(90deg,transparent,var(--ac),transparent); opacity:0.4 }
        .hamburger { background:none; border:none; color:var(--t2); font-size:22px; cursor:pointer; padding:8px; margin-right:12px; line-height:1; border-radius:6px; transition:all 0.2s; font-family:Arial,sans-serif }
        .hamburger:hover { color:var(--ac); background:var(--ag) }
        .title { font-size:14px; font-weight:600; color:var(--ac); text-transform:none }
        .sidebar { position:fixed; left:-260px; top:0; width:260px; height:100%; background:var(--bg); border-right:1px solid var(--bd); transition:left 0.3s ease; z-index:201; pointer-events:auto }
        .sidebar.open { left:0 }
        .sidebar-header { padding:20px; border-bottom:1px solid var(--bd); font-weight:600; color:var(--ac); font-size:13px; background:var(--bg); text-transform:none }
        .sidebar-nav { list-style:none; padding:8px 0 }
        .sidebar-nav a { display:block; padding:12px 20px; color:var(--t2); text-decoration:none; font-size:14px; font-weight:500; transition:all 0.2s; border-left:2px solid transparent; border-bottom:1px solid rgba(26,39,68,0.5) }
        .sidebar-nav a:hover { color:var(--tx); background:rgba(34,211,238,0.05) }
        .sidebar-nav a.active { color:var(--ac); border-left-color:var(--ac); background:rgba(34,211,238,0.08); font-weight:600 }
        .overlay { position:fixed; inset:0; background:rgba(0,0,0,0.6); display:none; z-index:200; pointer-events:auto; cursor:pointer; backdrop-filter:blur(2px) }
        .overlay.show { display:block }
        .container { padding:16px; max-width:640px; margin:0 auto }
        .sensors-grid { display:grid; gap:12px }
        .sensor-card { background:var(--cd); border:1px solid var(--bd); border-radius:12px; padding:16px 20px; position:relative; overflow:hidden; transition:border-color 0.3s }
        .sensor-card:hover { border-color:var(--b2) }
        .sensor-card::before { content:''; position:absolute; left:0; top:0; bottom:0; width:3px; background:var(--ac) }
        .sensor-name { font-size:11px; font-weight:600; color:var(--t2); text-transform:uppercase; letter-spacing:1.5px; margin-bottom:8px }
        .sensor-value { font-size:28px; font-weight:700; color:var(--tx); font-family:'SF Mono',ui-monospace,'Cascadia Code',Consolas,monospace; font-variant-numeric:tabular-nums; line-height:1.2; text-shadow:0 0 30px rgba(34,211,238,0.12) }
        .sensor-unit { font-size:14px; font-weight:400; color:var(--t2); margin-left:4px }
        .sensor-meta { margin-top:8px; font-size:11px; color:var(--t3) }
        .sensor-card.offline { opacity:0.4 }
        .sensor-card.offline::before { background:var(--t3) }
        .sensor-offline-label { font-size:10px; color:var(--t3); margin-top:4px; font-style:italic }
        .section-title { font-size:11px; font-weight:600; color:var(--t3); text-transform:uppercase; letter-spacing:2px; margin:24px 0 12px; display:flex; align-items:center; gap:12px; padding-bottom:0; border-bottom:none }
        .section-title::after { content:''; flex:1; height:1px; background:var(--bd) }
        .env-grid { display:grid; grid-template-columns:1fr 1fr; gap:10px }
        .env-card { background:var(--cd); border:1px solid var(--bd); border-radius:10px; padding:12px 14px; transition:border-color 0.3s; position:relative; overflow:hidden }
        .env-card::before { content:''; position:absolute; left:0; top:0; bottom:0; width:3px; background:var(--a2) }
        .env-card:hover { border-color:var(--b2) }
        .env-card.stale { opacity:0.3 }
        .env-label { font-size:10px; font-weight:600; color:var(--a2); text-transform:uppercase; letter-spacing:1px; margin-bottom:4px; opacity:0.8 }
        .env-value { font-size:20px; font-weight:700; color:var(--tx); font-family:'SF Mono',ui-monospace,Consolas,monospace; font-variant-numeric:tabular-nums; line-height:1.2 }
        .env-unit { font-size:11px; color:var(--t2); margin-left:2px; font-weight:400 }
        .env-none { text-align:center; padding:20px; color:var(--t3); font-size:13px; grid-column:1/-1 }
        .env-nodata { font-size:9px; color:var(--t3); font-style:italic; margin-top:2px }
        .loading-pulse { animation:pulse 2s ease-in-out infinite }
        @keyframes pulse { 0%,100% { opacity:1 } 50% { opacity:0.4 } }
        .status-msg { text-align:center; padding:30px; color:var(--t3); font-size:13px }
        .measure-bar { display:flex; align-items:center; justify-content:space-between; background:var(--cd); border:1px solid var(--bd); border-radius:10px; padding:10px 16px; margin:10px 0 }
        .countdown { font-size:13px; color:var(--ac); font-weight:600; font-variant-numeric:tabular-nums; font-family:'SF Mono',ui-monospace,Consolas,monospace }
        .upload-bar { background:var(--cd); border:1px solid var(--bd); border-radius:10px; padding:8px 16px; margin:0 0 16px; font-size:12px; color:var(--t2); display:flex; flex-wrap:wrap; align-items:center; gap:8px; min-height:34px }
        .up-state { font-weight:700; font-family:ui-monospace,Consolas,monospace; font-size:11px; letter-spacing:0.5px }
        .up-state.ok { color:var(--ok) }
        .up-state.err { color:var(--er) }
        .up-state.busy { color:var(--wn) }
        .up-sep { color:var(--t3) }
    </style>
</head>
<body>
    <div class="overlay" id="overlay" onclick="closeMenu()"></div>

    <div class="sidebar" id="sidebar">
        <div class="sidebar-header">Project SeaSense Data Logger</div>
        <ul class="sidebar-nav">
            <li><a href="/dashboard" class="active">Dashboard</a></li>
            <li><a href="/data">Data</a></li>
            <li><a href="/calibrate">Calibration</a></li>
            <li><a href="/settings">Settings</a></li>
        </ul>
    </div>

    <div class="header">
        <button class="hamburger" onclick="toggleMenu()">&#9776;</button>
        <div class="title">Project SeaSense Data Logger</div>
    </div>

    <div class="container">
        <div class="measure-bar">
            <span class="countdown" id="countdownLabel">Next measurement in --:--</span>
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
        <div class="section-title">Navigation</div>
        <div class="env-grid" id="envNav">
            <div class="env-none">Waiting for data...</div>
        </div>
        <div class="section-title">Environment</div>
        <div class="env-grid" id="envData">
            <div class="env-none">Waiting for data...</div>
        </div>
    </div>

    <script>
        let autoUpdate = true;
        let cdAnchorMs = null;   // server-reported remaining ms
        let cdAnchorAt = null;   // Date.now() when received
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

        // Countdown ticker — runs every 100ms, driven by local clock from anchor
        setInterval(function() {
            const label = document.getElementById('countdownLabel');
            if (!label) return;
            if (pumpPhaseLabel) { label.textContent = pumpPhaseLabel; return; }
            if (cdAnchorMs === null || cdAnchorAt === null) return;
            const elapsed = Date.now() - cdAnchorAt;
            const remaining = Math.max(0, cdAnchorMs - elapsed);
            const s = Math.floor(remaining / 1000);
            const m = Math.floor(s / 60);
            label.textContent = 'Next measurement in ' + m + ':' + String(s % 60).padStart(2, '0');
        }, 100);

        function updateMeasurement() {
            fetch('/api/measurement')
                .then(r => r.json())
                .then(d => {
                    cdAnchorMs = d.next_read_in_ms;
                    cdAnchorAt = Date.now();
                    pumpPhaseLabel = d.pump_phase_label || '';
                })
                .catch(() => {});
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

        function fmtBytes(b) {
            if (b < 1024) return b + ' B';
            if (b < 1048576) return (b/1024).toFixed(1) + ' KB';
            if (b < 1073741824) return (b/1048576).toFixed(2) + ' MB';
            return (b/1073741824).toFixed(2) + ' GB';
        }

        let _upNextMs = 0, _upFetchedAt = 0;
        let _upLastHtml = '';
        function updateUploadStatus() {
            const bar = document.getElementById('uploadBar');
            if (!bar) return;
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
                    const lastEpoch = u.last_success_epoch || 0;
                    let lastStr;
                    if (lastMs > 0 && uptimeMs > 0) lastStr = fmtAgo(uptimeMs - lastMs);
                    else if (lastEpoch > 0) lastStr = fmtAgo((Date.now()/1000 - lastEpoch) * 1000);
                    else lastStr = 'never';
                    _upNextMs = u.next_upload_ms || 0;
                    _upFetchedAt = Date.now();
                    _upLastHtml = '<span class="up-state ' + stateClass + '">' + status + '</span>'
                        + '<span class="up-sep">&middot;</span>'
                        + '<span>' + pending + '</span>'
                        + '<span class="up-sep">&middot;</span>'
                        + '<span>Last: ' + lastStr + '</span>';
                    if (u.retry_count > 0) {
                        _upLastHtml += '<span class="up-sep">&middot;</span>'
                            + '<span style="color:#f87171">Retry #' + u.retry_count + '</span>';
                    }
                    const totalUp = u.total_bytes_uploaded || 0;
                    if (totalUp > 0) {
                        _upLastHtml += '<span class="up-sep">&middot;</span>'
                            + '<span>Total: ' + fmtBytes(totalUp) + '</span>';
                    }
                    renderUploadBar();
                })
                .catch(() => {});
        }
        function renderUploadBar() {
            const bar = document.getElementById('uploadBar');
            if (!bar) return;
            const elapsed = Date.now() - _upFetchedAt;
            const remaining = Math.max(0, _upNextMs - elapsed);
            const nextStr = remaining > 0 ? fmtMs(remaining) : '--';
            bar.innerHTML = _upLastHtml
                + '<span class="up-sep">&middot;</span>'
                + '<span>Next: ' + nextStr + '</span>';
        }

        // Track last known good values per sensor type
        const lastGood = {};

        function fmtSensor(type, value) {
            const t = type.toLowerCase();
            if (t.includes('temperature')) return value.toFixed(3);
            if (t.includes('salinity'))    return value.toFixed(2);
            if (t.includes('ph'))          return value.toFixed(3);
            if (t.includes('oxygen'))      return value.toFixed(2);
            return value.toFixed(0);
        }

        function update() {
            fetch('/api/sensors')
                .then(r => r.json())
                .then(data => {
                    let html = '';
                    if (data.sensors && data.sensors.length > 0) {
                        data.sensors.forEach(s => {
                            const key = s.type;
                            // Use new value if non-zero, otherwise keep last known
                            if (s.value !== 0) {
                                lastGood[key] = { value: s.value, unit: s.unit, clamped: s.clamped };
                            }
                            const has = lastGood[key];
                            let valueFormatted = has ? fmtSensor(key, has.value) : '&mdash;';
                            if (has && has.clamped && key.toLowerCase().includes('salinity')) {
                                valueFormatted = '>' + valueFormatted;
                            }
                            const unit = has ? has.unit : '';

                            html += `<div class="sensor-card">
                                <div class="sensor-name">${s.type}</div>
                                <div class="sensor-value">
                                    ${valueFormatted}<span class="sensor-unit">${unit}</span>
                                </div>
                                ${s.serial ? `<div class="sensor-meta">Serial: ${s.serial}</div>` : ''}
                            </div>`;
                        });
                    } else {
                        html = '<div class="status-msg">No sensor data available</div>';
                    }
                    // Placeholder cards for sensors not yet connected
                    let types = (data.sensors||[]).map(s => s.type.toLowerCase());
                    if (!types.some(t => t.includes('ph'))) {
                        html += `<div class="sensor-card offline">
                            <div class="sensor-name">pH</div>
                            <div class="sensor-value">&mdash;<span class="sensor-unit"></span></div>
                            <div class="sensor-offline-label">Sensor not connected</div>
                        </div>`;
                    }
                    if (!types.some(t => t.includes('oxygen'))) {
                        html += `<div class="sensor-card offline">
                            <div class="sensor-name">Dissolved Oxygen</div>
                            <div class="sensor-value">&mdash;<span class="sensor-unit"></span></div>
                            <div class="sensor-offline-label">Sensor not connected</div>
                        </div>`;
                    }
                    document.getElementById('sensors').innerHTML = html;
                })
                .catch(err => {
                    document.getElementById('sensors').innerHTML = '<div class="status-msg">Error loading sensors</div>';
                });
        }

        function envCard(label, value, unit) {
            if (value !== undefined) {
                return `<div class="env-card"><div class="env-label">${label}</div><div class="env-value">${value}<span class="env-unit">${unit}</span></div></div>`;
            }
            return `<div class="env-card stale"><div class="env-label">${label}</div><div class="env-value">&mdash;<span class="env-unit"></span></div><div class="env-nodata">no data</div></div>`;
        }

        function updateEnv() {
            fetch('/api/environment')
                .then(r => r.json())
                .then(d => {
                    const n = d.navigation || {};
                    const a = d.attitude || {};
                    let nav = '';
                    nav += envCard('COG', n.cog, '\u00B0');
                    nav += envCard('SOG', n.sog, 'm/s');
                    nav += envCard('Heading', n.heading, '\u00B0');
                    nav += envCard('STW', d.water ? d.water.stw : undefined, 'm/s');
                    nav += envCard('Pitch', a.pitch, '\u00B0');
                    nav += envCard('Roll', a.roll, '\u00B0');
                    document.getElementById('envNav').innerHTML = nav;

                    const w = d.wind || {};
                    const atm = d.atmosphere || {};
                    const wat = d.water || {};
                    let env = '';
                    env += envCard('Water Temp', wat.temp_ext, '\u00B0C');
                    env += envCard('Air Temp', atm.air_temp, '\u00B0C');
                    env += envCard('Depth', wat.depth, 'm');
                    env += envCard('Pressure', atm.pressure_hpa, 'hPa');
                    env += envCard('True Wind', w.speed_true, 'm/s');
                    env += envCard('Wind Dir', w.angle_true, '\u00B0');
                    env += envCard('Humidity', atm.humidity, '%');
                    env += envCard('App Wind', w.speed_app, 'm/s');
                    document.getElementById('envData').innerHTML = env;
                })
                .catch(() => {});
        }

        function loadLatest() {
            fetch('/api/data/latest').then(r => r.json()).then(data => {
                if (data.sensors) data.sensors.forEach(s => {
                    if (s.value !== 0) lastGood[s.type] = { value: s.value, unit: s.unit, clamped: s.clamped };
                });
                update();
            }).catch(() => { update(); });
        }
        loadLatest();
        updateEnv();
        updateMeasurement();
        updateUploadStatus();
        setInterval(() => { if (autoUpdate) { update(); updateEnv(); updateMeasurement(); } }, 3000);
        setInterval(updateUploadStatus, 10000);
        setInterval(renderUploadBar, 1000);
    </script>
</body>
</html>
)HTML";

    _server->send_P(200, "text/html", PAGE);
}

void SeaSenseWebServer::handleCalibrate() {
    static const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Calibration - Project SeaSense</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        :root { --bg:#060a13; --sf:#0c1221; --cd:#111a2e; --bd:#1a2744; --b2:#243352; --ac:#22d3ee; --a2:#2dd4bf; --ag:rgba(34,211,238,0.12); --tx:#e2e8f0; --t2:#94a3b8; --t3:#475569; --ok:#34d399; --wn:#fbbf24; --er:#f87171 }
        * { margin:0; padding:0; box-sizing:border-box }
        body { font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif; background:var(--sf); color:var(--tx); -webkit-font-smoothing:antialiased; min-height:100vh }
        .header { background:var(--bg); padding:0 16px; height:52px; display:flex; align-items:center; border-bottom:1px solid var(--bd); position:sticky; top:0; z-index:100; box-shadow:0 4px 24px rgba(0,0,0,0.3) }
        .header::after { content:''; position:absolute; bottom:-1px; left:0; right:0; height:1px; background:linear-gradient(90deg,transparent,var(--ac),transparent); opacity:0.4 }
        .hamburger { background:none; border:none; color:var(--t2); font-size:22px; cursor:pointer; padding:8px; margin-right:12px; line-height:1; border-radius:6px; transition:all 0.2s; font-family:Arial,sans-serif }
        .hamburger:hover { color:var(--ac); background:var(--ag) }
        .title { font-size:14px; font-weight:600; color:var(--ac); text-transform:none }
        .sidebar { position:fixed; left:-260px; top:0; width:260px; height:100%; background:var(--bg); border-right:1px solid var(--bd); transition:left 0.3s ease; z-index:201; pointer-events:auto }
        .sidebar.open { left:0 }
        .sidebar-header { padding:20px; border-bottom:1px solid var(--bd); font-weight:600; color:var(--ac); font-size:13px; background:var(--bg); text-transform:none }
        .sidebar-nav { list-style:none; padding:8px 0 }
        .sidebar-nav a { display:block; padding:12px 20px; color:var(--t2); text-decoration:none; font-size:14px; font-weight:500; transition:all 0.2s; border-left:2px solid transparent; border-bottom:1px solid rgba(26,39,68,0.5) }
        .sidebar-nav a:hover { color:var(--tx); background:rgba(34,211,238,0.05) }
        .sidebar-nav a.active { color:var(--ac); border-left-color:var(--ac); background:rgba(34,211,238,0.08); font-weight:600 }
        .overlay { position:fixed; inset:0; background:rgba(0,0,0,0.6); display:none; z-index:200; pointer-events:auto; cursor:pointer; backdrop-filter:blur(2px) }
        .overlay.show { display:block }
        .container { padding:16px; max-width:640px; margin:0 auto }
        .cal-card { background:var(--cd); border:1px solid var(--bd); border-radius:12px; padding:20px; margin-bottom:15px; position:relative; overflow:hidden }
        .cal-card::before { content:''; position:absolute; left:0; top:0; bottom:0; width:3px; background:var(--ac) }
        .cal-header { font-size:14px; font-weight:600; color:var(--ac); margin-bottom:15px; text-transform:uppercase; letter-spacing:1px }
        .cal-info { background:rgba(34,211,238,0.05); border:1px solid var(--bd); padding:12px; border-radius:8px; margin-bottom:15px; font-size:13px; color:var(--t2) }
        .cal-section { margin:15px 0 }
        .cal-section-title { font-size:12px; font-weight:600; color:var(--t2); margin-bottom:10px; text-transform:uppercase; letter-spacing:0.5px }
        .form-group { margin:12px 0 }
        .form-group label { display:block; font-size:13px; font-weight:600; color:var(--t2); margin-bottom:5px }
        .form-group input, .form-group select { width:100%; padding:10px; border:1px solid var(--bd); border-radius:8px; font-size:14px; transition:all 0.2s; background:var(--bg); color:var(--tx) }
        .form-group input:focus, .form-group select:focus { outline:none; border-color:var(--ac); box-shadow:0 0 0 3px var(--ag) }
        .form-group small { display:block; margin-top:5px; font-size:12px; color:var(--t3) }
        .btn-group { display:flex; gap:10px; margin-top:15px }
        .btn { padding:10px 20px; border:none; border-radius:8px; font-size:14px; font-weight:600; cursor:pointer; transition:all 0.2s; flex:1 }
        .btn-primary { background:var(--ac); color:var(--bg) }
        .btn-primary:hover { background:#06b6d4; box-shadow:0 0 16px rgba(34,211,238,0.3) }
        .btn-primary:disabled { background:var(--t3); cursor:not-allowed; box-shadow:none }
        .btn-secondary { background:var(--b2); color:var(--tx) }
        .btn-secondary:hover { background:var(--bd) }
        .btn-danger { background:var(--er); color:white; flex:none }
        .btn-danger:hover { background:#ef4444 }
        .btn-sm { padding:6px 12px; font-size:12px; flex:none }
        .toast { position:fixed; top:60px; right:20px; padding:12px 20px; border-radius:8px; display:none; z-index:1000; box-shadow:0 8px 24px rgba(0,0,0,0.4); font-size:13px; max-width:350px; border:1px solid; backdrop-filter:blur(12px) }
        .toast-success { background:rgba(52,211,153,0.15); color:var(--ok); border-color:rgba(52,211,153,0.3) }
        .toast-error { background:rgba(248,113,113,0.15); color:var(--er); border-color:rgba(248,113,113,0.3) }
        .toast-info { background:rgba(34,211,238,0.15); color:var(--ac); border-color:rgba(34,211,238,0.3) }
        .status-current { display:inline-block; padding:3px 10px; border-radius:12px; font-size:11px; font-weight:600; margin-left:10px; letter-spacing:0.5px }
        .status-calibrated { background:rgba(52,211,153,0.15); color:var(--ok); border:1px solid rgba(52,211,153,0.3) }
        .status-not-calibrated { background:rgba(248,113,113,0.15); color:var(--er); border:1px solid rgba(248,113,113,0.3) }
        .status-offline { background:rgba(71,85,105,0.2); color:var(--t3); border:1px solid rgba(71,85,105,0.3) }
        .cal-card.offline { opacity:0.4; pointer-events:none }
        .cal-card.offline::before { background:var(--t3) }
        @keyframes readPulse { 0%,100%{opacity:1} 50%{opacity:0.3} }
        .reading-pulse { animation:readPulse 0.4s ease-in-out 2 }
    </style>
</head>
<body>
    <div class="overlay" id="overlay" onclick="closeMenu()"></div>

    <div class="sidebar" id="sidebar">
        <div class="sidebar-header">Project SeaSense Data Logger</div>
        <ul class="sidebar-nav">
            <li><a href="/dashboard">Dashboard</a></li>
            <li><a href="/data">Data</a></li>
            <li><a href="/calibrate" class="active">Calibration</a></li>
            <li><a href="/settings">Settings</a></li>
        </ul>
    </div>

    <div class="header">
        <button class="hamburger" onclick="toggleMenu()">&#9776;</button>
        <div class="title">Project SeaSense Data Logger</div>
    </div>

    <div id="toast" class="toast"></div>

    <div class="container">
        <!-- Temperature Calibration -->
        <div class="cal-card">
            <div class="cal-header">Temperature Sensor <span class="status-current status-calibrated" id="tempStatus">Calibrated</span></div>
            <div class="cal-info">
                <strong>EZO-RTD Temperature Sensor</strong><br>
                Single-point calibration recommended. Use ice water (0&deg;C) or room temperature with accurate thermometer.
            </div>

            <div class="cal-section">
                <div class="cal-section-title">Current Reading</div>
                <div style="font-size:24px; font-weight:700; color:var(--ac); margin:10px 0; font-family:'SF Mono',ui-monospace,Consolas,monospace;">
                    <span id="tempReading">--</span> &deg;C
                </div>
            </div>

            <div class="form-group">
                <label>Calibration Type</label>
                <select id="tempCalType">
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
                <button class="btn btn-primary" onclick="calibrateTemp(this)">Calibrate</button>
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
                <div style="font-size:24px; font-weight:700; color:var(--ac); margin:10px 0; font-family:'SF Mono',ui-monospace,Consolas,monospace;">
                    <span id="ecReading">--</span> &micro;S/cm
                </div>
            </div>

            <div class="form-group">
                <label>Calibration Type</label>
                <select id="ecCalType">
                    <option value="single">Single Point</option>
                    <option value="dry">Dry Calibration</option>
                    <option value="two-low">Two-Point (Low)</option>
                    <option value="two-high">Two-Point (High)</option>
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
                <button class="btn btn-primary" onclick="calibrateEC(this)">Calibrate</button>
            </div>
        </div>

        <!-- pH Calibration -->
        <div class="cal-card" id="phCard">
            <div class="cal-header">pH Sensor <span class="status-current status-offline" id="phStatus">Not Connected</span></div>
            <div class="cal-info">
                <strong>EZO-pH Sensor</strong><br>
                Up to 3-point calibration per Atlas Scientific specs. Always start with mid-point (pH 7.00). Add low (pH 4.00) and high (pH 10.00) for best accuracy. Rinse probe between solutions.
            </div>

            <div class="cal-section">
                <div class="cal-section-title">Current Reading</div>
                <div style="font-size:24px; font-weight:700; color:var(--ac); margin:10px 0; font-family:'SF Mono',ui-monospace,Consolas,monospace;">
                    <span id="phReading">--</span> pH
                </div>
            </div>

            <div class="form-group">
                <label>Calibration Type</label>
                <select id="phCalType">
                    <option value="mid">Mid Point (pH 7.00)</option>
                    <option value="low">Low Point (pH 4.00)</option>
                    <option value="high">High Point (pH 10.00)</option>
                </select>
                <small>Start with mid point. Then add low and/or high for 2 or 3-point calibration.</small>
            </div>

            <div class="form-group" id="phValueGroup">
                <label>Reference pH Value</label>
                <input type="number" id="phValue" step="0.01" placeholder="e.g. 7.00">
                <small>Enter the exact pH of your buffer solution</small>
            </div>

            <div class="btn-group">
                <button class="btn btn-secondary" onclick="readPH()">Read Sensor</button>
                <button class="btn btn-primary" onclick="calibratePH(this)">Calibrate</button>
            </div>
        </div>

        <!-- Dissolved Oxygen Calibration -->
        <div class="cal-card" id="doCard">
            <div class="cal-header">Dissolved Oxygen Sensor <span class="status-current status-offline" id="doStatus">Not Connected</span></div>
            <div class="cal-info">
                <strong>EZO-DO Sensor</strong><br>
                Atmospheric calibration: hold probe in air with dry membrane. Zero calibration (optional): submerge in sodium sulfite (Na&#8322;SO&#8323;) solution for 0 mg/L reference.
            </div>

            <div class="cal-section">
                <div class="cal-section-title">Current Reading</div>
                <div style="font-size:24px; font-weight:700; color:var(--ac); margin:10px 0; font-family:'SF Mono',ui-monospace,Consolas,monospace;">
                    <span id="doReading">--</span> mg/L
                </div>
            </div>

            <div class="form-group">
                <label>Calibration Type</label>
                <select id="doCalType">
                    <option value="atmospheric">Atmospheric (probe in air)</option>
                    <option value="zero">Zero (0 mg/L solution)</option>
                </select>
                <small>Atmospheric calibration is usually sufficient. Zero calibration improves low-range accuracy.</small>
            </div>

            <div class="btn-group">
                <button class="btn btn-secondary" onclick="readDO()">Read Sensor</button>
                <button class="btn btn-primary" onclick="calibrateDO(this)">Calibrate</button>
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

        function showToast(message, type) {
            const toast = document.getElementById('toast');
            toast.textContent = message;
            toast.className = 'toast toast-' + type;
            toast.style.display = 'block';
            setTimeout(() => { toast.style.display = 'none'; }, 5000);
        }

        function updateReadings() {
            fetch('/api/sensors')
                .then(r => r.json())
                .then(data => {
                    if (!data.sensors) return;
                    let phPresent = false, doPresent = false;
                    data.sensors.forEach(s => {
                        const t = s.type.toLowerCase();
                        if (t.includes('temperature')) {
                            document.getElementById('tempReading').textContent = s.value.toFixed(3);
                        } else if (t.includes('conductivity')) {
                            document.getElementById('ecReading').textContent = s.value.toFixed(0);
                        } else if (t === 'ph') {
                            document.getElementById('phReading').textContent = s.value.toFixed(3);
                            phPresent = true;
                        } else if (t.includes('oxygen')) {
                            document.getElementById('doReading').textContent = s.value.toFixed(2);
                            doPresent = true;
                        }
                    });
                    // Update pH card status
                    const phCard = document.getElementById('phCard');
                    const phStatus = document.getElementById('phStatus');
                    if (phPresent) {
                        phCard.classList.remove('offline');
                        phStatus.textContent = 'Connected';
                        phStatus.className = 'status-current status-calibrated';
                    } else {
                        phCard.classList.add('offline');
                        phStatus.textContent = 'Not Connected';
                        phStatus.className = 'status-current status-offline';
                    }
                    // Update DO card status
                    const doCard = document.getElementById('doCard');
                    const doSt = document.getElementById('doStatus');
                    if (doPresent) {
                        doCard.classList.remove('offline');
                        doSt.textContent = 'Connected';
                        doSt.className = 'status-current status-calibrated';
                    } else {
                        doCard.classList.add('offline');
                        doSt.textContent = 'Not Connected';
                        doSt.className = 'status-current status-offline';
                    }
                })
                .catch(() => {});
        }

        function pulseEl(el) {
            el.classList.add('reading-pulse');
            setTimeout(() => el.classList.remove('reading-pulse'), 800);
        }

        function triggerRead(then) {
            fetch('/api/sensor/read', { method: 'POST' })
                .then(r => r.json())
                .then(then)
                .catch(() => {});
        }

        function readTemp() {
            const el = document.getElementById('tempReading');
            el.classList.add('reading-pulse');
            triggerRead(() => {
                fetch('/api/sensor/reading?type=temperature').then(r => r.json())
                    .then(data => { el.textContent = data.value.toFixed(3); setTimeout(() => el.classList.remove('reading-pulse'), 800); })
                    .catch(() => { el.classList.remove('reading-pulse'); showToast('Error reading temperature sensor', 'error'); });
            });
        }

        function readEC() {
            const el = document.getElementById('ecReading');
            el.classList.add('reading-pulse');
            triggerRead(() => {
                fetch('/api/sensor/reading?type=conductivity').then(r => r.json())
                    .then(data => { el.textContent = data.value.toFixed(0); setTimeout(() => el.classList.remove('reading-pulse'), 800); })
                    .catch(() => { el.classList.remove('reading-pulse'); showToast('Error reading conductivity sensor', 'error'); });
            });
        }

        function readPH() {
            const el = document.getElementById('phReading');
            el.classList.add('reading-pulse');
            triggerRead(() => {
                fetch('/api/sensor/reading?type=ph').then(r => r.json())
                    .then(data => { el.textContent = data.value.toFixed(3); setTimeout(() => el.classList.remove('reading-pulse'), 800); })
                    .catch(() => { el.classList.remove('reading-pulse'); showToast('Error reading pH sensor', 'error'); });
            });
        }

        function readDO() {
            const el = document.getElementById('doReading');
            el.classList.add('reading-pulse');
            triggerRead(() => {
                fetch('/api/sensor/reading?type=dissolved_oxygen').then(r => r.json())
                    .then(data => { el.textContent = data.value.toFixed(2); setTimeout(() => el.classList.remove('reading-pulse'), 800); })
                    .catch(() => { el.classList.remove('reading-pulse'); showToast('Error reading DO sensor', 'error'); });
            });
        }

        updateReadings();
        setInterval(updateReadings, 3000);

        // Poll calibration status — show progress on the button, toast only at start/end
        let calPolling = false;
        let calBtn = null;
        let calBtnOrigText = '';

        function pollCalibration(sensorLabel, readFn) {
            if (calPolling) return;
            calPolling = true;
            setCalBtnsDisabled(true);
            showToast('Calibration started', 'info');
            const poll = setInterval(() => {
                fetch('/api/calibrate/status')
                    .then(r => r.json())
                    .then(s => {
                        if (s.status === 'preparing') {
                            setCalBtnText('Preparing...');
                            return;
                        }
                        if (s.status === 'waiting_stable') {
                            const rd = s.currentReading ? ' (' + s.currentReading.toFixed(0) + ')' : '';
                            setCalBtnText('Stabilizing...' + rd);
                            return;
                        }
                        if (s.status === 'calibrating') {
                            setCalBtnText('Calibrating...');
                            return;
                        }
                        clearInterval(poll);
                        calPolling = false;
                        setCalBtnsDisabled(false);
                        restoreCalBtn();
                        if (s.status === 'complete') {
                            showToast(sensorLabel + ' calibration successful!', 'success');
                            if (readFn) setTimeout(readFn, 500);
                        } else {
                            showToast('Calibration failed: ' + (s.message || 'Unknown error'), 'error');
                        }
                    })
                    .catch(() => {
                        clearInterval(poll);
                        calPolling = false;
                        setCalBtnsDisabled(false);
                        restoreCalBtn();
                        showToast('Lost connection during calibration', 'error');
                    });
            }, 1000);
        }

        function setCalBtnsDisabled(d) {
            document.querySelectorAll('.btn-primary').forEach(b => {
                b.disabled = d;
                b.style.opacity = d ? '0.5' : '';
            });
            // Keep the active button visible
            if (d && calBtn) calBtn.style.opacity = '1';
        }

        function setCalBtnText(txt) {
            if (calBtn) calBtn.textContent = txt;
        }

        function restoreCalBtn() {
            if (calBtn) { calBtn.textContent = calBtnOrigText; calBtn = null; }
        }

        function startCalibration(data, sensorLabel, readFn, btnEl) {
            if (calPolling) { showToast('Calibration already in progress', 'error'); return; }
            calBtn = btnEl;
            calBtnOrigText = btnEl.textContent;
            fetch('/api/calibrate', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(data)
            })
            .then(r => r.json())
            .then(result => {
                if (result.success) {
                    pollCalibration(sensorLabel, readFn);
                } else {
                    restoreCalBtn();
                    showToast('Calibration failed: ' + (result.error || 'Unknown error'), 'error');
                }
            })
            .catch(err => { restoreCalBtn(); showToast('Error starting calibration', 'error'); });
        }

        function calibrateTemp(btn) {
            const value = parseFloat(document.getElementById('tempValue').value);
            if (!value && value !== 0) { showToast('Please enter a reference temperature value', 'error'); return; }
            startCalibration({ sensor: 'temperature', type: 'single', value: value || 0 }, 'Temperature', readTemp, btn);
        }

        function calibrateEC(btn) {
            const type = document.getElementById('ecCalType').value;
            const value = parseFloat(document.getElementById('ecValue').value);
            if (type !== 'dry' && !value && value !== 0) { showToast('Please enter a reference conductivity value', 'error'); return; }
            startCalibration({ sensor: 'conductivity', type: type, value: value || 0 }, 'Conductivity', readEC, btn);
        }

        function calibratePH(btn) {
            const type = document.getElementById('phCalType').value;
            const value = parseFloat(document.getElementById('phValue').value);
            if (!value && value !== 0) { showToast('Please enter a reference pH value', 'error'); return; }
            startCalibration({ sensor: 'ph', type: type, value: value }, 'pH', readPH, btn);
        }

        function calibrateDO(btn) {
            const type = document.getElementById('doCalType').value;
            startCalibration({ sensor: 'dissolved_oxygen', type: type, value: 0 }, 'DO', readDO, btn);
        }

        // Toggle value input visibility based on calibration type
        document.getElementById('ecCalType').addEventListener('change', function() {
            document.getElementById('ecValueGroup').style.display = this.value === 'dry' ? 'none' : 'block';
        });

        // Pre-fill pH reference value based on selected calibration type
        document.getElementById('phCalType').addEventListener('change', function() {
            const defaults = { mid: '7.00', low: '4.00', high: '10.00' };
            document.getElementById('phValue').value = defaults[this.value] || '';
        });
        // Set initial default
        document.getElementById('phValue').value = '7.00';

        // Initial read
        readTemp();
        readEC();
    </script>
</body>
</html>
)HTML";

    _server->send_P(200, "text/html", PAGE);
}

void SeaSenseWebServer::handleData() {
    static const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Data - Project SeaSense</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        :root { --bg:#060a13; --sf:#0c1221; --cd:#111a2e; --bd:#1a2744; --b2:#243352; --ac:#22d3ee; --a2:#2dd4bf; --ag:rgba(34,211,238,0.12); --tx:#e2e8f0; --t2:#94a3b8; --t3:#475569; --ok:#34d399; --wn:#fbbf24; --er:#f87171 }
        * { margin:0; padding:0; box-sizing:border-box }
        body { font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif; background:var(--sf); color:var(--tx); -webkit-font-smoothing:antialiased; min-height:100vh }
        .header { background:var(--bg); padding:0 16px; height:52px; display:flex; align-items:center; border-bottom:1px solid var(--bd); position:sticky; top:0; z-index:100; box-shadow:0 4px 24px rgba(0,0,0,0.3) }
        .header::after { content:''; position:absolute; bottom:-1px; left:0; right:0; height:1px; background:linear-gradient(90deg,transparent,var(--ac),transparent); opacity:0.4 }
        .hamburger { background:none; border:none; color:var(--t2); font-size:22px; cursor:pointer; padding:8px; margin-right:12px; line-height:1; border-radius:6px; transition:all 0.2s; font-family:Arial,sans-serif }
        .hamburger:hover { color:var(--ac); background:var(--ag) }
        .title { font-size:14px; font-weight:600; color:var(--ac); text-transform:none }
        .sidebar { position:fixed; left:-260px; top:0; width:260px; height:100%; background:var(--bg); border-right:1px solid var(--bd); transition:left 0.3s ease; z-index:201; pointer-events:auto }
        .sidebar.open { left:0 }
        .sidebar-header { padding:20px; border-bottom:1px solid var(--bd); font-weight:600; color:var(--ac); font-size:13px; background:var(--bg); text-transform:none }
        .sidebar-nav { list-style:none; padding:8px 0 }
        .sidebar-nav a { display:block; padding:12px 20px; color:var(--t2); text-decoration:none; font-size:14px; font-weight:500; transition:all 0.2s; border-left:2px solid transparent; border-bottom:1px solid rgba(26,39,68,0.5) }
        .sidebar-nav a:hover { color:var(--tx); background:rgba(34,211,238,0.05) }
        .sidebar-nav a.active { color:var(--ac); border-left-color:var(--ac); background:rgba(34,211,238,0.08); font-weight:600 }
        .overlay { position:fixed; inset:0; background:rgba(0,0,0,0.6); display:none; z-index:200; pointer-events:auto; cursor:pointer; backdrop-filter:blur(2px) }
        .overlay.show { display:block }
        .container { padding:16px; max-width:700px; margin:0 auto }
        .card { background:var(--cd); border:1px solid var(--bd); border-radius:12px; padding:16px; margin-bottom:14px }
        .card-title { font-size:11px; font-weight:700; color:var(--ac); text-transform:uppercase; letter-spacing:1px; margin-bottom:12px; display:flex; align-items:center; justify-content:space-between }
        .stat-row { display:flex; flex-wrap:wrap; gap:12px; margin-bottom:10px }
        .stat { flex:1; min-width:100px }
        .stat-label { font-size:10px; color:var(--t3); text-transform:uppercase; letter-spacing:0.5px }
        .stat-value { font-size:20px; font-weight:700; color:var(--tx); font-family:'SF Mono',ui-monospace,Consolas,monospace; font-variant-numeric:tabular-nums }
        .stat-sub { font-size:11px; color:var(--t3) }
        .progress-bar { height:4px; background:var(--bd); border-radius:2px; margin:6px 0 8px; overflow:hidden }
        .progress-fill { height:100%; background:var(--ac); border-radius:2px; transition:width 0.4s }
        .progress-fill.warn { background:var(--wn) }
        .progress-fill.danger { background:var(--er) }
        .badge { display:inline-block; padding:2px 8px; border-radius:10px; font-size:11px; font-weight:700 }
        .badge-ok { background:rgba(52,211,153,0.15); color:var(--ok) }
        .badge-err { background:rgba(248,113,113,0.15); color:var(--er) }
        .badge-idle { background:rgba(148,163,184,0.1); color:var(--t2) }
        .badge-busy { background:rgba(251,191,36,0.15); color:var(--wn) }
        .btn { padding:8px 16px; border:none; border-radius:8px; font-size:13px; font-weight:600; cursor:pointer; transition:all 0.2s }
        .btn-primary { background:var(--ac); color:var(--bg) }
        .btn-primary:hover { background:#06b6d4; box-shadow:0 0 16px rgba(34,211,238,0.3) }
        .btn-primary:disabled { background:var(--t3); color:var(--sf); cursor:not-allowed }
        .btn-danger { background:var(--er); color:white }
        .btn-danger:hover { background:#ef4444 }
        .btn-sm { padding:5px 10px; font-size:12px }
        .btn-outline { background:transparent; border:1px solid var(--b2); color:var(--t2) }
        .btn-outline:hover { border-color:var(--ac); color:var(--ac); background:var(--ag) }
        table { width:100%; border-collapse:collapse; font-size:13px }
        th { text-align:left; padding:6px 8px; border-bottom:1px solid var(--bd); font-size:10px; text-transform:uppercase; color:var(--t3); letter-spacing:0.5px }
        td { padding:7px 8px; border-bottom:1px solid rgba(26,39,68,0.5) }
        tr:last-child td { border-bottom:none }
        tr:hover td { background:rgba(34,211,238,0.03) }
        .empty-row { text-align:center; color:var(--t3); padding:20px; font-size:13px }
        .pagination { display:flex; align-items:center; gap:8px; justify-content:flex-end; margin-top:10px }
        .page-info { font-size:12px; color:var(--t3) }
        .danger-zone { border:1px solid rgba(248,113,113,0.3); background:rgba(248,113,113,0.04) }
        .danger-zone .card-title { color:var(--er) }
        .confirm-box { display:none; background:rgba(248,113,113,0.08); border:1px solid rgba(248,113,113,0.3); border-radius:8px; padding:12px; margin-top:10px; font-size:13px; color:var(--er) }
        .confirm-box.show { display:block }
        .confirm-actions { display:flex; gap:8px; margin-top:10px }
        .toast { position:fixed; top:60px; right:20px; padding:12px 20px; border-radius:8px; display:none; z-index:1000; box-shadow:0 8px 24px rgba(0,0,0,0.4); font-size:13px; max-width:350px; border:1px solid; backdrop-filter:blur(12px) }
        .toast-success { background:rgba(52,211,153,0.15); color:var(--ok); border-color:rgba(52,211,153,0.3) }
        .toast-error { background:rgba(248,113,113,0.15); color:var(--er); border-color:rgba(248,113,113,0.3) }
        .type-temp { color:#f97316 }
        .type-ec { color:var(--ac) }
        .type-ph { color:var(--ok) }
        .type-do { color:#a78bfa }
    </style>
</head>
<body>
    <div class="overlay" id="overlay" onclick="closeMenu()"></div>
    <div class="sidebar" id="sidebar">
        <div class="sidebar-header">Project SeaSense Data Logger</div>
        <ul class="sidebar-nav">
            <li><a href="/dashboard">Dashboard</a></li>
            <li><a href="/data" class="active">Data</a></li>
            <li><a href="/calibrate">Calibration</a></li>
            <li><a href="/settings">Settings</a></li>
        </ul>
    </div>
    <div class="header">
        <button class="hamburger" onclick="toggleMenu()">&#9776;</button>
        <div class="title">Project SeaSense Data Logger</div>
    </div>

    <div id="toast" class="toast"></div>

    <div class="container">
        <!-- Storage Stats -->
        <div class="card">
            <div class="card-title">Storage <span><button class="btn btn-sm btn-outline" onclick="window.location='/api/data/download'">Download CSV</button> <button class="btn btn-sm btn-outline" onclick="loadStats()">Refresh</button></span></div>
            <div class="stat-row" id="statsRow">
                <div class="stat"><div class="stat-label">Records</div><div class="stat-value" id="statRecords">--</div><div class="stat-sub" id="statPending">-- pending upload</div></div>
                <div class="stat"><div class="stat-label">SPIFFS Used</div><div class="stat-value" style="font-size:14px;padding-top:4px;" id="statSpiffs">--</div><div class="progress-bar"><div class="progress-fill" id="spiffsBar" style="width:0%"></div></div></div>
                <div class="stat"><div class="stat-label">SD Card</div><div class="stat-value" style="font-size:14px;padding-top:4px;" id="statSD">--</div><div class="progress-bar"><div class="progress-fill" id="sdBar" style="width:0%"></div></div></div>
            </div>
        </div>

        <!-- Upload Control -->
        <div class="card">
            <div class="card-title">Upload Control</div>
            <div class="stat-row">
                <div class="stat"><div class="stat-label">Status</div><div class="stat-value" style="font-size:15px;padding-top:3px;color:var(--tx);" id="upStatus"><span class="badge badge-idle">--</span></div></div>
                <div class="stat"><div class="stat-label">Pending</div><div class="stat-value" id="upPending">--</div><div class="stat-sub">records</div></div>
                <div class="stat"><div class="stat-label">Last Upload</div><div class="stat-value" style="font-size:14px;padding-top:4px;color:var(--tx);" id="upLast">--</div></div>
                <div class="stat"><div class="stat-label">Next Upload</div><div class="stat-value" style="font-size:14px;padding-top:4px;color:var(--tx);" id="upNext">--</div></div>
                <div class="stat"><div class="stat-label">Session Bandwidth</div><div class="stat-value" id="upBandwidth">--</div><div class="stat-sub">this session</div></div>
                <div class="stat"><div class="stat-label">Total Uploaded</div><div class="stat-value" id="upTotal">--</div><div class="stat-sub">all time</div></div>
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
            <p style="font-size:13px;color:var(--t2);margin-bottom:12px;">Permanently delete all stored sensor records from SPIFFS and SD card. This cannot be undone.</p>
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
        let _dataUpNextMs = 0, _dataUpFetchedAt = 0;
        function tickUpNext() {
            const elapsed = Date.now() - _dataUpFetchedAt;
            const remaining = Math.max(0, _dataUpNextMs - elapsed);
            document.getElementById('upNext').textContent = remaining > 0 ? fmtMs(remaining) : '--';
        }

        function toggleMenu() { document.getElementById('sidebar').classList.toggle('open'); document.getElementById('overlay').classList.toggle('show'); }
        function closeMenu()  { document.getElementById('sidebar').classList.remove('open'); document.getElementById('overlay').classList.remove('show'); }
        document.addEventListener('DOMContentLoaded', () => { document.getElementById('sidebar').addEventListener('click', e => e.stopPropagation()); });

        function fmtBytes(b) {
            if (b < 1024) return b + ' B';
            if (b < 1048576) return (b/1024).toFixed(1) + ' KB';
            if (b < 1073741824) return (b/1048576).toFixed(2) + ' MB';
            return (b/1073741824).toFixed(2) + ' GB';
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
        function fmtUTC(s) {
            if (!s) return '--';
            // Handle "YYYY-MM-DDTHH:MM:SSZ" or "YYYY-MM-DD HH:MM:SS"
            return s.replace('T',' ').replace('Z','');
        }
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
        function showToast(msg, type) {
            const toast = document.getElementById('toast');
            toast.textContent = msg;
            toast.className = 'toast toast-' + type;
            toast.style.display = 'block';
            setTimeout(() => { toast.style.display = 'none'; }, 5000);
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
                    let statusHtml = '<span class="badge ' + cls + '">' + status + '</span>';
                    if (u.last_error) {
                        statusHtml += '<div style="font-size:11px;color:#f87171;margin-top:4px;">' + u.last_error + '</div>';
                    }
                    document.getElementById('upStatus').innerHTML = statusHtml;
                    document.getElementById('upPending').textContent = u.pending_records != null ? u.pending_records : '--';

                    const lastMs = u.last_success_ms || 0;
                    const lastEpoch = u.last_success_epoch || 0;
                    let lastStr;
                    if (lastMs > 0 && uptimeMs > 0) lastStr = fmtAgo(uptimeMs - lastMs);
                    else if (lastEpoch > 0) lastStr = fmtAgo((Date.now()/1000 - lastEpoch) * 1000);
                    else lastStr = 'never';
                    document.getElementById('upLast').textContent = lastStr;
                    _dataUpNextMs = u.next_upload_ms || 0;
                    _dataUpFetchedAt = Date.now();
                    tickUpNext();
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
                    const totalUp = d.total_bytes_uploaded || 0;
                    document.getElementById('upTotal').textContent = fmtBytes(totalUp);
                    const tbody = document.getElementById('historyBody');
                    if (!d.history || d.history.length === 0) {
                        tbody.innerHTML = '<tr><td colspan="5" class="empty-row">No upload history</td></tr>';
                        return;
                    }
                    tbody.innerHTML = d.history.map(e => {
                        const cls = e.success ? 'badge-ok' : 'badge-err';
                        const lbl = e.success ? 'OK' : 'FAIL';
                        let time;
                        if (e.start_ms > 0 && uptimeMs > 0) time = fmtAgo(uptimeMs - e.start_ms);
                        else if (e.epoch > 0) time = fmtAgo((Date.now()/1000 - e.epoch) * 1000);
                        else time = '--';
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
            const ctrl = new AbortController();
            const timer = setTimeout(() => ctrl.abort(), 15000);
            fetch('/api/data/records?page=' + currentPage + '&limit=' + PAGE_SIZE, { signal: ctrl.signal })
                .then(r => { clearTimeout(timer); return r.json(); })
                .then(d => {
                    if (!d.records || d.records.length === 0) {
                        tbody.innerHTML = '<tr><td colspan="4" class="empty-row">No records stored yet</td></tr>';
                    } else {
                        tbody.innerHTML = d.records.map(r => {
                            const tc = typeClass(r.type);
                            let timeStr;
                            if (r.time) {
                                timeStr = fmtUTC(r.time);
                            } else if (uptimeMs > 0 && r.millis > 0 && r.millis <= uptimeMs) {
                                timeStr = fmtAgo(uptimeMs - r.millis);
                            } else {
                                timeStr = '--';
                            }
                            return '<tr><td style="font-size:11px;color:#94a3b8;">' + timeStr + '</td>'
                                + '<td class="' + tc + '">' + r.type + '</td>'
                                + '<td>' + fmtValue(r.value, r.type) + ' <span style="color:#475569;font-size:11px;">' + r.unit + '</span></td>'
                                + '<td style="font-size:11px;color:#94a3b8;">' + (r.quality||'--') + '</td></tr>';
                        }).join('');
                    }
                    const maxPage = Math.floor((d.total - 1) / PAGE_SIZE);
                    document.getElementById('pageInfo').textContent = 'Page ' + (currentPage + 1) + ' of ' + (maxPage + 1);
                    document.getElementById('prevBtn').disabled = currentPage >= maxPage;
                    document.getElementById('nextBtn').disabled = currentPage <= 0;
                })
                .catch(e => {
                    clearTimeout(timer);
                    const msg = e.name === 'AbortError' ? 'Request timed out — SPIFFS may be busy' : 'Error loading records';
                    tbody.innerHTML = '<tr><td colspan="4" class="empty-row">' + msg + '</td></tr>';
                });
        }

        function changePage(dir) {
            currentPage = Math.max(0, currentPage - dir);  // page 0 = most recent
            loadRecords();
        }

        function forceUpload() {
            const btn = document.getElementById('forceBtn');
            btn.disabled = true; btn.textContent = 'Uploading...';
            fetch('/api/upload/force', { method: 'POST' })
                .then(r => r.json())
                .then(d => {
                    let polls = 0;
                    const maxPolls = 15;
                    const pollId = setInterval(() => {
                        polls++;
                        fetch('/api/status').then(r => r.json()).then(s => {
                            const u = s.upload || {};
                            const st = (u.status || '').toLowerCase();
                            const done = !u.force_pending && st !== 'uploading' && st !== 'querying data' && st !== 'syncing time';
                            if (done || polls >= maxPolls) {
                                clearInterval(pollId);
                                btn.textContent = 'Force Upload Now';
                                btn.disabled = false;
                                loadStats(); loadHistory();
                                if (done && st === 'success') showToast('Upload completed successfully', 'success');
                                else if (done) showToast('Upload finished: ' + (u.status || 'unknown'), st.startsWith('error') ? 'error' : 'success');
                                else showToast('Upload still in progress — check history', 'error');
                            } else {
                                btn.textContent = 'Uploading... (' + polls + '/' + maxPolls + ')';
                            }
                        }).catch(() => {});
                    }, 2000);
                })
                .catch(() => { btn.textContent = 'Force Upload Now'; btn.disabled = false; showToast('Request failed', 'error'); });
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
                    showToast('All data flushed successfully', 'success');
                    currentPage = 0;
                    setTimeout(() => { loadStats(); loadRecords(); }, 500);
                })
                .catch(() => { showToast('Flush failed', 'error'); });
        }

        loadStats();
        loadHistory();
        loadRecords();
        setInterval(loadStats, 15000);
        setInterval(loadHistory, 15000);
        setInterval(tickUpNext, 1000);
        setInterval(function() { if (currentPage === 0) loadRecords(); }, 30000);
    </script>
</body>
</html>
)HTML";
    _server->send_P(200, "text/html", PAGE);
}

void SeaSenseWebServer::handleSettings() {
    static const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Settings - Project SeaSense</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        :root { --bg:#060a13; --sf:#0c1221; --cd:#111a2e; --bd:#1a2744; --b2:#243352; --ac:#22d3ee; --a2:#2dd4bf; --ag:rgba(34,211,238,0.12); --tx:#e2e8f0; --t2:#94a3b8; --t3:#475569; --ok:#34d399; --wn:#fbbf24; --er:#f87171 }
        * { margin:0; padding:0; box-sizing:border-box }
        body { font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif; background:var(--sf); color:var(--tx); -webkit-font-smoothing:antialiased; min-height:100vh }
        .header { background:var(--bg); padding:0 16px; height:52px; display:flex; align-items:center; border-bottom:1px solid var(--bd); position:sticky; top:0; z-index:100; box-shadow:0 4px 24px rgba(0,0,0,0.3) }
        .header::after { content:''; position:absolute; bottom:-1px; left:0; right:0; height:1px; background:linear-gradient(90deg,transparent,var(--ac),transparent); opacity:0.4 }
        .hamburger { background:none; border:none; color:var(--t2); font-size:22px; cursor:pointer; padding:8px; margin-right:12px; line-height:1; border-radius:6px; transition:all 0.2s; font-family:Arial,sans-serif }
        .hamburger:hover { color:var(--ac); background:var(--ag) }
        .title { font-size:14px; font-weight:600; color:var(--ac); text-transform:none }
        .sidebar { position:fixed; left:-260px; top:0; width:260px; height:100%; background:var(--bg); border-right:1px solid var(--bd); transition:left 0.3s ease; z-index:201; pointer-events:auto }
        .sidebar.open { left:0 }
        .sidebar-header { padding:20px; border-bottom:1px solid var(--bd); font-weight:600; color:var(--ac); font-size:13px; background:var(--bg); text-transform:none }
        .sidebar-nav { list-style:none; padding:8px 0 }
        .sidebar-nav a { display:block; padding:12px 20px; color:var(--t2); text-decoration:none; font-size:14px; font-weight:500; transition:all 0.2s; border-left:2px solid transparent; border-bottom:1px solid rgba(26,39,68,0.5) }
        .sidebar-nav a:hover { color:var(--tx); background:rgba(34,211,238,0.05) }
        .sidebar-nav a.active { color:var(--ac); border-left-color:var(--ac); background:rgba(34,211,238,0.08); font-weight:600 }
        .overlay { position:fixed; inset:0; background:rgba(0,0,0,0.6); display:none; z-index:200; pointer-events:auto; cursor:pointer; backdrop-filter:blur(2px) }
        .overlay.show { display:block }
        .container { padding:16px; max-width:640px; margin:0 auto }
        .section { background:var(--cd); padding:20px; margin:15px 0; border-radius:12px; border:1px solid var(--bd); position:relative; overflow:hidden }
        .section::before { content:''; position:absolute; left:0; top:0; bottom:0; width:3px; background:var(--ac) }
        .section h2 { margin-top:0; color:var(--ac); border-bottom:1px solid var(--bd); padding-bottom:10px; font-size:13px; font-weight:600; text-transform:uppercase; letter-spacing:1.5px }
        .section h3 { color:var(--t2); font-size:13px; font-weight:600; margin-top:18px; margin-bottom:4px; text-transform:uppercase; letter-spacing:0.5px }
        .form-group { margin:15px 0 }
        .form-group label { display:block; font-weight:600; margin-bottom:5px; color:var(--t2); font-size:13px }
        .form-group input, .form-group select { width:100%; padding:10px; border:1px solid var(--bd); border-radius:8px; font-size:14px; transition:all 0.2s; background:var(--bg); color:var(--tx) }
        .form-group input:focus, .form-group select:focus { outline:none; border-color:var(--ac); box-shadow:0 0 0 3px var(--ag) }
        .form-group input[readonly] { background:var(--sf); color:var(--t3); border-color:var(--bd); cursor:not-allowed; opacity:0.7 }
        .form-group input[readonly]:focus { border-color:var(--bd); box-shadow:none }
        .form-group input[type="checkbox"] { width:auto }
        .form-group small { color:var(--t3); font-size:12px; display:block; margin-top:5px }
        .btn { padding:10px 20px; border:none; border-radius:8px; cursor:pointer; font-size:14px; font-weight:600; margin:5px; transition:all 0.2s }
        .btn-primary { background:var(--ac); color:var(--bg) }
        .btn-primary:hover { background:#06b6d4; box-shadow:0 0 16px rgba(34,211,238,0.3) }
        .btn-danger { background:var(--er); color:white }
        .btn-danger:hover { background:#ef4444; box-shadow:0 0 16px rgba(248,113,113,0.3) }
        .btn-warning { background:var(--wn); color:var(--bg) }
        .btn-warning:hover { background:#f59e0b; box-shadow:0 0 16px rgba(251,191,36,0.3) }
        .toast { position:fixed; top:60px; right:20px; padding:12px 20px; border-radius:8px; display:none; z-index:1000; box-shadow:0 8px 24px rgba(0,0,0,0.4); font-size:13px; max-width:350px; border:1px solid; backdrop-filter:blur(12px) }
        .toast-success { background:rgba(52,211,153,0.15); color:var(--ok); border-color:rgba(52,211,153,0.3) }
        .toast-error { background:rgba(248,113,113,0.15); color:var(--er); border-color:rgba(248,113,113,0.3) }
        .toast-info { background:rgba(34,211,238,0.15); color:var(--ac); border-color:rgba(34,211,238,0.3) }
        .actions { text-align:center; margin-top:20px }
        .btn-sm { padding:5px 10px; font-size:11px; background:var(--b2); color:var(--tx); border-radius:6px; margin:0 }
        .btn-sm:hover { background:var(--bd) }
    </style>
</head>
<body>
    <div class="overlay" id="overlay" onclick="closeMenu()"></div>

    <div class="sidebar" id="sidebar">
        <div class="sidebar-header">Project SeaSense Data Logger</div>
        <ul class="sidebar-nav">
            <li><a href="/dashboard">Dashboard</a></li>
            <li><a href="/data">Data</a></li>
            <li><a href="/calibrate">Calibration</a></li>
            <li><a href="/settings" class="active">Settings</a></li>
        </ul>
    </div>

    <div class="header">
        <button class="hamburger" onclick="toggleMenu()">&#9776;</button>
        <div class="title">Project SeaSense Data Logger</div>
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
                <small>Leave empty for AP mode only. Device appears on the network as <strong id="hostnameHint"></strong></small>
            </div>
            <div class="form-group">
                <label>Station Password</label>
                <input type="password" id="wifi-password" name="wifi-password">
            </div>
            <div class="form-group">
                <label>AP Password</label>
                <input type="password" id="wifi-ap-password" name="wifi-ap-password">
                <small>Password for <strong id="apSsidHint"></strong> access point</small>
            </div>
        </div>

        <!-- API Configuration -->
        <div class="section">
            <h2>API Configuration</h2>
            <div class="form-group">
                <label>API Environment</label>
                <select id="api-url" name="api-url">
                    <option value="https://seasense.projectseasense.org">Live</option>
                    <option value="https://test-api.projectseasense.org">Test</option>
                </select>
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

        <!-- Sampling Configuration -->
        <div class="section">
            <h2>Sampling</h2>
            <div class="form-group">
                <label>Sensor Reading Interval</label>
                <div style="display:flex;gap:10px;align-items:center;">
                    <div style="display:flex;align-items:center;gap:4px;">
                        <input type="number" id="sensor-interval-min" min="0" max="1439" step="1" value="15" style="width:70px;background:var(--bg);color:var(--tx);border:1px solid var(--bd);border-radius:6px;padding:8px;">
                        <span style="font-size:13px;color:var(--t2);">min</span>
                    </div>
                    <div style="display:flex;align-items:center;gap:4px;">
                        <input type="number" id="sensor-interval-sec" min="0" max="59" step="1" value="0" style="width:60px;background:var(--bg);color:var(--tx);border:1px solid var(--bd);border-radius:6px;padding:8px;">
                        <span style="font-size:13px;color:var(--t2);">sec</span>
                    </div>
                </div>
                <small id="interval-hint">How often to pump and read sensors. Default: 15 min.</small>
            </div>
            <div class="form-group">
                <label style="display:flex;align-items:center;gap:8px;cursor:pointer;">
                    <input type="checkbox" id="skip-if-stationary" name="skip-if-stationary" style="width:auto;margin:0;">
                    Skip measurement cycle if boat has not moved
                </label>
            </div>

        </div>

        <!-- NMEA2000 Output -->
        <div class="section">
            <h2>NMEA2000 Output</h2>
            <div class="form-group">
                <label style="display:flex;align-items:center;gap:8px;cursor:pointer;">
                    <input type="checkbox" id="nmea-output-enabled" name="nmea-output-enabled" style="width:auto;margin:0;">
                    Enable outbound NMEA2000 PGN output
                </label>
                <small>Default is off. Keep disabled unless outbound PGNs are needed.</small>
            </div>
        </div>

        <!-- Deployment -->
        <div class="section">
            <h2>Deployment</h2>
            <div class="form-group">
                <label>Sensor Depth Below Waterline (cm)</label>
                <input type="number" id="deploy-depth" min="0" step="1" placeholder="e.g. 30">
                <small>How far below the waterline the sensor intake sits</small>
            </div>
            <div class="form-group">
                <label>Purchase Date</label>
                <input type="date" id="deploy-purchase-date">
                <small>When the device/sensors were purchased</small>
            </div>
            <div class="form-group">
                <label>Deploy Date</label>
                <input type="text" id="deploy-deploy-date" readonly>
                <small>Auto-stamped on first boot. Read-only.</small>
            </div>
        </div>

        <!-- Device Configuration -->
        <div class="section">
            <h2>Device Configuration</h2>
            <div class="form-group">
                <label>Device GUID</label>
                <div style="display:flex;gap:8px;">
                    <input type="text" id="device-guid" name="device-guid" readonly style="flex:1;">
                    <button type="button" class="btn" onclick="regenerateGUID()" style="white-space:nowrap;">Regenerate</button>
                </div>
            </div>
            <div class="form-group">
                <label>Partner ID</label>
                <input type="text" id="partner-id" name="partner-id" readonly>
            </div>
            <div class="form-group">
                <label>Firmware Version</label>
                <input type="text" id="firmware-version" name="firmware-version" readonly>
            </div>
        </div>

        <!-- Actions -->
        <div class="actions">
            <button type="submit" class="btn btn-primary">Save Configuration</button>
            <button type="button" class="btn btn-warning" onclick="resetConfig()">Reset to Defaults</button>
            <button type="button" class="btn btn-danger" onclick="restartDevice()">Restart Device</button>
        </div>
        </form>
    </div>

    <div id="restartModal" style="display:none;position:fixed;inset:0;background:rgba(0,0,0,0.7);z-index:500;backdrop-filter:blur(4px);align-items:center;justify-content:center;">
        <div style="background:var(--cd);border:1px solid var(--bd);border-radius:12px;padding:24px;max-width:380px;margin:20px;text-align:center;">
            <div style="font-size:15px;font-weight:600;color:var(--ac);margin-bottom:12px;">Restart Required</div>
            <p style="font-size:13px;color:var(--t2);margin-bottom:20px;">WiFi settings were changed. A restart is needed to apply them.</p>
            <div style="display:flex;gap:10px;justify-content:center;">
                <button class="btn btn-danger" onclick="restartDevice()">Restart Now</button>
                <button class="btn" onclick="closeRestartModal()" style="background:var(--b2);color:var(--tx);">Later</button>
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

        let _initWifi = {};

        function closeRestartModal() {
            document.getElementById('restartModal').style.display = 'none';
        }

        async function loadConfig() {
            try {
                const config = await fetch('/api/config').then(r => r.json());
                const status = await fetch('/api/status').then(r => r.json());
                const apSsid = (status.wifi && status.wifi.ap_ssid) || '';
                const hn = document.getElementById('hostnameHint');
                if (hn && apSsid) hn.textContent = apSsid;
                const ah = document.getElementById('apSsidHint');
                if (ah && apSsid) ah.textContent = apSsid;

                // Store initial WiFi values for restart detection
                _initWifi = { ssid: config.wifi.station_ssid || '', password: config.wifi.station_password || '', ap_password: config.wifi.ap_password || '' };

                // WiFi
                document.getElementById('wifi-ssid').value = config.wifi.station_ssid || '';
                document.getElementById('wifi-password').value = config.wifi.station_password || '';
                document.getElementById('wifi-ap-password').value = config.wifi.ap_password || '';

                // API
                document.getElementById('api-url').value = config.api.url || 'https://seasense.projectseasense.org';
                document.getElementById('api-interval').value = (config.api.upload_interval_ms / 60000) || 5;
                document.getElementById('api-batch').value = config.api.batch_size || 100;
                document.getElementById('api-retries').value = config.api.max_retries || 5;

                // Sampling
                if (config.sampling) {
                    const ms = config.sampling.sensor_interval_ms || 900000;
                    document.getElementById('sensor-interval-min').value = Math.floor(ms / 60000);
                    document.getElementById('sensor-interval-sec').value = Math.round((ms % 60000) / 1000);
                    document.getElementById('skip-if-stationary').checked = !!config.sampling.skip_if_stationary;

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

                // NMEA output
                document.getElementById('nmea-output-enabled').checked = !!(
                    (config.nmea && config.nmea.output_enabled)
                    || (config.gps && config.gps.nmea_output_enabled) // backward-compat
                );

                // Deployment
                if (config.deployment) {
                    document.getElementById('deploy-depth').value = config.deployment.depth_cm || '';
                    document.getElementById('deploy-purchase-date').value = config.deployment.purchase_date || '';
                    document.getElementById('deploy-deploy-date').value = config.deployment.deploy_date || 'Not set';
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
                    upload_interval_ms: parseInt(document.getElementById('api-interval').value) * 60000,
                    batch_size: parseInt(document.getElementById('api-batch').value),
                    max_retries: parseInt(document.getElementById('api-retries').value)
                },
                sampling: {
                    sensor_interval_ms: (parseInt(document.getElementById('sensor-interval-min').value || 0) * 60
                        + parseInt(document.getElementById('sensor-interval-sec').value || 0)) * 1000,
                    skip_if_stationary: document.getElementById('skip-if-stationary').checked
                },
                nmea: {
                    output_enabled: document.getElementById('nmea-output-enabled').checked
                },
                deployment: {
                    depth_cm: parseFloat(document.getElementById('deploy-depth').value) || 0,
                    purchase_date: document.getElementById('deploy-purchase-date').value || ''
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
                    showToast('Configuration saved.', 'success');
                    // Check if WiFi credentials changed — these need a restart
                    const wifiChanged = config.wifi.station_ssid !== _initWifi.ssid
                        || config.wifi.station_password !== _initWifi.password
                        || config.wifi.ap_password !== _initWifi.ap_password;
                    if (wifiChanged) {
                        document.getElementById('restartModal').style.display = 'flex';
                    }
                    // Update stored values so modal doesn't re-trigger on next save
                    _initWifi = { ssid: config.wifi.station_ssid, password: config.wifi.station_password, ap_password: config.wifi.ap_password };
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

        async function regenerateGUID() {
            if (!confirm('Generate a new Device GUID? The old one cannot be recovered.')) return;
            try {
                const r = await fetch('/api/device/regenerate-guid', {method:'POST'});
                const d = await r.json();
                if (r.ok) {
                    document.getElementById('device-guid').value = d.device_guid;
                    showToast('Device GUID regenerated.', 'success');
                } else {
                    showToast('Failed to regenerate GUID.', 'error');
                }
            } catch (e) {
                showToast('Error: ' + e.message, 'error');
            }
        }

        async function restartDevice() {
            if (!confirm('Restart the device? This will apply WiFi and API changes.')) return;

            try {
                await fetch('/api/system/restart', {method: 'POST'});
                showToast('Device restarting... Reconnect in 30 seconds.', 'info');
                setTimeout(() => {
                    document.body.innerHTML = '<div style="text-align:center;padding:50px;color:#e2e8f0;"><h2 style="color:#22d3ee;">Device Restarting...</h2><p style="color:#94a3b8;">Please wait 30 seconds and refresh the page.</p></div>';
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

    _server->send_P(200, "text/html", PAGE);
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
    } else if (sensorType == "ph" && _phSensor && _phSensor->isEnabled()) {
        sendJSON(sensorToJSON(_phSensor));
    } else if (sensorType == "dissolved_oxygen" && _doSensor && _doSensor->isEnabled()) {
        sendJSON(sensorToJSON(_doSensor));
    } else {
        sendError("Unknown or unavailable sensor type");
    }
}

void SeaSenseWebServer::handleApiSensorRead() {
    if (_server->method() != HTTP_POST) {
        sendError("Method not allowed", 405);
        return;
    }

    // Acquire I2C mutex to prevent collision with loop() sensor reads
    extern SemaphoreHandle_t g_i2cMutex;
    bool locked = (g_i2cMutex != NULL) && xSemaphoreTake(g_i2cMutex, pdMS_TO_TICKS(500));
    if (!locked) {
        sendError("I2C bus busy, try again", 503);
        return;
    }

    // Force read all sensors
    bool tempSuccess = false;
    bool ecSuccess = false;
    bool phSuccess = false;
    bool doSuccess = false;

    if (_tempSensor && _tempSensor->isEnabled()) {
        tempSuccess = _tempSensor->read();

        // Set temperature compensation for other sensors
        if (tempSuccess) {
            SensorData tempData = _tempSensor->getData();
            if (_ecSensor) _ecSensor->setTemperatureCompensation(tempData.value);
            if (_phSensor && _phSensor->isEnabled()) _phSensor->setTemperatureCompensation(tempData.value);
            if (_doSensor && _doSensor->isEnabled()) _doSensor->setTemperatureCompensation(tempData.value);
        }
    }

    if (_ecSensor && _ecSensor->isEnabled()) {
        ecSuccess = _ecSensor->read();
        if (ecSuccess && _doSensor && _doSensor->isEnabled()) {
            _doSensor->setSalinityCompensation(_ecSensor->getSalinity());
        }
    }

    if (_phSensor && _phSensor->isEnabled()) {
        phSuccess = _phSensor->read();
    }

    if (_doSensor && _doSensor->isEnabled()) {
        doSuccess = _doSensor->read();
    }

    xSemaphoreGive(g_i2cMutex);

    sendJSON("{\"success\":true,\"temperature\":" + String(tempSuccess ? "true" : "false") +
             ",\"conductivity\":" + String(ecSuccess ? "true" : "false") +
             ",\"ph\":" + String(phSuccess ? "true" : "false") +
             ",\"dissolved_oxygen\":" + String(doSuccess ? "true" : "false") + "}");
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
    } else if (sensorType == "ph") {
        if (calType == "mid") {
            calibrationType = CalibrationType::PH_MID;
        } else if (calType == "low") {
            calibrationType = CalibrationType::PH_LOW;
        } else if (calType == "high") {
            calibrationType = CalibrationType::PH_HIGH;
        }
    } else if (sensorType == "dissolved_oxygen") {
        if (calType == "atmospheric") {
            calibrationType = CalibrationType::DO_ATMOSPHERIC;
        } else if (calType == "zero") {
            calibrationType = CalibrationType::DO_ZERO;
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

void SeaSenseWebServer::handleApiDataLatest() {
    StorageStats stats = _storage->getStats();
    uint32_t total = stats.totalRecords;
    if (total == 0) {
        sendJSON("{\"sensors\":[]}");
        return;
    }

    uint32_t skip = total > 20 ? total - 20 : 0;
    std::vector<DataRecord> recs = _storage->readRecords(0, 20, skip);

    // Collect most recent value per sensor type (iterate in reverse)
    JsonDocument doc;
    JsonArray sensors = doc["sensors"].to<JsonArray>();
    std::vector<String> seen;

    for (int i = (int)recs.size() - 1; i >= 0; i--) {
        bool found = false;
        for (const auto& s : seen) { if (s == recs[i].sensorType) { found = true; break; } }
        if (found) continue;
        seen.push_back(recs[i].sensorType);

        JsonObject obj = sensors.add<JsonObject>();
        obj["type"] = recs[i].sensorType;
        obj["value"] = recs[i].value;
        obj["unit"] = recs[i].unit;
        obj["quality"] = recs[i].quality;
    }

    String json;
    serializeJson(doc, json);
    sendJSON(json);
}

void SeaSenseWebServer::handleApiDataDownload() {
    if (!SPIFFS.exists("/data.csv")) {
        sendError("No data file found", 404);
        return;
    }
    File file = SPIFFS.open("/data.csv", FILE_READ);
    if (!file) {
        sendError("Failed to open data file", 500);
        return;
    }
    _server->sendHeader("Content-Disposition", "attachment; filename=\"seasense-data.csv\"");
    _server->streamFile(file, "text/csv");
    file.close();
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

    // Read the full SPIFFS circular buffer so we can serve the most-recent
    // records from the tail, regardless of page number.
    // NOTE: Limiting this to 200 caused "recent data missing" once total>200,
    // because only the oldest records were loaded.
    std::vector<DataRecord> recs = _storage->readRecords(0, SPIFFS_CIRCULAR_BUFFER_SIZE);

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
    doc["total_bytes_uploaded"] = _storage->getTotalBytesUploaded();
    JsonArray arr = doc["history"].to<JsonArray>();

    if (count > 0) {
        // Serve in-memory history (millis-based, current session)
        for (uint8_t i = 0; i < count; i++) {
            uint8_t idx = (head + APIUploader::UPLOAD_HISTORY_SIZE - 1 - i) % APIUploader::UPLOAD_HISTORY_SIZE;
            JsonObject e = arr.add<JsonObject>();
            e["start_ms"]    = hist[idx].startMs;
            e["duration_ms"] = hist[idx].durationMs;
            e["success"]     = hist[idx].success;
            e["record_count"]  = hist[idx].recordCount;
            e["payload_bytes"] = hist[idx].payloadBytes;
        }
    } else {
        // Fallback: serve persisted history (epoch-based, survives reboots)
        uint8_t pCount = 0, pHead = 0;
        const SPIFFSStorage::PersistedUploadRecord* phist = _storage->getUploadHistory(pCount, pHead);
        if (phist && pCount > 0) {
            for (uint8_t i = 0; i < pCount; i++) {
                uint8_t idx = (pHead + SPIFFSStorage::MAX_UPLOAD_HISTORY - 1 - i) % SPIFFSStorage::MAX_UPLOAD_HISTORY;
                JsonObject e = arr.add<JsonObject>();
                e["epoch"]       = phist[idx].epochTime;
                e["duration_ms"] = phist[idx].durationMs;
                e["success"]     = phist[idx].success;
                e["record_count"]  = phist[idx].recordCount;
                e["payload_bytes"] = phist[idx].payloadBytes;
            }
        }
    }

    String json;
    serializeJson(doc, json);
    sendJSON(json);
}

void SeaSenseWebServer::handleApiDeviceRegenerateGuid() {
    if (_server->method() != HTTP_POST) { sendError("POST required", 405); return; }
    if (!_configManager) { sendError("Configuration manager not available", 503); return; }
    String newGUID = _configManager->regenerateDeviceGUID();
    // Update the live APIUploader config so the next upload uses the new GUID
    extern APIUploader apiUploader;
    apiUploader.setDeviceGUID(newGUID);
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
    apiObj["upload_interval_ms"] = api.uploadInterval;
    apiObj["batch_size"] = api.batchSize;
    apiObj["max_retries"] = api.maxRetries;

    // Sampling config
    ConfigManager::SamplingConfig sampling = _configManager->getSamplingConfig();
    JsonObject samplingObj = doc["sampling"].to<JsonObject>();
    samplingObj["sensor_interval_ms"] = sampling.sensorIntervalMs;
    samplingObj["skip_if_stationary"] = sampling.skipIfStationary;
    samplingObj["stationary_delta_meters"] = sampling.stationaryDeltaMeters;

    // GPS config
    ConfigManager::GPSConfig gps = _configManager->getGPSConfig();
    JsonObject gpsObj = doc["gps"].to<JsonObject>();
    gpsObj["use_nmea2000"] = gps.useNMEA2000;
    gpsObj["fallback_to_onboard"] = gps.fallbackToOnboard;

    // NMEA output config
    ConfigManager::NMEAConfig nmea = _configManager->getNMEAConfig();
    JsonObject nmeaObj = doc["nmea"].to<JsonObject>();
    nmeaObj["output_enabled"] = nmea.outputEnabled;

    // Minimum sampling interval = full pump cycle duration (calculated from current pump config)
    {
        PumpConfig pc = _configManager->getPumpConfig();
        unsigned long minMs = (unsigned long)pc.flushDurationMs + pc.measureDurationMs;
        samplingObj["min_sampling_ms"] = max(minMs, 5000UL);
    }

    // Deployment metadata
    ConfigManager::DeploymentConfig dep = _configManager->getDeploymentConfig();
    JsonObject depObj = doc["deployment"].to<JsonObject>();
    depObj["depth_cm"] = dep.depthCm;
    depObj["purchase_date"] = dep.purchaseDate;
    depObj["deploy_date"] = dep.deployDate;

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

    // Update API config (preserve API key — set in firmware, not UI)
    if (doc["api"].is<JsonObject>()) {
        ConfigManager::APIConfig api = _configManager->getAPIConfig();
        api.url = doc["api"]["url"] | api.url;
        api.uploadInterval = doc["api"]["upload_interval_ms"] | 300000;
        api.batchSize = doc["api"]["batch_size"] | 100;
        api.maxRetries = doc["api"]["max_retries"] | 5;
        _configManager->setAPIConfig(api);
    }

    // Update sampling config
    if (doc["sampling"].is<JsonObject>()) {
        // Calculate minimum from current pump config
        PumpConfig pc = _configManager->getPumpConfig();
        unsigned long minSamplingMs = (unsigned long)pc.flushDurationMs + pc.measureDurationMs;
        minSamplingMs = max(minSamplingMs, 5000UL);

        ConfigManager::SamplingConfig sampling = _configManager->getSamplingConfig();
        sampling.sensorIntervalMs = doc["sampling"]["sensor_interval_ms"] | 900000UL;
        sampling.sensorIntervalMs = max(sampling.sensorIntervalMs, minSamplingMs);
        sampling.skipIfStationary = doc["sampling"]["skip_if_stationary"] | false;
        // keep/allow threshold updates for forward compatibility
        sampling.stationaryDeltaMeters = doc["sampling"]["stationary_delta_meters"] | sampling.stationaryDeltaMeters;
        _configManager->setSamplingConfig(sampling);

        // Apply immediately: update globals so dashboard countdown reflects new interval
        extern unsigned long sensorSamplingIntervalMs;
        extern bool skipMeasurementIfStationary;
        extern float stationaryDeltaMeters;
        extern unsigned long lastSensorReadAt;
        extern portMUX_TYPE g_timerMux;
        sensorSamplingIntervalMs = sampling.sensorIntervalMs;
        skipMeasurementIfStationary = sampling.skipIfStationary;
        stationaryDeltaMeters = sampling.stationaryDeltaMeters;
        portENTER_CRITICAL(&g_timerMux);
        lastSensorReadAt = millis();  // reschedule from now
        portEXIT_CRITICAL(&g_timerMux);
    }

    // Update GPS config
    if (doc["gps"].is<JsonObject>()) {
        ConfigManager::GPSConfig gps = _configManager->getGPSConfig();
        gps.useNMEA2000 = doc["gps"]["use_nmea2000"] | gps.useNMEA2000;
        gps.fallbackToOnboard = doc["gps"]["fallback_to_onboard"] | gps.fallbackToOnboard;
        _configManager->setGPSConfig(gps);
    }

    // Update NMEA output config (preferred path: nmea.output_enabled).
    // Backward-compat: still accept gps.nmea_output_enabled if provided.
    {
        ConfigManager::NMEAConfig nmea = _configManager->getNMEAConfig();
        bool hasValue = false;
        if (doc["nmea"].is<JsonObject>()) {
            nmea.outputEnabled = doc["nmea"]["output_enabled"] | false;
            hasValue = true;
        } else if (doc["gps"].is<JsonObject>() && !doc["gps"]["nmea_output_enabled"].isNull()) {
            nmea.outputEnabled = doc["gps"]["nmea_output_enabled"] | false;
            hasValue = true;
        }

        if (hasValue) {
            _configManager->setNMEAConfig(nmea);
            // Apply immediately for runtime NMEA output gate
            extern bool nmeaOutputEnabled;
            nmeaOutputEnabled = nmea.outputEnabled;
        }
    }

    // Update device config
    if (doc["device"].is<JsonObject>()) {
        ConfigManager::DeviceConfig device;
        device.deviceGUID = doc["device"]["device_guid"] | "";
        device.partnerID = doc["device"]["partner_id"] | PARTNER_ID_DEFAULT;
        device.firmwareVersion = FIRMWARE_VERSION;  // Always use compiled-in version
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

    // Loop breadcrumbs (from main loop)
    extern volatile unsigned long g_lastLoopStartMs;
    extern volatile unsigned long g_maxLoopGapMs;
    extern const char* g_loopStage;
    doc["runtime"]["loop_stage"] = g_loopStage ? g_loopStage : "unknown";
    doc["runtime"]["last_loop_start_ms"] = (unsigned long)g_lastLoopStartMs;
    doc["runtime"]["max_loop_gap_ms"] = (unsigned long)g_maxLoopGapMs;

    // GPS status (via extern globals from main sketch)
    extern bool activeGPSHasValidFix();
    extern GPSData activeGPSGetData();
    extern NMEA2000GPS n2kGPS;
    doc["gps"]["has_fix"] = activeGPSHasValidFix();
    doc["gps"]["source"] = n2kGPS.hasValidFix() ? "nmea2000" : "onboard";
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
    doc["upload"]["last_success_epoch"] = _storage->getLastSuccessEpoch();
    doc["upload"]["last_attempt_ms"] = apiUploader.getLastAttemptTime();
    doc["upload"]["last_error"] = apiUploader.getLastError();
    doc["upload"]["force_pending"] = apiUploader.isForcePending();
    doc["upload"]["retry_count"] = apiUploader.getRetryCount();
    doc["upload"]["next_upload_ms"] = apiUploader.getTimeUntilNext();
    doc["upload"]["total_bytes_uploaded"] = _storage->getTotalBytesUploaded();
    if (apiUploader.getLastError().length() > 0) {
        doc["upload"]["last_error"] = apiUploader.getLastError();
    }

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
    extern NMEA2000Environment n2kEnv;

    N2kEnvironmentData env = n2kEnv.getSnapshot();
    JsonDocument doc;
    doc["has_any"] = n2kEnv.hasAnyData();

    // Wind
    JsonObject wind = doc["wind"].to<JsonObject>();
    if (!isnan(env.windSpeedTrue))     wind["speed_true"] = serialized(String(env.windSpeedTrue, 1));
    if (!isnan(env.windAngleTrue))     wind["angle_true"] = serialized(String(env.windAngleTrue, 0));
    if (!isnan(env.windSpeedApparent)) wind["speed_app"] = serialized(String(env.windSpeedApparent, 1));
    if (!isnan(env.windAngleApparent)) wind["angle_app"] = serialized(String(env.windAngleApparent, 0));

    // Water
    JsonObject water = doc["water"].to<JsonObject>();
    if (!isnan(env.waterDepth))        water["depth"] = serialized(String(env.waterDepth, 1));
    if (!isnan(env.speedThroughWater)) water["stw"] = serialized(String(env.speedThroughWater, 1));
    if (!isnan(env.waterTempExternal)) water["temp_ext"] = serialized(String(env.waterTempExternal, 1));

    // Atmosphere
    JsonObject atmo = doc["atmosphere"].to<JsonObject>();
    if (!isnan(env.airTemp))      atmo["air_temp"] = serialized(String(env.airTemp, 1));
    if (!isnan(env.baroPressure)) atmo["pressure_hpa"] = serialized(String(env.baroPressure / 100.0f, 1));
    if (!isnan(env.humidity))     atmo["humidity"] = serialized(String(env.humidity, 0));

    // Navigation
    JsonObject nav = doc["navigation"].to<JsonObject>();
    if (!isnan(env.cogTrue)) nav["cog"] = serialized(String(env.cogTrue, 0));
    if (!isnan(env.sog))    nav["sog"] = serialized(String(env.sog, 1));
    if (!isnan(env.heading)) nav["heading"] = serialized(String(env.heading, 0));

    // Attitude
    JsonObject att = doc["attitude"].to<JsonObject>();
    if (!isnan(env.pitch)) att["pitch"] = serialized(String(env.pitch, 1));
    if (!isnan(env.roll))  att["roll"] = serialized(String(env.roll, 1));

    String json;
    serializeJson(doc, json);
    sendJSON(json);
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
    } else if (state == PumpState::FLUSHING) {
        doc["state"] = "FLUSHING";
    } else if (state == PumpState::MEASURING) {
        doc["state"] = "MEASURING";
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
        // Persist enabled state to config
        if (_configManager) {
            PumpConfig pc = _configManager->getPumpConfig();
            pc.enabled = true;
            _configManager->setPumpConfig(pc);
            _configManager->save();
        }
        sendJSON("{\"success\":true,\"message\":\"Pump controller enabled\"}");
    } else if (action == "disable") {
        _pumpController->setEnabled(false);
        // Persist disabled state to config
        if (_configManager) {
            PumpConfig pc = _configManager->getPumpConfig();
            pc.enabled = false;
            _configManager->setPumpConfig(pc);
            _configManager->save();
        }
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
    doc["flush_duration_ms"] = config.flushDurationMs;
    doc["measure_duration_ms"] = config.measureDurationMs;
    doc["cycle_interval_ms"] = config.cycleIntervalMs;
    doc["max_on_time_ms"] = config.maxPumpOnTimeMs;

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
    config.flushDurationMs = doc["flush_duration_ms"] | PUMP_FLUSH_DURATION_MS;
    config.measureDurationMs = doc["measure_duration_ms"] | PUMP_MEASURE_DURATION_MS;
    config.cycleIntervalMs = doc["cycle_interval_ms"] | PUMP_CYCLE_INTERVAL_MS;
    config.maxPumpOnTimeMs = doc["max_on_time_ms"] | PUMP_MAX_ON_TIME_MS;

    _configManager->setPumpConfig(config);

    // Apply config to running PumpController immediately
    _pumpController->setConfig(config);

    // Save to SPIFFS
    if (_configManager->save()) {
        sendJSON("{\"success\":true,\"message\":\"Pump configuration saved and applied\"}");
    } else {
        sendError("Failed to save pump configuration", 500);
    }
}

void SeaSenseWebServer::handleApiMeasurement() {
    extern unsigned long lastSensorReadAt;
    extern unsigned long sensorSamplingIntervalMs;
    extern portMUX_TYPE g_timerMux;

    unsigned long now = millis();
    unsigned long remaining;
    if (_pumpController && _pumpController->isEnabled()) {
        remaining = _pumpController->getTimeUntilNextMeasurementMs();
    } else {
        portENTER_CRITICAL(&g_timerMux);
        unsigned long lastRead = lastSensorReadAt;
        portEXIT_CRITICAL(&g_timerMux);
        unsigned long elapsed = now - lastRead;
        remaining = (elapsed >= sensorSamplingIntervalMs) ? 0 : (sensorSamplingIntervalMs - elapsed);
    }

    JsonDocument resp;
    resp["next_read_in_ms"] = remaining;
    resp["interval_ms"] = (_pumpController && _pumpController->isEnabled())
        ? _pumpController->getCycleInterval()
        : sensorSamplingIntervalMs;

    // Include pump cycle phase for dashboard status label (with countdown seconds)
    if (_pumpController) {
        PumpState ps = _pumpController->getState();
        const char* phaseName = nullptr;
        switch (ps) {
            case PumpState::FLUSHING:  phaseName = "Flushing pipe"; break;
            case PumpState::MEASURING: phaseName = "Measuring";     break;
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
        salinityObj["clamped"] = _ecSensor->isSalinityClamped();
    }

    if (_phSensor && _phSensor->isEnabled()) {
        SensorData data = _phSensor->getData();
        JsonObject sensor = sensors.add<JsonObject>();
        sensor["type"] = data.sensorType;
        sensor["model"] = data.sensorModel;
        sensor["value"] = data.value;
        sensor["unit"] = data.unit;
        sensor["quality"] = sensorQualityToString(data.quality);
    }

    if (_doSensor && _doSensor->isEnabled()) {
        SensorData data = _doSensor->getData();
        JsonObject sensor = sensors.add<JsonObject>();
        sensor["type"] = data.sensorType;
        sensor["model"] = data.sensorModel;
        sensor["value"] = data.value;
        sensor["unit"] = data.unit;
        sensor["quality"] = sensorQualityToString(data.quality);
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
