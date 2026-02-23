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

CalibrationManager::CalibrationManager(EZO_RTD* tempSensor, EZO_EC* ecSensor, EZO_pH* phSensor, EZO_DO* doSensor)
    : _tempSensor(tempSensor),
      _ecSensor(ecSensor),
      _phSensor(phSensor),
      _doSensor(doSensor),
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
    } else if (calibrationType == CalibrationType::PH_MID) {
        _state.message = "Place probe in pH " + String(referenceValue, 2) + " buffer solution";
    } else if (calibrationType == CalibrationType::PH_LOW) {
        _state.message = "Place probe in pH " + String(referenceValue, 2) + " buffer solution";
    } else if (calibrationType == CalibrationType::PH_HIGH) {
        _state.message = "Place probe in pH " + String(referenceValue, 2) + " buffer solution";
    } else if (calibrationType == CalibrationType::DO_ATMOSPHERIC) {
        _state.message = "Hold probe in air, ensure membrane is dry";
    } else if (calibrationType == CalibrationType::DO_ZERO) {
        _state.message = "Place probe in 0 mg/L sodium sulfite solution";
    } else {
        _state.message = "Place probe in calibration solution (" +
                        String(referenceValue, 0) + " \xC2\xB5S/cm)";
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
    } else if (_state.sensorType == "ph" && _phSensor) {
        if (_phSensor->read()) {
            currentValue = _phSensor->getValue();
            readSuccess = true;
        }
    } else if (_state.sensorType == "dissolved_oxygen" && _doSensor) {
        if (_doSensor->read()) {
            currentValue = _doSensor->getValue();
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

        case CalibrationStatus::WAITING_STABLE: {
            // Timeout after 60 seconds of waiting for stability
            if (now - _state.startTime > 60000) {
                _state.status = CalibrationStatus::ERROR;
                _state.message = "Timed out waiting for stable reading (CV=" +
                               String(_lastCV, 2) + "%). Try reducing agitation.";
                _state.success = false;
                Serial.print("[CALIBRATION] Timeout — CV: ");
                Serial.print(_lastCV, 2);
                Serial.println("%");
                break;
            }
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
                    _state.message = "Calibration command failed — sensor rejected the command";
                    _state.success = false;
                    Serial.println("[CALIBRATION] Failed!");
                }
            } else {
                // Show CV progress so user can see how close to stability
                _state.message = "Stabilizing... CV=" + String(_lastCV, 2) +
                               "% (need <" + String(_lastCVTarget, 1) +
                               "%) — " + String(currentValue, 1);
            }
            break;
        }

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

    // Only sample every 500ms
    if (now - _lastReadingTime < 500) {
        return false;
    }
    _lastReadingTime = now;

    // Add to circular stability buffer
    _stabilityBuffer[_stabilityIndex] = currentValue;
    _stabilityIndex = (_stabilityIndex + 1) % STABILITY_SAMPLES;

    // Minimum time in WAITING_STABLE: need full buffer + at least 5s total
    unsigned long minStableMs = STABILITY_SAMPLES * 500 + 3000;
    if (now - _state.startTime < minStableMs) {
        return false;
    }

    // Calculate mean and coefficient of variation (CV)
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

    // Use percentage-based threshold (coefficient of variation)
    // CV = sqrt(variance) / |mean|, but we compare variance against (mean * pct)^2
    float pct = 0.002f;  // default: 0.2% for temperature
    if (_state.sensorType == "conductivity") {
        pct = 0.005f;    // 0.5% — e.g. ±750 µS at 150000 (stirred high-EC)
    } else if (_state.sensorType == "ph") {
        pct = 0.005f;    // 0.5% — e.g. ±0.035 at pH 7
    } else if (_state.sensorType == "dissolved_oxygen") {
        pct = 0.005f;    // 0.5% — e.g. ±0.04 at 8 mg/L
    }

    // Threshold = (mean * pct)^2, with a small floor to avoid div-by-zero
    float absMean = fabs(mean);
    float limit = absMean * pct;
    float threshold = (limit > 0.001f) ? limit * limit : 0.001f;

    // Track CV for status messages and timeout reporting
    _lastCV = (absMean > 0.001f) ? sqrt(variance) / absMean * 100.0f : 0.0f;
    _lastCVTarget = pct * 100.0f;

    bool stable = (variance < threshold);

    if (stable) {
        Serial.print("[CALIBRATION] Reading stable: ");
        Serial.print(mean, 2);
        Serial.print(" (CV: ");
        Serial.print(_lastCV, 3);
        Serial.println("%)");
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

        case CalibrationType::PH_MID:
            if (_phSensor) {
                success = _phSensor->calibrateMidPoint(_state.referenceValue);
            }
            break;

        case CalibrationType::PH_LOW:
            if (_phSensor) {
                success = _phSensor->calibrateLowPoint(_state.referenceValue);
            }
            break;

        case CalibrationType::PH_HIGH:
            if (_phSensor) {
                success = _phSensor->calibrateHighPoint(_state.referenceValue);
            }
            break;

        case CalibrationType::DO_ATMOSPHERIC:
            if (_doSensor) {
                success = _doSensor->calibrateAtmospheric();
            }
            break;

        case CalibrationType::DO_ZERO:
            if (_doSensor) {
                success = _doSensor->calibrateZero();
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
            case CalibrationType::TEMPERATURE_SINGLE: calTypeStr = "single";       break;
            case CalibrationType::EC_DRY:             calTypeStr = "dry";          break;
            case CalibrationType::EC_SINGLE:          calTypeStr = "single";       break;
            case CalibrationType::EC_TWO_LOW:         calTypeStr = "two-low";      break;
            case CalibrationType::EC_TWO_HIGH:        calTypeStr = "two-high";     break;
            case CalibrationType::PH_MID:             calTypeStr = "mid";          break;
            case CalibrationType::PH_LOW:             calTypeStr = "low";          break;
            case CalibrationType::PH_HIGH:            calTypeStr = "high";         break;
            case CalibrationType::DO_ATMOSPHERIC:     calTypeStr = "atmospheric";  break;
            case CalibrationType::DO_ZERO:            calTypeStr = "zero";         break;
            default:                                  calTypeStr = "unknown";      break;
        }
        // Sensor type strings must match what getSensorMetadata() expects
        String sensorType;
        if (_state.sensorType == "temperature") sensorType = "Temperature";
        else if (_state.sensorType == "conductivity") sensorType = "Conductivity";
        else if (_state.sensorType == "ph") sensorType = "pH";
        else if (_state.sensorType == "dissolved_oxygen") sensorType = "Dissolved Oxygen";
        else sensorType = _state.sensorType;
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
    _lastCV = 0.0;
    _lastCVTarget = 0.0;
    for (int i = 0; i < STABILITY_SAMPLES; i++) {
        _stabilityBuffer[i] = 0.0;
    }
}
