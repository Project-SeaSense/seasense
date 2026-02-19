/**
 * SeaSense Logger - Pump Controller Implementation
 */

#include "PumpController.h"
#include "../../config/hardware_config.h"

// ============================================================================
// Constructor
// ============================================================================

PumpController::PumpController(EZO_RTD* tempSensor, EZO_EC* ecSensor)
    : _tempSensor(tempSensor),
      _ecSensor(ecSensor),
      _state(PumpState::IDLE),
      _relayOn(false),
      _stateStartTime(0),
      _lastCycleTime(0),
      _pumpStartTime(0),
      _errorTime(0),
      _measurementTaken(false),
      _errorMessage("")
{
    // Default configuration
    _config.flushDurationMs = PUMP_FLUSH_DURATION_MS;
    _config.measureDurationMs = PUMP_MEASURE_DURATION_MS;
    _config.cycleIntervalMs = PUMP_CYCLE_INTERVAL_MS;
    _config.maxPumpOnTimeMs = PUMP_MAX_ON_TIME_MS;
    _config.relayPin = PUMP_RELAY_PIN;
    _config.enabled = true;
}

// ============================================================================
// Public Methods
// ============================================================================

bool PumpController::begin() {
    Serial.println("[PUMP] Initializing pump controller...");

    // Configure relay pin
    pinMode(_config.relayPin, OUTPUT);
    setRelay(false);  // Ensure relay is off

    Serial.print("[PUMP] Relay pin: GPIO ");
    Serial.println(_config.relayPin);
    Serial.print("[PUMP] Cycle interval: ");
    Serial.print(_config.cycleIntervalMs / 1000);
    Serial.println(" seconds");
    Serial.print("[PUMP] Flush duration: ");
    Serial.print(_config.flushDurationMs);
    Serial.println("ms");
    Serial.print("[PUMP] Measure duration: ");
    Serial.print(_config.measureDurationMs);
    Serial.println("ms");

    if (_config.enabled) {
        Serial.println("[PUMP] Pump controller enabled");
        _lastCycleTime = millis();
        _state = PumpState::IDLE;
    } else {
        Serial.println("[PUMP] Pump controller disabled");
        _state = PumpState::PAUSED;
    }

    return true;
}

void PumpController::update() {
    if (!_config.enabled || _state == PumpState::PAUSED) {
        return;
    }

    unsigned long now = millis();
    unsigned long elapsed = now - _stateStartTime;

    // Safety check: maximum pump on time (across FLUSHING + MEASURING)
    if (_state == PumpState::FLUSHING || _state == PumpState::MEASURING) {
        unsigned long pumpOnTime = now - _pumpStartTime;
        if (pumpOnTime > _config.maxPumpOnTimeMs) {
            handleError("Pump exceeded maximum on time");
            return;
        }
    }

    // State machine
    switch (_state) {
        case PumpState::IDLE:
            // Check if cycle interval elapsed
            if (now - _lastCycleTime >= _config.cycleIntervalMs) {
                transitionToState(PumpState::FLUSHING);
            }
            break;

        case PumpState::FLUSHING:
            // Wait for flush duration
            if (elapsed >= _config.flushDurationMs) {
                transitionToState(PumpState::MEASURING);
            }
            break;

        case PumpState::MEASURING:
            // Time-based transition after measure duration
            if (elapsed >= _config.measureDurationMs) {
                transitionToState(PumpState::IDLE);
                _lastCycleTime = now;
            }
            break;

        case PumpState::ERROR:
            // Turn off pump and wait for cycle interval before retry
            if (now - _errorTime >= _config.cycleIntervalMs) {
                Serial.println("[PUMP] Recovering from error, returning to IDLE");
                _errorMessage = "";
                transitionToState(PumpState::IDLE);
                _lastCycleTime = now;
            }
            break;

        case PumpState::PAUSED:
            // Manual pause - wait for resume command
            break;
    }
}

bool PumpController::shouldReadSensors() const {
    return (_state == PumpState::MEASURING && !_measurementTaken);
}

void PumpController::notifyMeasurementComplete() {
    if (_state == PumpState::MEASURING && !_measurementTaken) {
        _measurementTaken = true;
        Serial.println("[PUMP] Measurement complete");
    }
}

void PumpController::setConfig(const PumpConfig& config) {
    _config = config;
    Serial.println("[PUMP] Configuration updated");
}

void PumpController::setEnabled(bool enabled) {
    _config.enabled = enabled;

    if (enabled) {
        Serial.println("[PUMP] Pump controller enabled");
        if (_state == PumpState::PAUSED) {
            transitionToState(PumpState::IDLE);
            _lastCycleTime = millis();
        }
    } else {
        Serial.println("[PUMP] Pump controller disabled");
        setRelay(false);
        transitionToState(PumpState::PAUSED);
    }
}

unsigned long PumpController::getCycleElapsed() const {
    return millis() - _lastCycleTime;
}

uint8_t PumpController::getCycleProgress() const {
    unsigned long elapsed = getCycleElapsed();
    if (elapsed >= _config.cycleIntervalMs) {
        return 100;
    }
    return (elapsed * 100) / _config.cycleIntervalMs;
}

unsigned long PumpController::getPhaseRemainingMs() const {
    unsigned long elapsed = millis() - _stateStartTime;
    unsigned long duration = 0;
    switch (_state) {
        case PumpState::FLUSHING:  duration = _config.flushDurationMs;   break;
        case PumpState::MEASURING: duration = _config.measureDurationMs;  break;
        default: return 0;
    }
    return (elapsed < duration) ? (duration - elapsed) : 0;
}

unsigned long PumpController::getTimeUntilNextMeasurementMs() const {
    if (!_config.enabled || _state == PumpState::PAUSED || _state == PumpState::ERROR) {
        return 0;
    }
    if (_state == PumpState::IDLE) {
        unsigned long elapsed = millis() - _lastCycleTime;
        long remaining = (long)_config.cycleIntervalMs - (long)elapsed;
        unsigned long idleRemaining = (unsigned long)(remaining > 0 ? remaining : 0);
        return idleRemaining + _config.flushDurationMs;
    }
    if (_state == PumpState::FLUSHING) {
        unsigned long elapsed = millis() - _stateStartTime;
        long remaining = (long)_config.flushDurationMs - (long)elapsed;
        return (unsigned long)(remaining > 0 ? remaining : 0);
    }
    // MEASURING or other: measurement is in progress
    return 0;
}

void PumpController::startPump() {
    if (_state == PumpState::IDLE) {
        Serial.println("[PUMP] Manual pump start");
        transitionToState(PumpState::FLUSHING);
    } else {
        Serial.println("[PUMP] Cannot start - pump not in IDLE state");
    }
}

void PumpController::stopPump() {
    Serial.println("[PUMP] Emergency stop");
    setRelay(false);
    transitionToState(PumpState::IDLE);
    _lastCycleTime = millis();
}

void PumpController::pause() {
    Serial.println("[PUMP] Pausing pump controller");
    setRelay(false);
    transitionToState(PumpState::PAUSED);
}

void PumpController::resume() {
    Serial.println("[PUMP] Resuming pump controller");
    transitionToState(PumpState::IDLE);
    _lastCycleTime = millis();
}

String PumpController::getStatusString() const {
    return pumpStateToString(_state);
}

// ============================================================================
// Private Methods
// ============================================================================

void PumpController::transitionToState(PumpState newState) {
    // Update state
    _state = newState;
    _stateStartTime = millis();

    // Entry actions for new state
    switch (_state) {
        case PumpState::IDLE:
            Serial.println("[PUMP] State: IDLE");
            setRelay(false);
            break;

        case PumpState::FLUSHING:
            Serial.println("[PUMP] State: FLUSHING");
            setRelay(true);
            _pumpStartTime = millis();
            break;

        case PumpState::MEASURING:
            Serial.println("[PUMP] State: MEASURING");
            _measurementTaken = false;
            // Relay stays on
            break;

        case PumpState::ERROR:
            Serial.println("[PUMP] State: ERROR");
            setRelay(false);
            break;

        case PumpState::PAUSED:
            Serial.println("[PUMP] State: PAUSED");
            setRelay(false);
            break;
    }
}

void PumpController::handleError(const String& errorMsg) {
    setRelay(false);  // Immediate relay off
    _state = PumpState::ERROR;
    _errorMessage = errorMsg;
    _errorTime = millis();

    Serial.print("[PUMP ERROR] ");
    Serial.println(errorMsg);
}

void PumpController::setRelay(bool on) {
    if (_relayOn != on) {
        _relayOn = on;
        digitalWrite(_config.relayPin, on ? HIGH : LOW);

        Serial.print("[PUMP] Relay: ");
        Serial.println(on ? "ON" : "OFF");
    }
}

// ============================================================================
// Helper Functions
// ============================================================================

String pumpStateToString(PumpState state) {
    switch (state) {
        case PumpState::IDLE:       return "IDLE";
        case PumpState::FLUSHING:   return "FLUSHING";
        case PumpState::MEASURING:  return "MEASURING";
        case PumpState::ERROR:      return "ERROR";
        case PumpState::PAUSED:     return "PAUSED";
        default:                    return "UNKNOWN";
    }
}
