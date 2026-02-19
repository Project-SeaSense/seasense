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
      _nextMeasurementTime(0),
      _errorTime(0),
      _measurementCount(0),
      _measurementTaken(false),
      _errorMessage("")
{
    // Default configuration
    _config.pumpStartupDelayMs = PUMP_STARTUP_DELAY_MS;
    _config.stabilityWaitMs = PUMP_STABILITY_WAIT_MS;
    _config.measurementCount = PUMP_MEASUREMENT_COUNT;
    _config.measurementIntervalMs = PUMP_MEASUREMENT_INTERVAL_MS;
    _config.pumpStopDelayMs = PUMP_STOP_DELAY_MS;
    _config.cooldownMs = PUMP_COOLDOWN_MS;
    _config.cycleIntervalMs = PUMP_CYCLE_INTERVAL_MS;
    _config.maxPumpOnTimeMs = PUMP_MAX_ON_TIME_MS;
    _config.relayPin = PUMP_RELAY_PIN;
    _config.enabled = true;
    _config.method = StabilityMethod::FIXED_DELAY;
    _config.tempVarianceThreshold = 0.1;
    _config.ecVarianceThreshold = 50.0;
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
    Serial.print("[PUMP] Stability method: ");
    Serial.println(_config.method == StabilityMethod::FIXED_DELAY ? "FIXED_DELAY" : "VARIANCE");

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

    // Safety check: maximum pump on time
    if (_state == PumpState::PUMP_STARTING || _state == PumpState::STABILIZING || _state == PumpState::MEASURING) {
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
                transitionToState(PumpState::PUMP_STARTING);
            }
            break;

        case PumpState::PUMP_STARTING:
            // Wait for startup delay
            if (elapsed >= _config.pumpStartupDelayMs) {
                transitionToState(PumpState::STABILIZING);
            }
            break;

        case PumpState::STABILIZING:
            // Check stability
            if (checkStability()) {
                transitionToState(PumpState::MEASURING);
            }
            break;

        case PumpState::MEASURING:
            // Check if measurement was taken
            if (_measurementTaken) {
                // Check if more measurements needed
                if (_measurementCount >= _config.measurementCount) {
                    transitionToState(PumpState::PUMP_STOPPING);
                } else {
                    // Wait for next measurement
                    if (now >= _nextMeasurementTime) {
                        _measurementTaken = false;  // Ready for next measurement
                    }
                }
            }
            break;

        case PumpState::PUMP_STOPPING:
            // Wait for flush delay
            if (elapsed >= _config.pumpStopDelayMs) {
                transitionToState(PumpState::COOLDOWN);
            }
            break;

        case PumpState::COOLDOWN:
            // Wait for cooldown period
            if (elapsed >= _config.cooldownMs) {
                transitionToState(PumpState::IDLE);
                _lastCycleTime = now;
            }
            break;

        case PumpState::ERROR:
            // Turn off pump and wait for cooldown before retry
            if (now - _errorTime >= _config.cooldownMs) {
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
        _measurementCount++;

        // Schedule next measurement if needed
        if (_measurementCount < _config.measurementCount) {
            _nextMeasurementTime = millis() + _config.measurementIntervalMs;
        }

        Serial.print("[PUMP] Measurement ");
        Serial.print(_measurementCount);
        Serial.print(" of ");
        Serial.print(_config.measurementCount);
        Serial.println(" complete");
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
        case PumpState::PUMP_STARTING: duration = _config.pumpStartupDelayMs; break;
        case PumpState::STABILIZING:   duration = _config.stabilityWaitMs;    break;
        case PumpState::PUMP_STOPPING: duration = _config.pumpStopDelayMs;    break;
        case PumpState::COOLDOWN:      duration = _config.cooldownMs;         break;
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
        return (unsigned long)(remaining > 0 ? remaining : 0);
    }
    // In any active pump phase, measurement is imminent or in progress
    return 0;
}

void PumpController::startPump() {
    if (_state == PumpState::IDLE) {
        Serial.println("[PUMP] Manual pump start");
        transitionToState(PumpState::PUMP_STARTING);
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
    // Exit actions for current state
    switch (_state) {
        case PumpState::PUMP_STARTING:
        case PumpState::STABILIZING:
        case PumpState::MEASURING:
            // Pump states - relay handled by new state
            break;

        case PumpState::PUMP_STOPPING:
            setRelay(false);
            break;

        default:
            break;
    }

    // Update state
    PumpState oldState = _state;
    _state = newState;
    _stateStartTime = millis();

    // Entry actions for new state
    switch (_state) {
        case PumpState::IDLE:
            Serial.println("[PUMP] State: IDLE");
            setRelay(false);
            break;

        case PumpState::PUMP_STARTING:
            Serial.println("[PUMP] State: PUMP_STARTING");
            setRelay(true);
            _pumpStartTime = millis();
            _measurementCount = 0;
            break;

        case PumpState::STABILIZING:
            Serial.println("[PUMP] State: STABILIZING");
            // Relay already on
            break;

        case PumpState::MEASURING:
            Serial.println("[PUMP] State: MEASURING");
            _measurementTaken = false;
            _nextMeasurementTime = 0;
            break;

        case PumpState::PUMP_STOPPING:
            Serial.println("[PUMP] State: PUMP_STOPPING");
            setRelay(false);
            break;

        case PumpState::COOLDOWN:
            Serial.println("[PUMP] State: COOLDOWN");
            // Relay already off
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

bool PumpController::checkStability() {
    unsigned long elapsed = millis() - _stateStartTime;

    switch (_config.method) {
        case StabilityMethod::FIXED_DELAY:
            return (elapsed >= _config.stabilityWaitMs);

        case StabilityMethod::VARIANCE_CHECK:
            // TODO: Implement variance-based stability detection
            // For now, fall back to fixed delay
            return (elapsed >= _config.stabilityWaitMs);

        case StabilityMethod::HYBRID:
            // TODO: Implement hybrid stability detection
            // For now, fall back to fixed delay
            return (elapsed >= _config.stabilityWaitMs);

        default:
            return (elapsed >= _config.stabilityWaitMs);
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
        case PumpState::IDLE:           return "IDLE";
        case PumpState::PUMP_STARTING:  return "PUMP_STARTING";
        case PumpState::STABILIZING:    return "STABILIZING";
        case PumpState::MEASURING:      return "MEASURING";
        case PumpState::PUMP_STOPPING:  return "PUMP_STOPPING";
        case PumpState::COOLDOWN:       return "COOLDOWN";
        case PumpState::ERROR:          return "ERROR";
        case PumpState::PAUSED:         return "PAUSED";
        default:                        return "UNKNOWN";
    }
}
