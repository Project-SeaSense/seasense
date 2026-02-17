/**
 * SeaSense Logger v2 - EZO-EC Conductivity Sensor Implementation
 */

#include "EZO_EC.h"
#include <math.h>

// ============================================================================
// Constructors
// ============================================================================

EZO_EC::EZO_EC()
    : EZOSensor(
        EZO_EC_ADDR,
        EZO_EC_RESPONSE_TIME_MS,
        "Conductivity",
        "EZO-EC",
        "µS/cm"
    ),
      _lastTempCompensation(25.0),
      _tempCompensationSet(false)
{
}

EZO_EC::EZO_EC(uint8_t i2cAddress)
    : EZOSensor(
        i2cAddress,
        EZO_EC_RESPONSE_TIME_MS,
        "Conductivity",
        "EZO-EC",
        "µS/cm"
    ),
      _lastTempCompensation(25.0),
      _tempCompensationSet(false)
{
}

// ============================================================================
// Temperature Compensation
// ============================================================================

bool EZO_EC::setTemperatureCompensation(float tempC) {
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

bool EZO_EC::calibrateDry() {
    DEBUG_SENSOR_PRINTLN("Starting dry calibration (zero point)");
    DEBUG_SENSOR_PRINTLN("Ensure probe is dry and clean!");

    String response;
    EZOResponseCode code = sendCommand("Cal,dry", response, 600);

    if (code != EZOResponseCode::SUCCESS) {
        DEBUG_SENSOR_PRINTLN("Dry calibration failed");
        return false;
    }

    DEBUG_SENSOR_PRINTLN("Dry calibration successful");
    return true;
}

bool EZO_EC::calibrateSinglePoint(float solutionValue) {
    DEBUG_SENSOR_PRINT("Starting single point calibration at ");
    DEBUG_SENSOR_PRINT(solutionValue);
    DEBUG_SENSOR_PRINTLN(" µS/cm");

    // Build calibration command: "Cal,one,1413"
    String command = "Cal,one," + String(solutionValue, 0);
    String response;
    EZOResponseCode code = sendCommand(command, response, 600);

    if (code != EZOResponseCode::SUCCESS) {
        DEBUG_SENSOR_PRINTLN("Single point calibration failed");
        return false;
    }

    DEBUG_SENSOR_PRINTLN("Single point calibration successful (±2% accuracy)");
    return true;
}

bool EZO_EC::calibrateLowPoint(float lowValue) {
    DEBUG_SENSOR_PRINT("Starting two-point calibration - LOW point at ");
    DEBUG_SENSOR_PRINT(lowValue);
    DEBUG_SENSOR_PRINTLN(" µS/cm");

    // Build calibration command: "Cal,low,84"
    String command = "Cal,low," + String(lowValue, 0);
    String response;
    EZOResponseCode code = sendCommand(command, response, 600);

    if (code != EZOResponseCode::SUCCESS) {
        DEBUG_SENSOR_PRINTLN("Low point calibration failed");
        return false;
    }

    DEBUG_SENSOR_PRINTLN("Low point calibration successful");
    DEBUG_SENSOR_PRINTLN("Next: calibrate HIGH point for ±1% accuracy");
    return true;
}

bool EZO_EC::calibrateHighPoint(float highValue) {
    DEBUG_SENSOR_PRINT("Starting two-point calibration - HIGH point at ");
    DEBUG_SENSOR_PRINT(highValue);
    DEBUG_SENSOR_PRINTLN(" µS/cm");

    // Build calibration command: "Cal,high,1413"
    String command = "Cal,high," + String(highValue, 0);
    String response;
    EZOResponseCode code = sendCommand(command, response, 600);

    if (code != EZOResponseCode::SUCCESS) {
        DEBUG_SENSOR_PRINTLN("High point calibration failed");
        return false;
    }

    DEBUG_SENSOR_PRINTLN("High point calibration successful (±1% accuracy)");
    return true;
}

// ============================================================================
// Salinity Calculation
// ============================================================================

float EZO_EC::calculateSalinity(float ec, float tempC) {
    // Simplified salinity calculation based on conductivity and temperature
    // This is an approximation of the PSS-78 (Practical Salinity Scale 1978)
    //
    // Reference: UNESCO Technical Papers in Marine Science No. 44 (1983)
    // "Algorithms for computation of fundamental properties of seawater"
    //
    // Note: For precise salinity, use the full PSS-78 algorithm which requires
    // conductivity ratio, temperature, and pressure. This simplified version
    // is suitable for surface water measurements.

    // Convert µS/cm to mS/cm
    float ecMS = ec / 1000.0;

    // Standard seawater at 15°C has conductivity of about 42.914 mS/cm
    // and salinity of 35 PSU
    const float STD_EC_15C = 42.914;  // mS/cm
    const float STD_SALINITY = 35.0;  // PSU

    // Temperature correction factor (simplified)
    // Conductivity increases by about 2% per °C
    float tempFactor = 1.0 + 0.02 * (tempC - 15.0);

    // Adjust conductivity to 15°C standard
    float ecAdjusted = ecMS / tempFactor;

    // Calculate salinity ratio
    float ratio = ecAdjusted / STD_EC_15C;

    // Simplified salinity calculation
    // S = a * R^0.5 + b * R^1.5 + c * R^2.5 + d * R^3.5
    // Where R is the conductivity ratio
    // These coefficients are simplified approximations
    float salinity = 0.0080 * pow(ratio, 0.5)
                   - 0.1692 * pow(ratio, 1.5)
                   + 25.3851 * pow(ratio, 2.0)
                   + 14.0941 * pow(ratio, 2.5)
                   - 7.0261 * pow(ratio, 3.0)
                   + 2.7081 * pow(ratio, 3.5);

    // Clamp to reasonable range
    if (salinity < 0.0) salinity = 0.0;
    if (salinity > 50.0) salinity = 50.0;  // Typical seawater max ~40 PSU

    return salinity;
}

float EZO_EC::getSalinity() const {
    if (!_tempCompensationSet) {
        return 0.0;  // Can't calculate without temperature
    }

    return calculateSalinity(_value, _lastTempCompensation);
}

// ============================================================================
// Quality Assessment
// ============================================================================

SensorQuality EZO_EC::assessQuality() {
    if (!_valid) {
        return SensorQuality::ERROR;
    }

    // Check if sensor is calibrated
    if (_calibrationDate.length() == 0) {
        return SensorQuality::NOT_CALIBRATED;
    }

    // Check if temperature compensation has been set
    if (!_tempCompensationSet) {
        // Conductivity measurements require temperature compensation
        return SensorQuality::FAIR;
    }

    // Check if reading is within valid sensor range
    if (!isInValidRange(_value)) {
        return SensorQuality::ERROR;
    }

    // Check if reading is within typical seawater conductivity range
    if (!isTypicalSeawaterEC(_value)) {
        // Unusual conductivity - might be valid but worth flagging
        // Could be freshwater, brackish, or hypersaline environment
        return SensorQuality::FAIR;
    }

    if (isCalibrationStale(90)) return SensorQuality::FAIR;  // >3 months

    return SensorQuality::GOOD;
}

// ============================================================================
// Private Helper Methods
// ============================================================================

bool EZO_EC::isInValidRange(float ec) const {
    return (ec >= EC_MIN && ec <= EC_MAX);
}

bool EZO_EC::isTypicalSeawaterEC(float ec) const {
    return (ec >= SEAWATER_EC_MIN && ec <= SEAWATER_EC_MAX);
}
