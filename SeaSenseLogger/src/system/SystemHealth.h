/**
 * SeaSense Logger - System Health Monitor
 *
 * Manages device resilience for unattended deployment:
 * - Hardware watchdog (ESP32 task WDT)
 * - Boot loop detection with safe mode
 * - Persistent error counters (NVS)
 * - Reboot reason tracking
 */

#ifndef SEASENSE_SYSTEM_HEALTH_H
#define SEASENSE_SYSTEM_HEALTH_H

#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_task_wdt.h>
#include <esp_system.h>

enum class ErrorType : uint8_t {
    SENSOR = 0,
    SD = 1,
    API = 2,
    WIFI = 3
};

class SystemHealth {
public:
    SystemHealth();

    /**
     * Initialize NVS, read counters, detect boot loops, start watchdog.
     * Call early in setup(), before other subsystems.
     * @param wdtTimeoutMs Watchdog timeout in milliseconds
     * @param bootLoopThreshold Consecutive reboots before entering safe mode
     * @param bootLoopWindowMs Time window: if uptime exceeds this, reset consecutive counter
     * @return true if initialization succeeded
     */
    bool begin(uint32_t wdtTimeoutMs, uint8_t bootLoopThreshold, uint32_t bootLoopWindowMs);

    /**
     * Feed the watchdog timer. Call at the top of every loop() iteration.
     * Also checks if uptime exceeds boot loop window to clear consecutive counter.
     */
    void feedWatchdog();

    /**
     * Record an error for a subsystem. Persisted to NVS.
     */
    void recordError(ErrorType type);

    /**
     * Whether the device entered safe mode due to boot loops.
     */
    bool isInSafeMode() const { return _safeMode; }

    /**
     * Total reboot count (lifetime).
     */
    uint32_t getRebootCount() const { return _rebootCount; }

    /**
     * Consecutive reboot count (resets after stable operation).
     */
    uint32_t getConsecutiveReboots() const { return _consecutiveReboots; }

    /**
     * Last reset reason from ESP32.
     */
    esp_reset_reason_t getLastResetReason() const { return _resetReason; }

    /**
     * Get error count for a specific subsystem.
     */
    uint32_t getErrorCount(ErrorType type) const;

    /**
     * Get reset reason as a human-readable string.
     */
    String getResetReasonString() const;

    /**
     * Clear safe mode: zeroes consecutive reboot counter in NVS.
     * Call before ESP.restart() to exit safe mode on next boot.
     */
    void clearSafeMode();

    /**
     * Factory reset: erase all NVS counters and zero in-memory state.
     */
    void resetAllCounters();

private:
    bool _safeMode;
    bool _nvsReady;
    bool _consecutiveRebootCleared;

    uint32_t _rebootCount;
    uint32_t _consecutiveReboots;
    uint32_t _bootLoopWindowMs;
    esp_reset_reason_t _resetReason;

    // Persistent error counters
    uint32_t _sensorErrors;
    uint32_t _sdErrors;
    uint32_t _apiErrors;
    uint32_t _wifiErrors;

    nvs_handle_t _nvsHandle;

    bool openNVS();
    void closeNVS();
    uint32_t readNVS(const char* key, uint32_t defaultValue = 0);
    void writeNVS(const char* key, uint32_t value);
};

#endif // SEASENSE_SYSTEM_HEALTH_H
