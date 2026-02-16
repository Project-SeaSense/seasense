/**
 * SeaSense Logger v2 - EZO-pH Sensor
 *
 * Atlas Scientific EZO-pH sensor
 * - Measurement range: 0.001 - 14.000 pH
 * - Accuracy: ±0.002 pH
 * - Response time: 1000ms
 * - Requires temperature compensation for accurate readings
 *
 * Calibration procedure (per Atlas Scientific datasheet):
 * 1. Mid point: Cal,mid,7.00 (required first calibration)
 * 2. Low point: Cal,low,4.00 (optional for better accuracy)
 * 3. High point: Cal,high,10.00 (optional for better accuracy)
 *
 * Temperature compensation:
 * - pH readings are temperature-dependent
 * - Use T,<temp> command before each reading
 * - Typical: T,25.0 for 25°C
 */

#ifndef EZO_PH_H
#define EZO_PH_H

#include "EZOSensor.h"
#include "../../config/hardware_config.h"

/**
 * Calibration type for EZO-pH
 */
enum class pHCalibrationType {
    MID_POINT,         // Mid point calibration (pH 7.00)
    LOW_POINT,         // Low point calibration (pH 4.00)
    HIGH_POINT,        // High point calibration (pH 10.00)
    THREE_POINT        // Full three point calibration
};

class EZO_pH : public EZOSensor {
public:
    /**
     * Constructor
     * Uses default I2C address 0x63 from hardware_config.h
     */
    EZO_pH();

    /**
     * Constructor with custom I2C address
     * @param i2cAddress Custom I2C address
     */
    explicit EZO_pH(uint8_t i2cAddress);

    virtual ~EZO_pH() {}

    /**
     * Set temperature compensation value
     * MUST be called before reading to ensure accurate pH measurement
     * @param tempC Temperature in °C
     * @return true if successful, false otherwise
     */
    bool setTemperatureCompensation(float tempC);

    /**
     * Mid point calibration (pH 7.00)
     * This is the first calibration that must be done
     * @param ph Reference pH value (default 7.00)
     * @return true if calibration successful, false otherwise
     */
    bool calibrateMidPoint(float ph = 7.00);

    /**
     * Low point calibration (pH 4.00)
     * Do this after mid point for two-point calibration
     * @param ph Reference pH value (default 4.00)
     * @return true if calibration successful, false otherwise
     */
    bool calibrateLowPoint(float ph = 4.00);

    /**
     * High point calibration (pH 10.00)
     * Do this after mid and low for three-point calibration
     * @param ph Reference pH value (default 10.00)
     * @return true if calibration successful, false otherwise
     */
    bool calibrateHighPoint(float ph = 10.00);

    /**
     * Get the current pH reading
     * @return pH value
     */
    float getpH() const { return getValue(); }

protected:
    /**
     * Assess pH reading quality
     * @return SensorQuality enum
     */
    virtual SensorQuality assessQuality() override;

private:
    // pH range constants (from datasheet)
    static constexpr float PH_MIN = 0.001;        // Minimum pH
    static constexpr float PH_MAX = 14.000;       // Maximum pH

    // Typical seawater pH range for quality assessment
    static constexpr float SEAWATER_PH_MIN = 7.5; // Slightly acidic seawater
    static constexpr float SEAWATER_PH_MAX = 8.5; // Slightly alkaline seawater

    // Temperature compensation
    float _lastTempCompensation;  // Last temperature used for compensation (°C)
    bool _tempCompensationSet;    // Has temperature compensation been set?

    /**
     * Check if pH is within valid sensor range
     * @param ph pH value
     * @return true if valid, false otherwise
     */
    bool isInValidRange(float ph) const;

    /**
     * Check if pH is within typical seawater range
     * @param ph pH value
     * @return true if typical, false if unusual
     */
    bool isTypicalSeawaterpH(float ph) const;
};

#endif // EZO_PH_H
