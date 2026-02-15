/**
 * SeaSense Logger v2 - EZO-RTD Temperature Sensor
 *
 * Atlas Scientific EZO-RTD temperature sensor
 * - Measurement range: -126°C to +1254°C
 * - Accuracy: ±0.1°C (factory calibration)
 * - Response time: 600ms
 * - Usually factory calibrated - no field calibration needed
 *
 * Calibration commands (if needed):
 * - Cal,clear - Clear calibration
 * - Cal,t - Single point calibration at current temperature
 */

#ifndef EZO_RTD_H
#define EZO_RTD_H

#include "EZOSensor.h"
#include "../../config/hardware_config.h"

class EZO_RTD : public EZOSensor {
public:
    /**
     * Constructor
     * Uses default I2C address 0x66 from hardware_config.h
     */
    EZO_RTD();

    /**
     * Constructor with custom I2C address
     * @param i2cAddress Custom I2C address
     */
    explicit EZO_RTD(uint8_t i2cAddress);

    virtual ~EZO_RTD() {}

    /**
     * Perform single-point temperature calibration
     * @param referenceTemp Known reference temperature in °C
     * @return true if calibration successful, false otherwise
     */
    bool calibrate(float referenceTemp);

    /**
     * Get the current temperature reading
     * @return Temperature in °C
     */
    float getTemperatureC() const { return getValue(); }

    /**
     * Get the current temperature in Fahrenheit
     * @return Temperature in °F
     */
    float getTemperatureF() const { return (getValue() * 9.0 / 5.0) + 32.0; }

    /**
     * Get the current temperature in Kelvin
     * @return Temperature in K
     */
    float getTemperatureK() const { return getValue() + 273.15; }

protected:
    /**
     * Assess temperature reading quality
     * @return SensorQuality enum
     */
    virtual SensorQuality assessQuality() override;

private:
    // Temperature range constants (from datasheet)
    static constexpr float TEMP_MIN = -126.0;  // Minimum valid temperature (°C)
    static constexpr float TEMP_MAX = 1254.0;  // Maximum valid temperature (°C)

    // Typical ocean temperature range for quality assessment
    static constexpr float OCEAN_TEMP_MIN = -2.0;  // Freezing seawater
    static constexpr float OCEAN_TEMP_MAX = 35.0;  // Warm tropical surface water

    /**
     * Check if temperature is within valid sensor range
     * @param temp Temperature in °C
     * @return true if valid, false otherwise
     */
    bool isInValidRange(float temp) const;

    /**
     * Check if temperature is within typical ocean range
     * @param temp Temperature in °C
     * @return true if typical, false if unusual
     */
    bool isTypicalOceanTemp(float temp) const;
};

#endif // EZO_RTD_H
