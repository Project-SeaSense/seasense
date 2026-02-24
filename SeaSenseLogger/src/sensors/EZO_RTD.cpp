/**
 * SeaSense Logger v2 - EZO-RTD Temperature Sensor Implementation
 */

#include "EZO_RTD.h"

// ============================================================================
// Constructors
// ============================================================================

EZO_RTD::EZO_RTD()
    : EZOSensor(
        EZO_RTD_ADDR,
        EZO_RTD_RESPONSE_TIME_MS,
        "Temperature",
        "EZO-RTD",
        "°C"
    )
{
}

EZO_RTD::EZO_RTD(uint8_t i2cAddress)
    : EZOSensor(
        i2cAddress,
        EZO_RTD_RESPONSE_TIME_MS,
        "Temperature",
        "EZO-RTD",
        "°C"
    )
{
}

// ============================================================================
// Calibration
// ============================================================================

bool EZO_RTD::calibrate(float referenceTemp) {
    DEBUG_SENSOR_PRINT("Calibrating EZO-RTD to reference temperature: ");
    DEBUG_SENSOR_PRINT(referenceTemp);
    DEBUG_SENSOR_PRINTLN("°C");

    // Build calibration command: "Cal,t"
    // Note: EZO-RTD calibration requires the probe to be at a known temperature
    // The sensor will use the current reading and adjust to match the reference
    String command = "Cal," + String(referenceTemp, 2);
    String response;
    EZOResponseCode code = sendCommand(command, response, 900);

    if (code != EZOResponseCode::SUCCESS) {
        DEBUG_SENSOR_PRINTLN("Calibration failed");
        return false;
    }

    DEBUG_SENSOR_PRINTLN("Calibration successful");

    return true;
}

// ============================================================================
// Quality Assessment
// ============================================================================

SensorQuality EZO_RTD::assessQuality() {
    if (!_valid) {
        return SensorQuality::ERROR;
    }

    // Check if sensor is calibrated
    if (_calibrationDate.length() == 0) {
        return SensorQuality::NOT_CALIBRATED;
    }

    // Check if reading is within valid sensor range
    if (!isInValidRange(_value)) {
        return SensorQuality::ERROR;
    }

    // Check if reading is within typical ocean temperature range
    if (!isTypicalOceanTemp(_value)) {
        // Unusual temperature - might be valid but worth flagging
        return SensorQuality::FAIR;
    }

    if (isCalibrationStale(365)) return SensorQuality::FAIR;  // >1 year

    return SensorQuality::GOOD;
}

// ============================================================================
// Private Helper Methods
// ============================================================================

bool EZO_RTD::isInValidRange(float temp) const {
    return (temp >= TEMP_MIN && temp <= TEMP_MAX);
}

bool EZO_RTD::isTypicalOceanTemp(float temp) const {
    return (temp >= OCEAN_TEMP_MIN && temp <= OCEAN_TEMP_MAX);
}
