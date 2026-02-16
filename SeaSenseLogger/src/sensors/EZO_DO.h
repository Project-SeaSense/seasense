/**
 * SeaSense Logger v2 - EZO-DO Dissolved Oxygen Sensor
 *
 * Atlas Scientific EZO-DO dissolved oxygen sensor
 * - Measurement range: 0.01 - 100+ mg/L
 * - Accuracy: ±0.05 mg/L
 * - Response time: 1000ms
 * - Requires temperature compensation for accurate readings
 * - Optional salinity and pressure compensation for seawater
 *
 * Calibration procedure (per Atlas Scientific datasheet):
 * 1. Atmospheric: Cal (probe in air)
 * 2. Zero: Cal,0 (probe in 0 DO solution)
 *
 * Compensation commands:
 * - Temperature: T,<temp> (required, e.g., T,25.0)
 * - Salinity: S,<psu> (recommended for seawater, e.g., S,35.0)
 * - Pressure: P,<kPa> (optional, e.g., P,101.3 for sea level)
 *
 */

#ifndef EZO_DO_H
#define EZO_DO_H

#include "EZOSensor.h"
#include "../../config/hardware_config.h"

/**
 * Calibration type for EZO-DO
 */
enum class DOCalibrationType {
    ATMOSPHERIC,  // Atmospheric calibration (probe in air)
    ZERO          // Zero calibration (probe in 0 DO solution)
};

class EZO_DO : public EZOSensor {
public:
    /**
     * Constructor
     * Uses default I2C address 0x61 from hardware_config.h
     */
    EZO_DO();

    /**
     * Constructor with custom I2C address
     * @param i2cAddress Custom I2C address
     */
    explicit EZO_DO(uint8_t i2cAddress);

    virtual ~EZO_DO() {}

    /**
     * Set temperature compensation value
     * MUST be called before reading to ensure accurate DO measurement
     * @param tempC Temperature in °C
     * @return true if successful, false otherwise
     */
    bool setTemperatureCompensation(float tempC);

    /**
     * Set salinity compensation value
     * Recommended for seawater measurements
     * @param salinity Salinity in PSU (practical salinity units)
     * @return true if successful, false otherwise
     */
    bool setSalinityCompensation(float salinity);

    /**
     * Set pressure compensation value
     * Optional, improves accuracy at non-sea-level depths
     * @param kPa Pressure in kilopascals (101.3 kPa = sea level)
     * @return true if successful, false otherwise
     */
    bool setPressureCompensation(float kPa);

    /**
     * Atmospheric calibration (100% air saturation)
     * Probe must be in air with membrane dry
     * Takes 1.3 seconds
     * @return true if calibration successful, false otherwise
     */
    bool calibrateAtmospheric();

    /**
     * Zero calibration (0 mg/L DO)
     * Probe must be in 0 DO solution (sodium sulfite solution)
     * Takes 1.3 seconds
     * @return true if calibration successful, false otherwise
     */
    bool calibrateZero();

    /**
     * Get the current dissolved oxygen reading
     * @return Dissolved oxygen in mg/L
     */
    float getDO() const { return getValue(); }

protected:
    /**
     * Assess DO reading quality
     * @return SensorQuality enum
     */
    virtual SensorQuality assessQuality() override;

private:
    // DO range constants (from datasheet)
    static constexpr float DO_MIN = 0.01;         // Minimum DO (mg/L)
    static constexpr float DO_MAX = 100.0;        // Maximum DO (mg/L)

    // Typical seawater DO range for quality assessment
    static constexpr float SEAWATER_DO_MIN = 4.0; // Low oxygen seawater
    static constexpr float SEAWATER_DO_MAX = 10.0; // Highly oxygenated seawater

    // Compensation tracking
    float _lastTempCompensation;   // Last temperature used for compensation (°C)
    bool _tempCompensationSet;     // Has temperature compensation been set?
    float _lastSalinityCompensation; // Last salinity used for compensation (PSU)
    bool _salinityCompensationSet; // Has salinity compensation been set?

    /**
     * Check if DO is within valid sensor range
     * @param doValue DO in mg/L
     * @return true if valid, false otherwise
     */
    bool isInValidRange(float doValue) const;

    /**
     * Check if DO is within typical seawater range
     * @param doValue DO in mg/L
     * @return true if typical, false if unusual
     */
    bool isTypicalSeawaterDO(float doValue) const;
};

#endif // EZO_DO_H
