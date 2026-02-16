/**
 * SeaSense Logger v2 - EZO-DO Dissolved Oxygen Sensor Implementation
 */

#include "EZO_DO.h"

// ============================================================================
// Constructors
// ============================================================================

EZO_DO::EZO_DO()
    : EZOSensor(
        EZO_DO_ADDR,
        EZO_DO_RESPONSE_TIME_MS,
        "Dissolved Oxygen",
        "EZO-DO",
        "mg/L"
    ),
      _lastTempCompensation(25.0),
      _tempCompensationSet(false),
      _lastSalinityCompensation(0.0),
      _salinityCompensationSet(false)
{
}

EZO_DO::EZO_DO(uint8_t i2cAddress)
    : EZOSensor(
        i2cAddress,
        EZO_DO_RESPONSE_TIME_MS,
        "Dissolved Oxygen",
        "EZO-DO",
        "mg/L"
    ),
      _lastTempCompensation(25.0),
      _tempCompensationSet(false),
      _lastSalinityCompensation(0.0),
      _salinityCompensationSet(false)
{
}

// ============================================================================
// Compensation Methods
// ============================================================================

bool EZO_DO::setTemperatureCompensation(float tempC) {
    DEBUG_SENSOR_PRINT("Setting temperature compensation: ");
    DEBUG_SENSOR_PRINT(tempC);
    DEBUG_SENSOR_PRINTLN("°C");

    // Build temperature command: "T,25.0"
    String command = "T," + String(tempC, 2);
    String response;
    EZOResponseCode code = sendCommand(command, response, 300);

    if (code != EZOResponseCode::SUCCESS) {
        DEBUG_SENSOR_PRINTLN("Failed to set temperature compensation");
        return false;
    }

    _lastTempCompensation = tempC;
    _tempCompensationSet = true;

    DEBUG_SENSOR_PRINTLN("Temperature compensation set successfully");
    return true;
}

bool EZO_DO::setSalinityCompensation(float salinity) {
    DEBUG_SENSOR_PRINT("Setting salinity compensation: ");
    DEBUG_SENSOR_PRINT(salinity);
    DEBUG_SENSOR_PRINTLN(" PSU");

    // Build salinity command: "S,35.0"
    String command = "S," + String(salinity, 2);
    String response;
    EZOResponseCode code = sendCommand(command, response, 300);

    if (code != EZOResponseCode::SUCCESS) {
        DEBUG_SENSOR_PRINTLN("Failed to set salinity compensation");
        return false;
    }

    _lastSalinityCompensation = salinity;
    _salinityCompensationSet = true;

    DEBUG_SENSOR_PRINTLN("Salinity compensation set successfully");
    return true;
}

bool EZO_DO::setPressureCompensation(float kPa) {
    DEBUG_SENSOR_PRINT("Setting pressure compensation: ");
    DEBUG_SENSOR_PRINT(kPa);
    DEBUG_SENSOR_PRINTLN(" kPa");

    // Build pressure command: "P,101.3"
    String command = "P," + String(kPa, 1);
    String response;
    EZOResponseCode code = sendCommand(command, response, 300);

    if (code != EZOResponseCode::SUCCESS) {
        DEBUG_SENSOR_PRINTLN("Failed to set pressure compensation");
        return false;
    }

    DEBUG_SENSOR_PRINTLN("Pressure compensation set successfully");
    return true;
}

// ============================================================================
// Calibration Methods
// ============================================================================

bool EZO_DO::calibrateAtmospheric() {
    DEBUG_SENSOR_PRINTLN("Starting atmospheric calibration (100% air saturation)");
    DEBUG_SENSOR_PRINTLN("Ensure probe is in air with membrane dry!");

    String response;
    EZOResponseCode code = sendCommand("Cal", response, 1300);

    if (code != EZOResponseCode::SUCCESS) {
        DEBUG_SENSOR_PRINTLN("Atmospheric calibration failed");
        return false;
    }

    DEBUG_SENSOR_PRINTLN("Atmospheric calibration successful");

    // TODO: Update calibration date in device_config.h
    return true;
}

bool EZO_DO::calibrateZero() {
    DEBUG_SENSOR_PRINTLN("Starting zero calibration (0 mg/L DO)");
    DEBUG_SENSOR_PRINTLN("Ensure probe is in 0 DO solution (sodium sulfite)!");

    String response;
    EZOResponseCode code = sendCommand("Cal,0", response, 1300);

    if (code != EZOResponseCode::SUCCESS) {
        DEBUG_SENSOR_PRINTLN("Zero calibration failed");
        return false;
    }

    DEBUG_SENSOR_PRINTLN("Zero calibration successful");

    // TODO: Update calibration date in device_config.h
    return true;
}

// ============================================================================
// Quality Assessment
// ============================================================================

SensorQuality EZO_DO::assessQuality() {
    if (!_valid) {
        return SensorQuality::ERROR;
    }

    // Check if sensor is calibrated
    if (_calibrationDate.length() == 0) {
        return SensorQuality::NOT_CALIBRATED;
    }

    // Check if temperature compensation has been set
    if (!_tempCompensationSet) {
        // DO measurements require temperature compensation
        return SensorQuality::FAIR;
    }

    // Check if reading is within valid sensor range
    if (!isInValidRange(_value)) {
        return SensorQuality::ERROR;
    }

    // Check if reading is within typical seawater DO range
    if (!isTypicalSeawaterDO(_value)) {
        // Unusual DO - might be valid but worth flagging
        // Could be stagnant water, algae bloom, or hypoxic conditions
        return SensorQuality::FAIR;
    }

    // TODO: Check calibration age and return FAIR if old (>3 months for DO)
    // DO probes need regular calibration to maintain accuracy

    return SensorQuality::GOOD;
}

// ============================================================================
// Private Helper Methods
// ============================================================================

bool EZO_DO::isInValidRange(float doValue) const {
    return (doValue >= DO_MIN && doValue <= DO_MAX);
}

bool EZO_DO::isTypicalSeawaterDO(float doValue) const {
    return (doValue >= SEAWATER_DO_MIN && doValue <= SEAWATER_DO_MAX);
}
