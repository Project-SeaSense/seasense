/**
 * SeaSense Logger v2 - EZO-EC Conductivity Sensor
 *
 * Atlas Scientific EZO-EC conductivity sensor
 * - Measurement range: 0.07 µS/cm to 500,000 µS/cm
 * - Accuracy: ±2% (single point cal), ±1% (two point cal)
 * - Response time: 600ms
 * - Requires temperature compensation for accurate readings
 *
 * Calibration procedure (per Atlas Scientific datasheet):
 * 1. Dry calibration (zero point): Cal,dry
 * 2. Single point: Cal,one,<value> (e.g., Cal,one,1413 for 1413 µS/cm solution)
 * 3. Two point low: Cal,low,<value> (e.g., Cal,low,84)
 * 4. Two point high: Cal,high,<value> (e.g., Cal,high,1413)
 *
 * Output modes (set with O,parameter,<0|1>):
 * - EC (conductivity in µS/cm)
 * - TDS (total dissolved solids in ppm)
 * - S (salinity in PSU or ppt)
 * - SG (specific gravity)
 */

#ifndef EZO_EC_H
#define EZO_EC_H

#include "EZOSensor.h"
#include "../../config/hardware_config.h"

/**
 * Calibration type for EZO-EC
 */
enum class ECCalibrationType {
    DRY,         // Dry calibration (zero point)
    SINGLE,      // Single point calibration
    TWO_POINT    // Two point calibration (low + high)
};

class EZO_EC : public EZOSensor {
public:
    /**
     * Constructor
     * Uses default I2C address 0x64 from hardware_config.h
     */
    EZO_EC();

    /**
     * Constructor with custom I2C address
     * @param i2cAddress Custom I2C address
     */
    explicit EZO_EC(uint8_t i2cAddress);

    virtual ~EZO_EC() {}

    /**
     * Set temperature compensation value
     * MUST be called before reading to ensure accurate conductivity measurement
     * @param tempC Temperature in °C
     * @return true if successful, false otherwise
     */
    bool setTemperatureCompensation(float tempC);

    /**
     * Dry calibration (zero point)
     * Sensor must be dry and clean
     * @return true if calibration successful, false otherwise
     */
    bool calibrateDry();

    /**
     * Single point calibration
     * @param solutionValue Known conductivity value in µS/cm (e.g., 1413)
     * @return true if calibration successful, false otherwise
     */
    bool calibrateSinglePoint(float solutionValue);

    /**
     * Two point calibration - low point
     * @param lowValue Known low conductivity value in µS/cm (e.g., 84 or 12880)
     * @return true if calibration successful, false otherwise
     */
    bool calibrateLowPoint(float lowValue);

    /**
     * Two point calibration - high point
     * @param highValue Known high conductivity value in µS/cm (e.g., 1413 or 80000)
     * @return true if calibration successful, false otherwise
     */
    bool calibrateHighPoint(float highValue);

    /**
     * Get the current conductivity reading
     * @return Conductivity in µS/cm
     */
    float getConductivity() const { return getValue(); }

    /**
     * Calculate salinity from conductivity and temperature
     * Uses simplified practical salinity scale (PSS-78 approximation)
     * @param ec Conductivity in µS/cm
     * @param tempC Temperature in °C
     * @return Salinity in PSU (practical salinity units)
     */
    static float calculateSalinity(float ec, float tempC);

    /**
     * Get estimated salinity from last reading
     * Requires temperature compensation to have been set
     * @return Salinity in PSU, or 0 if temperature not set
     */
    float getSalinity() const;

protected:
    /**
     * Assess conductivity reading quality
     * @return SensorQuality enum
     */
    virtual SensorQuality assessQuality() override;

private:
    // Conductivity range constants (from datasheet)
    static constexpr float EC_MIN = 0.07;         // Minimum conductivity (µS/cm)
    static constexpr float EC_MAX = 500000.0;     // Maximum conductivity (µS/cm)

    // Typical seawater conductivity range for quality assessment
    static constexpr float SEAWATER_EC_MIN = 30000.0;   // Fresh/brackish water
    static constexpr float SEAWATER_EC_MAX = 60000.0;   // Typical seawater at 25°C

    // Temperature compensation
    float _lastTempCompensation;  // Last temperature used for compensation (°C)
    bool _tempCompensationSet;    // Has temperature compensation been set?

    /**
     * Check if conductivity is within valid sensor range
     * @param ec Conductivity in µS/cm
     * @return true if valid, false otherwise
     */
    bool isInValidRange(float ec) const;

    /**
     * Check if conductivity is within typical seawater range
     * @param ec Conductivity in µS/cm
     * @return true if typical, false if unusual
     */
    bool isTypicalSeawaterEC(float ec) const;
};

#endif // EZO_EC_H
