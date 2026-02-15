/**
 * SeaSense Logger - Pump Controller
 *
 * State machine controller for relay-controlled water circulation pump
 * Manages pump cycles: start → stabilize → measure → stop → cooldown
 */

#ifndef PUMP_CONTROLLER_H
#define PUMP_CONTROLLER_H

#include <Arduino.h>
#include "../sensors/EZO_RTD.h"
#include "../sensors/EZO_EC.h"

/**
 * Pump controller states
 */
enum class PumpState {
    IDLE,              // Waiting for next cycle
    PUMP_STARTING,     // Relay ON, initial pump startup
    STABILIZING,       // Waiting for stable readings
    MEASURING,         // Sensor reading window
    PUMP_STOPPING,     // Relay OFF, flush delay
    COOLDOWN,          // Wait before next cycle
    ERROR,             // Error state
    PAUSED             // Manual pause
};

/**
 * Stability detection method
 */
enum class StabilityMethod {
    FIXED_DELAY,        // Simple time-based (recommended)
    VARIANCE_CHECK,     // Reading consistency (future)
    HYBRID              // Fixed minimum + variance (future)
};

/**
 * Pump configuration structure
 */
struct PumpConfig {
    uint16_t pumpStartupDelayMs;      // 2000ms default
    uint16_t stabilityWaitMs;         // 3000ms default
    uint8_t measurementCount;         // 1 default
    uint16_t measurementIntervalMs;   // 2000ms default
    uint16_t pumpStopDelayMs;         // 500ms default
    uint16_t cooldownMs;              // 55000ms default
    unsigned long cycleIntervalMs;    // 60000ms (1 minute) default
    uint16_t maxPumpOnTimeMs;         // 30000ms safety
    uint8_t relayPin;                 // GPIO 25
    bool enabled;                     // true default

    // Stability detection
    StabilityMethod method;           // FIXED_DELAY (recommended)
    float tempVarianceThreshold;      // 0.1°C (for future use)
    float ecVarianceThreshold;        // 50 µS/cm (for future use)
};

class PumpController {
public:
    /**
     * Constructor
     * @param tempSensor Pointer to temperature sensor
     * @param ecSensor Pointer to conductivity sensor
     */
    PumpController(EZO_RTD* tempSensor, EZO_EC* ecSensor);

    /**
     * Initialize pump controller
     * @return true if successful
     */
    bool begin();

    /**
     * Update pump state machine
     * Call this in loop()
     */
    void update();

    /**
     * Check if sensors should be read
     * @return true if in measurement window
     */
    bool shouldReadSensors() const;

    /**
     * Notify that sensor measurement is complete
     * Call this after reading sensors
     */
    void notifyMeasurementComplete();

    /**
     * Get current pump state
     * @return PumpState
     */
    PumpState getState() const { return _state; }

    /**
     * Get current configuration
     * @return PumpConfig reference
     */
    const PumpConfig& getConfig() const { return _config; }

    /**
     * Update configuration
     * @param config New configuration
     */
    void setConfig(const PumpConfig& config);

    /**
     * Check if pump is enabled
     * @return true if enabled
     */
    bool isEnabled() const { return _config.enabled; }

    /**
     * Enable/disable pump controller
     * @param enabled true to enable
     */
    void setEnabled(bool enabled);

    /**
     * Check if relay is currently on
     * @return true if relay on
     */
    bool isRelayOn() const { return _relayOn; }

    /**
     * Get elapsed time in current cycle
     * @return Milliseconds
     */
    unsigned long getCycleElapsed() const;

    /**
     * Get cycle interval
     * @return Milliseconds
     */
    unsigned long getCycleInterval() const { return _config.cycleIntervalMs; }

    /**
     * Get cycle progress (0-100%)
     * @return Progress percentage
     */
    uint8_t getCycleProgress() const;

    /**
     * Manual pump start
     */
    void startPump();

    /**
     * Emergency stop
     */
    void stopPump();

    /**
     * Pause pump cycles
     */
    void pause();

    /**
     * Resume pump cycles
     */
    void resume();

    /**
     * Get status string
     * @return Human-readable status
     */
    String getStatusString() const;

    /**
     * Get last error message
     * @return Error string (empty if no error)
     */
    String getLastError() const { return _errorMessage; }

private:
    EZO_RTD* _tempSensor;
    EZO_EC* _ecSensor;
    PumpConfig _config;
    PumpState _state;
    bool _relayOn;

    // Timing
    unsigned long _stateStartTime;
    unsigned long _lastCycleTime;
    unsigned long _pumpStartTime;
    unsigned long _nextMeasurementTime;
    unsigned long _errorTime;

    // Measurement tracking
    uint8_t _measurementCount;
    bool _measurementTaken;

    // Error tracking
    String _errorMessage;

    /**
     * Transition to new state
     * @param newState Target state
     */
    void transitionToState(PumpState newState);

    /**
     * Check if readings are stable
     * @return true if stable
     */
    bool checkStability();

    /**
     * Handle error condition
     * @param errorMsg Error message
     */
    void handleError(const String& errorMsg);

    /**
     * Set relay output
     * @param on true to turn on
     */
    void setRelay(bool on);
};

/**
 * Convert PumpState to string
 * @param state PumpState
 * @return String representation
 */
String pumpStateToString(PumpState state);

#endif // PUMP_CONTROLLER_H
