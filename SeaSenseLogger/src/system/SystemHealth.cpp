/**
 * SeaSense Logger - System Health Monitor
 *
 * Manages watchdog, boot loop protection, and persistent error tracking.
 */

#include "SystemHealth.h"

static const char* NVS_NAMESPACE = "seasense";
static const char* KEY_REBOOT_COUNT = "reboot_cnt";
static const char* KEY_CONSEC_REBOOT = "consec_boot";
static const char* KEY_SENSOR_ERRORS = "sensor_err";
static const char* KEY_SD_ERRORS = "sd_err";
static const char* KEY_API_ERRORS = "api_err";
static const char* KEY_WIFI_ERRORS = "wifi_err";

SystemHealth::SystemHealth()
    : _safeMode(false)
    , _nvsReady(false)
    , _consecutiveRebootCleared(false)
    , _rebootCount(0)
    , _consecutiveReboots(0)
    , _bootLoopWindowMs(120000)
    , _resetReason(ESP_RST_UNKNOWN)
    , _sensorErrors(0)
    , _sdErrors(0)
    , _apiErrors(0)
    , _wifiErrors(0)
    , _nvsHandle(0) {
}

bool SystemHealth::begin(uint32_t wdtTimeoutMs, uint8_t bootLoopThreshold, uint32_t bootLoopWindowMs) {
    _bootLoopWindowMs = bootLoopWindowMs;
    _resetReason = esp_reset_reason();

    Serial.print("[HEALTH] Reset reason: ");
    Serial.println(getResetReasonString());

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("[HEALTH] NVS partition issue, erasing...");
        nvs_flash_erase();
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        Serial.println("[HEALTH] NVS init failed - running without persistence");
        _nvsReady = false;
    } else {
        _nvsReady = openNVS();
    }

    // Read persistent counters
    if (_nvsReady) {
        _rebootCount = readNVS(KEY_REBOOT_COUNT);
        _consecutiveReboots = readNVS(KEY_CONSEC_REBOOT);
        _sensorErrors = readNVS(KEY_SENSOR_ERRORS);
        _sdErrors = readNVS(KEY_SD_ERRORS);
        _apiErrors = readNVS(KEY_API_ERRORS);
        _wifiErrors = readNVS(KEY_WIFI_ERRORS);

        // Increment counters
        _rebootCount++;
        _consecutiveReboots++;

        writeNVS(KEY_REBOOT_COUNT, _rebootCount);
        writeNVS(KEY_CONSEC_REBOOT, _consecutiveReboots);
        nvs_commit(_nvsHandle);
    }

    Serial.print("[HEALTH] Boot #");
    Serial.print(_rebootCount);
    Serial.print(" (consecutive: ");
    Serial.print(_consecutiveReboots);
    Serial.println(")");

    // Boot loop detection
    if (_consecutiveReboots >= bootLoopThreshold) {
        _safeMode = true;
        Serial.println("[HEALTH] *** SAFE MODE *** Too many consecutive reboots!");
        Serial.println("[HEALTH] Only AP WiFi + web server will be started.");
    }

    // Initialize hardware watchdog
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = wdtTimeoutMs,
        .idle_core_mask = 0,
        .trigger_panic = true
    };

    err = esp_task_wdt_reconfigure(&wdt_config);
    if (err != ESP_OK) {
        err = esp_task_wdt_init(&wdt_config);
    }

    if (err == ESP_OK) {
        esp_task_wdt_add(NULL);  // Add current task (Arduino loopTask)
        Serial.print("[HEALTH] Watchdog enabled (");
        Serial.print(wdtTimeoutMs / 1000);
        Serial.println("s timeout)");
    } else {
        Serial.print("[HEALTH] Watchdog init failed: ");
        Serial.println(err);
    }

    return true;
}

void SystemHealth::feedWatchdog() {
    esp_task_wdt_reset();

    // After stable operation, clear the consecutive reboot counter
    if (!_consecutiveRebootCleared && millis() > _bootLoopWindowMs) {
        _consecutiveRebootCleared = true;
        if (_nvsReady) {
            writeNVS(KEY_CONSEC_REBOOT, 0);
            nvs_commit(_nvsHandle);
            Serial.println("[HEALTH] Stable operation confirmed - consecutive reboot counter cleared");
        }
    }
}

void SystemHealth::recordError(ErrorType type) {
    uint32_t* counter = nullptr;
    const char* key = nullptr;

    switch (type) {
        case ErrorType::SENSOR:
            counter = &_sensorErrors;
            key = KEY_SENSOR_ERRORS;
            break;
        case ErrorType::SD:
            counter = &_sdErrors;
            key = KEY_SD_ERRORS;
            break;
        case ErrorType::API:
            counter = &_apiErrors;
            key = KEY_API_ERRORS;
            break;
        case ErrorType::WIFI:
            counter = &_wifiErrors;
            key = KEY_WIFI_ERRORS;
            break;
    }

    if (counter && key) {
        (*counter)++;
        if (_nvsReady) {
            writeNVS(key, *counter);
            nvs_commit(_nvsHandle);
        }
    }
}

uint32_t SystemHealth::getErrorCount(ErrorType type) const {
    switch (type) {
        case ErrorType::SENSOR: return _sensorErrors;
        case ErrorType::SD:     return _sdErrors;
        case ErrorType::API:    return _apiErrors;
        case ErrorType::WIFI:   return _wifiErrors;
        default:                return 0;
    }
}

String SystemHealth::getResetReasonString() const {
    switch (_resetReason) {
        case ESP_RST_POWERON:  return "Power-on";
        case ESP_RST_EXT:      return "External reset";
        case ESP_RST_SW:       return "Software reset";
        case ESP_RST_PANIC:    return "Exception/panic";
        case ESP_RST_INT_WDT:  return "Interrupt watchdog";
        case ESP_RST_TASK_WDT: return "Task watchdog";
        case ESP_RST_WDT:      return "Other watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
        case ESP_RST_BROWNOUT: return "Brownout";
        case ESP_RST_SDIO:     return "SDIO";
        default:               return "Unknown";
    }
}

void SystemHealth::clearSafeMode() {
    if (_nvsReady) {
        writeNVS(KEY_CONSEC_REBOOT, 0);
        nvs_commit(_nvsHandle);
        Serial.println("[HEALTH] Safe mode cleared - will boot normally on next restart");
    }
    _safeMode = false;
    _consecutiveReboots = 0;
}

void SystemHealth::resetAllCounters() {
    if (_nvsReady) {
        nvs_erase_all(_nvsHandle);
        nvs_commit(_nvsHandle);
        Serial.println("[HEALTH] All NVS counters erased (factory reset)");
    }

    _rebootCount = 0;
    _consecutiveReboots = 0;
    _sensorErrors = 0;
    _sdErrors = 0;
    _apiErrors = 0;
    _wifiErrors = 0;
    _safeMode = false;
}

bool SystemHealth::openNVS() {
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &_nvsHandle);
    if (err != ESP_OK) {
        Serial.print("[HEALTH] NVS open failed: ");
        Serial.println(err);
        return false;
    }
    return true;
}

void SystemHealth::closeNVS() {
    if (_nvsReady) {
        nvs_close(_nvsHandle);
        _nvsReady = false;
    }
}

uint32_t SystemHealth::readNVS(const char* key, uint32_t defaultValue) {
    uint32_t value = defaultValue;
    esp_err_t err = nvs_get_u32(_nvsHandle, key, &value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        Serial.print("[HEALTH] NVS read error for ");
        Serial.print(key);
        Serial.print(": ");
        Serial.println(err);
    }
    return value;
}

void SystemHealth::writeNVS(const char* key, uint32_t value) {
    esp_err_t err = nvs_set_u32(_nvsHandle, key, value);
    if (err != ESP_OK) {
        Serial.print("[HEALTH] NVS write error for ");
        Serial.print(key);
        Serial.print(": ");
        Serial.println(err);
    }
}
