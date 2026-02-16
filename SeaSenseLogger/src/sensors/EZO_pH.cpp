/**
 * SeaSense Logger v2 - EZO-pH Sensor Implementation
 */

#include "EZO_pH.h"

// ============================================================================
// Constructors
// ============================================================================

EZO_pH::EZO_pH()
    : EZOSensor(
        EZO_PH_ADDR,
        EZO_PH_RESPONSE_TIME_MS,
        "pH",
        "EZO-pH",
        "pH"
    ),
      _lastTempCompensation(25.0),
      _tempCompensationSet(false)
{
}

EZO_pH::EZO_pH(uint8_t i2cAddress)
    : EZOSensor(
        i2cAddress,
        EZO_PH_RESPONSE_TIME_MS,
        "pH",
        "EZO-pH",
        "pH"
    ),
      _lastTempCompensation(25.0),
      _tempCompensationSet(false)
{
}

// ============================================================================
// Temperature Compensation
// ============================================================================

bool EZO_pH::setTemperatureCompensation(float tempC) {
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

// ============================================================================
// Calibration Methods
// ============================================================================

bool EZO_pH::calibrateMidPoint(float ph) {
    DEBUG_SENSOR_PRINT("Starting mid point calibration at pH ");
    DEBUG_SENSOR_PRINTLN(ph);
    DEBUG_SENSOR_PRINTLN("Ensure probe is in pH 7.00 calibration solution!");

    // Build calibration command: "Cal,mid,7.00"
    String command = "Cal,mid," + String(ph, 2);
    String response;
    EZOResponseCode code = sendCommand(command, response, 900);

    if (code != EZOResponseCode::SUCCESS) {
        DEBUG_SENSOR_PRINTLN("Mid point calibration failed");
        return false;
    }

    DEBUG_SENSOR_PRINTLN("Mid point calibration successful");

    // TODO: Update calibration date in device_config.h
    return true;
}

bool EZO_pH::calibrateLowPoint(float ph) {
    DEBUG_SENSOR_PRINT("Starting low point calibration at pH ");
    DEBUG_SENSOR_PRINTLN(ph);
    DEBUG_SENSOR_PRINTLN("Ensure probe is in pH 4.00 calibration solution!");

    // Build calibration command: "Cal,low,4.00"
    String command = "Cal,low," + String(ph, 2);
    String response;
    EZOResponseCode code = sendCommand(command, response, 900);

    if (code != EZOResponseCode::SUCCESS) {
        DEBUG_SENSOR_PRINTLN("Low point calibration failed");
        return false;
    }

    DEBUG_SENSOR_PRINTLN("Low point calibration successful");
    DEBUG_SENSOR_PRINTLN("Two-point calibration complete");

    // TODO: Update calibration date in device_config.h
    return true;
}

bool EZO_pH::calibrateHighPoint(float ph) {
    DEBUG_SENSOR_PRINT("Starting high point calibration at pH ");
    DEBUG_SENSOR_PRINTLN(ph);
    DEBUG_SENSOR_PRINTLN("Ensure probe is in pH 10.00 calibration solution!");

    // Build calibration command: "Cal,high,10.00"
    String command = "Cal,high," + String(ph, 2);
    String response;
    EZOResponseCode code = sendCommand(command, response, 900);

    if (code != EZOResponseCode::SUCCESS) {
        DEBUG_SENSOR_PRINTLN("High point calibration failed");
        return false;
    }

    DEBUG_SENSOR_PRINTLN("High point calibration successful");
    DEBUG_SENSOR_PRINTLN("Three-point calibration complete (best accuracy)");

    // TODO: Update calibration date in device_config.h
    return true;
}

// ============================================================================
// Quality Assessment
// ============================================================================

SensorQuality EZO_pH::assessQuality() {
    if (!_valid) {
        return SensorQuality::ERROR;
    }

    // Check if sensor is calibrated
    if (_calibrationDate.length() == 0) {
        return SensorQuality::NOT_CALIBRATED;
    }

    // Check if temperature compensation has been set
    if (!_tempCompensationSet) {
        // pH measurements require temperature compensation
        return SensorQuality::FAIR;
    }

    // Check if reading is within valid sensor range
    if (!isInValidRange(_value)) {
        return SensorQuality::ERROR;
    }

    // Check if reading is within typical seawater pH range
    if (!isTypicalSeawaterpH(_value)) {
        // Unusual pH - might be valid but worth flagging
        // Could be freshwater, polluted, or unusual conditions
        return SensorQuality::FAIR;
    }

    // TODO: Check calibration age and return FAIR if old (>6 months for pH)
    // pH probes need regular calibration to maintain accuracy

    return SensorQuality::GOOD;
}

// ============================================================================
// Private Helper Methods
// ============================================================================

bool EZO_pH::isInValidRange(float ph) const {
    return (ph >= PH_MIN && ph <= PH_MAX);
}

bool EZO_pH::isTypicalSeawaterpH(float ph) const {
    return (ph >= SEAWATER_PH_MIN && ph <= SEAWATER_PH_MAX);
}
