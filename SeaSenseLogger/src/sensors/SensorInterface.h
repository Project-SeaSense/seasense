/**
 * SeaSense Logger v2 - Sensor Interface
 *
 * Abstract base class for all sensor implementations
 * Defines the contract that all sensors must implement
 */

#ifndef SENSOR_INTERFACE_H
#define SENSOR_INTERFACE_H

#include <Arduino.h>

/**
 * Quality indicators for sensor readings
 */
enum class SensorQuality {
    GOOD,           // Reading is valid and within expected range
    FAIR,           // Reading is valid but may be less accurate (e.g., old calibration)
    POOR,           // Reading is questionable (e.g., sensor drift, environmental issues)
    ERROR,          // Reading failed or invalid
    NOT_CALIBRATED  // Sensor is not calibrated
};

/**
 * Sensor data structure
 * Contains a single sensor reading with metadata
 */
struct SensorData {
    String sensorType;          // e.g., "Temperature", "Conductivity"
    String sensorModel;         // e.g., "EZO-RTD", "EZO-EC"
    String sensorSerial;        // e.g., "RTD-12345"
    uint8_t sensorInstance;     // NMEA2000 instance number
    String calibrationDate;     // ISO 8601 date of last calibration
    float value;                // Sensor reading value
    String unit;                // Unit of measurement (e.g., "°C", "µS/cm")
    SensorQuality quality;      // Quality indicator
    unsigned long timestamp;    // millis() when reading was taken
    bool valid;                 // Overall validity flag
};

/**
 * Abstract sensor interface
 * All sensor classes must inherit from this interface
 */
class ISensor {
public:
    virtual ~ISensor() {}

    /**
     * Initialize the sensor
     * @return true if initialization successful, false otherwise
     */
    virtual bool begin() = 0;

    /**
     * Read a value from the sensor
     * This may block for the sensor's response time
     * @return true if read successful, false otherwise
     */
    virtual bool read() = 0;

    /**
     * Get the most recent sensor reading
     * @return SensorData structure with reading and metadata
     */
    virtual SensorData getData() const = 0;

    /**
     * Get the raw sensor value
     * @return float value of most recent reading
     */
    virtual float getValue() const = 0;

    /**
     * Get the sensor's measurement unit
     * @return String representation of unit (e.g., "°C", "µS/cm")
     */
    virtual String getUnit() const = 0;

    /**
     * Get the sensor type identifier
     * @return String type (e.g., "Temperature", "Conductivity")
     */
    virtual String getSensorType() const = 0;

    /**
     * Get the sensor model
     * @return String model (e.g., "EZO-RTD", "EZO-EC")
     */
    virtual String getSensorModel() const = 0;

    /**
     * Get the sensor serial number
     * @return String serial number
     */
    virtual String getSerialNumber() const = 0;

    /**
     * Get the NMEA2000 instance number
     * @return uint8_t instance (0-255)
     */
    virtual uint8_t getInstance() const = 0;

    /**
     * Check if the most recent reading is valid
     * @return true if valid, false otherwise
     */
    virtual bool isValid() const = 0;

    /**
     * Get the quality of the most recent reading
     * @return SensorQuality enum
     */
    virtual SensorQuality getQuality() const = 0;

    /**
     * Get human-readable quality string
     * @return String representation of quality
     */
    virtual String getQualityString() const = 0;

    /**
     * Check if sensor is enabled
     * @return true if enabled, false otherwise
     */
    virtual bool isEnabled() const = 0;

    /**
     * Enable or disable the sensor
     * @param enabled true to enable, false to disable
     */
    virtual void setEnabled(bool enabled) = 0;

    /**
     * Get the last calibration date
     * @return ISO 8601 formatted date string
     */
    virtual String getLastCalibrationDate() const = 0;

    /**
     * Perform sensor self-test
     * @return true if self-test passed, false otherwise
     */
    virtual bool selfTest() = 0;

    /**
     * Get sensor status information for diagnostics
     * @return String with human-readable status
     */
    virtual String getStatusString() const = 0;
};

/**
 * Helper function to convert SensorQuality enum to string
 */
inline String sensorQualityToString(SensorQuality quality) {
    switch (quality) {
        case SensorQuality::GOOD:           return "good";
        case SensorQuality::FAIR:           return "fair";
        case SensorQuality::POOR:           return "poor";
        case SensorQuality::ERROR:          return "error";
        case SensorQuality::NOT_CALIBRATED: return "not_calibrated";
        default:                            return "unknown";
    }
}

#endif // SENSOR_INTERFACE_H
