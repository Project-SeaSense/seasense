/**
 * SeaSense Logger - Calibration Manager Implementation
 */

#include "CalibrationManager.h"

// Implemented in SeaSenseLogger.ino
extern bool updateSensorCalibration(const String& sensorType,
                                    const String& calibrationType,
                                    float calibrationValue,
                                    const String& note);

// ============================================================================
// Constructor
// ============================================================================

CalibrationManager::CalibrationManager(EZO_RTD* tempSensor, EZO_EC* ecSensor)
    : _tempSensor(tempSensor),
      _ecSensor(ecSensor),
      _stabilityIndex(0),
      _lastReadingTime(0)
{
    resetState();
}

// ============================================================================
// Public Methods
// ============================================================================

bool CalibrationManager::startCalibration(
    const String& sensorType,
    CalibrationType calibrationType,
    float referenceValue
) {
    // Check if already calibrating
    if (isCalibrating()) {
        return false;
    }

    // Reset state
    resetState();

    // Set calibration parameters
    _state.sensorType = sensorType;
    _state.type = calibrationType;
    _state.referenceValue = referenceValue;
    _state.startTime = millis();
    _state.status = CalibrationStatus::PREPARING;

    // Set initial message
    if (calibrationType == CalibrationType::EC_DRY) {
        _state.message = "Remove probe from liquid and ensure it is dry";
    } else if (calibrationType == CalibrationType::TEMPERATURE_SINGLE) {
        _state.message = "Place probe in reference temperature environment";
    } else {
        _state.message = "Place probe in calibration solution (" +
                        String(referenceValue, 0) + " µS/cm)";
    }

    Serial.print("[CALIBRATION] Starting ");
    Serial.print(sensorType);
    Serial.print(" calibration, type=");
    Serial.println((int)calibrationType);

    return true;
}

void CalibrationManager::update() {
    if (!isCalibrating()) {
        return;
    }

    unsigned long now = millis();

    // Read current sensor value
    float currentValue = 0.0;
    bool readSuccess = false;

    if (_state.sensorType == "temperature" && _tempSensor) {
        if (_tempSensor->read()) {
            currentValue = _tempSensor->getValue();
            readSuccess = true;
        }
    } else if (_state.sensorType == "conductivity" && _ecSensor) {
        if (_ecSensor->read()) {
            currentValue = _ecSensor->getValue();
            readSuccess = true;
        }
    }

    if (!readSuccess) {
        _state.status = CalibrationStatus::ERROR;
        _state.message = "Failed to read sensor";
        _state.success = false;
        return;
    }

    _state.currentReading = currentValue;

    // State machine
    switch (_state.status) {
        case CalibrationStatus::PREPARING:
            // Wait a few seconds before checking stability
            if (now - _state.startTime > 3000) {
                _state.status = CalibrationStatus::WAITING_STABLE;
                _state.message = "Waiting for reading to stabilize...";
                Serial.println("[CALIBRATION] Waiting for stable reading");
            }
            break;

        case CalibrationStatus::WAITING_STABLE:
            // Check if reading is stable
            if (isReadingStable(currentValue)) {
                _state.status = CalibrationStatus::CALIBRATING;
                _state.message = "Performing calibration...";
                _state.stableTime = now;
                Serial.println("[CALIBRATION] Reading stable, performing calibration");

                // Perform calibration
                if (performCalibration()) {
                    _state.status = CalibrationStatus::COMPLETE;
                    _state.message = "Calibration successful!";
                    _state.success = true;
                    Serial.println("[CALIBRATION] Success!");
                } else {
                    _state.status = CalibrationStatus::ERROR;
                    _state.message = "Calibration command failed";
                    _state.success = false;
                    Serial.println("[CALIBRATION] Failed!");
                }
            } else {
                // Update stability message with current reading
                _state.message = "Waiting for stable reading... (" +
                               String(currentValue, 2) + ")";
            }
            break;

        case CalibrationStatus::CALIBRATING:
            // Calibration command already sent, waiting for completion
            // This state is brief, should move to COMPLETE or ERROR quickly
            break;

        case CalibrationStatus::COMPLETE:
        case CalibrationStatus::ERROR:
        case CalibrationStatus::IDLE:
            // Terminal states, do nothing
            break;
    }
}

void CalibrationManager::cancel() {
    if (isCalibrating()) {
        Serial.println("[CALIBRATION] Cancelled");
        _state.status = CalibrationStatus::ERROR;
        _state.message = "Calibration cancelled by user";
        _state.success = false;
    }
}

// ============================================================================
// Private Methods
// ============================================================================

bool CalibrationManager::isReadingStable(float currentValue) {
    unsigned long now = millis();

    // Only check stability every 500ms
    if (now - _lastReadingTime < 500) {
        return false;
    }
    _lastReadingTime = now;

    // Add to stability buffer
    _stabilityBuffer[_stabilityIndex] = currentValue;
    _stabilityIndex = (_stabilityIndex + 1) % STABILITY_SAMPLES;

    // Need full buffer before checking stability
    if (_stabilityIndex != 0 && _state.status == CalibrationStatus::WAITING_STABLE &&
        (now - _state.startTime) < (STABILITY_SAMPLES * 500 + 3000)) {
        return false;
    }

    // Calculate variance
    float mean = 0;
    for (int i = 0; i < STABILITY_SAMPLES; i++) {
        mean += _stabilityBuffer[i];
    }
    mean /= STABILITY_SAMPLES;

    float variance = 0;
    for (int i = 0; i < STABILITY_SAMPLES; i++) {
        float diff = _stabilityBuffer[i] - mean;
        variance += diff * diff;
    }
    variance /= STABILITY_SAMPLES;

    // Stability threshold (adjust based on sensor type)
    float threshold = 0.1;  // Default for temperature
    if (_state.sensorType == "conductivity") {
        threshold = 50.0;  // Higher threshold for conductivity
    }

    bool stable = (variance < threshold);

    if (stable) {
        Serial.print("[CALIBRATION] Reading stable: ");
        Serial.print(mean, 2);
        Serial.print(" (variance: ");
        Serial.print(variance, 4);
        Serial.println(")");
    }

    return stable;
}

bool CalibrationManager::performCalibration() {
    bool success = false;

    switch (_state.type) {
        case CalibrationType::TEMPERATURE_SINGLE:
            if (_tempSensor) {
                success = _tempSensor->calibrate(_state.referenceValue);
            }
            break;

        case CalibrationType::EC_DRY:
            if (_ecSensor) {
                success = _ecSensor->calibrateDry();
            }
            break;

        case CalibrationType::EC_SINGLE:
            if (_ecSensor) {
                success = _ecSensor->calibrateSinglePoint(_state.referenceValue);
            }
            break;

        case CalibrationType::EC_TWO_LOW:
            if (_ecSensor) {
                success = _ecSensor->calibrateLowPoint(_state.referenceValue);
            }
            break;

        case CalibrationType::EC_TWO_HIGH:
            if (_ecSensor) {
                success = _ecSensor->calibrateHighPoint(_state.referenceValue);
            }
            break;

        default:
            success = false;
            break;
    }

    if (success) {
        // Map CalibrationType to a human-readable string for the log
        String calTypeStr;
        switch (_state.type) {
            case CalibrationType::TEMPERATURE_SINGLE: calTypeStr = "single";    break;
            case CalibrationType::EC_DRY:             calTypeStr = "dry";       break;
            case CalibrationType::EC_SINGLE:          calTypeStr = "single";    break;
            case CalibrationType::EC_TWO_LOW:         calTypeStr = "two-low";   break;
            case CalibrationType::EC_TWO_HIGH:        calTypeStr = "two-high";  break;
            default:                                  calTypeStr = "unknown";   break;
        }
        // Sensor type strings must match what getSensorMetadata() expects
        String sensorType = (_state.sensorType == "temperature") ? "Temperature" : "Conductivity";
        updateSensorCalibration(sensorType, calTypeStr, _state.referenceValue, "");
    }

    return success;
}

void CalibrationManager::resetState() {
    _state.status = CalibrationStatus::IDLE;
    _state.type = CalibrationType::NONE;
    _state.sensorType = "";
    _state.referenceValue = 0.0;
    _state.currentReading = 0.0;
    _state.startTime = 0;
    _state.stableTime = 0;
    _state.message = "";
    _state.success = false;

    _stabilityIndex = 0;
    _lastReadingTime = 0;
    for (int i = 0; i < STABILITY_SAMPLES; i++) {
        _stabilityBuffer[i] = 0.0;
    }
}
