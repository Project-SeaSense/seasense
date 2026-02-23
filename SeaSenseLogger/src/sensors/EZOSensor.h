/**
 * SeaSense Logger v2 - EZO Sensor Base Class
 *
 * Base class for Atlas Scientific EZO sensor family
 * Handles I2C ASCII protocol communication
 *
 * EZO Sensors use text-based commands:
 * - "R\r" - Read sensor value
 * - "Cal,xxx" - Calibrate sensor
 * - "I" - Get device information
 * - "Status" - Get device status
 * - "Sleep" - Put device to sleep
 *
 * Response format:
 * - ASCII text response
 * - Response codes: 1 (success), 2 (error), 254 (processing), 255 (no data)
 */

#ifndef EZO_SENSOR_H
#define EZO_SENSOR_H

#include <Arduino.h>
#include <Wire.h>
#include <time.h>
#include "SensorInterface.h"

/**
 * EZO sensor response codes
 */
enum class EZOResponseCode {
    SUCCESS = 1,      // Command successful
    ERROR = 2,        // Command failed
    PROCESSING = 254, // Command still processing
    NO_DATA = 255     // No data to send
};

/**
 * Base class for Atlas Scientific EZO sensors
 * Implements common I2C ASCII protocol communication
 */
class EZOSensor : public ISensor {
public:
    /**
     * Constructor
     * @param i2cAddress I2C address of the sensor
     * @param responseTimeMs Response time in milliseconds
     * @param sensorType Sensor type identifier (e.g., "Temperature")
     * @param sensorModel Sensor model (e.g., "EZO-RTD")
     * @param unit Measurement unit (e.g., "°C")
     */
    EZOSensor(
        uint8_t i2cAddress,
        uint16_t responseTimeMs,
        const String& sensorType,
        const String& sensorModel,
        const String& unit
    );

    virtual ~EZOSensor() {}

    // ========================================================================
    // ISensor interface implementation
    // ========================================================================

    virtual bool begin() override;
    virtual bool read() override;
    virtual SensorData getData() const override;
    virtual float getValue() const override;
    virtual String getUnit() const override;
    virtual String getSensorType() const override;
    virtual String getSensorModel() const override;
    virtual String getSerialNumber() const override;
    virtual uint8_t getInstance() const override;
    virtual bool isValid() const override;
    virtual SensorQuality getQuality() const override;
    virtual String getQualityString() const override;
    virtual bool isEnabled() const override;
    virtual void setEnabled(bool enabled) override;
    virtual String getLastCalibrationDate() const override;
    virtual bool selfTest() override;
    virtual String getStatusString() const override;

    // ========================================================================
    // EZO-specific methods
    // ========================================================================

    /**
     * Send a command to the sensor and read the response
     * @param command ASCII command string (without \r)
     * @param response String to store response
     * @param waitTime Time to wait for response (default: sensor's response time)
     * @return Response code
     */
    EZOResponseCode sendCommand(
        const String& command,
        String& response,
        uint16_t waitTime = 0
    );

    /**
     * Get device information (firmware version, etc.)
     * Sends "I" command
     * @return Device info string
     */
    String getDeviceInfo();

    /**
     * Get device status
     * Sends "Status" command
     * @return Status string
     */
    String getDeviceStatus();

    /**
     * Put device to sleep (low power mode)
     * Sends "Sleep" command
     * @return true if successful
     */
    bool sleep();

    /**
     * Wake device from sleep
     * Any command will wake the device
     * @return true if successful
     */
    bool wake();

    /**
     * Query calibration point count from the probe
     * Sends "Cal,?" command, parses "?Cal,N" response
     * @return Number of calibration points (0=uncalibrated), or -1 on error
     */
    int getCalibrationPoints();

    /**
     * Clear calibration data
     * Sends "Cal,clear" command
     * @return true if successful
     */
    bool clearCalibration();

    /**
     * Update calibration date in memory (called after successful calibration)
     * @param date ISO 8601 date string (e.g. "2025-06-01T14:00:00Z"), or "" if GPS unavailable
     */
    void setCalibrationDate(const String& date) { _calibrationDate = date; }

    /**
     * Set system epoch for calibration age checks
     * Call from main loop whenever GPS has a valid fix
     * @param t Unix timestamp (UTC)
     */
    static void setSystemEpoch(time_t t) { _systemEpoch = t; }

    /**
     * Check if sensor is responding on I2C bus
     * @return true if sensor is present and responding
     */
    bool isPresent();

    /**
     * Get the I2C address
     * @return uint8_t address
     */
    uint8_t getI2CAddress() const { return _i2cAddress; }

    /**
     * Get the response time
     * @return uint16_t response time in milliseconds
     */
    uint16_t getResponseTime() const { return _responseTimeMs; }

protected:
    // ========================================================================
    // Protected members for derived classes
    // ========================================================================

    uint8_t _i2cAddress;           // I2C address of sensor
    uint16_t _responseTimeMs;      // Response time in milliseconds
    String _sensorType;            // Sensor type (e.g., "Temperature")
    String _sensorModel;           // Sensor model (e.g., "EZO-RTD")
    String _unit;                  // Measurement unit
    String _serialNumber;          // Device serial number
    uint8_t _instance;             // NMEA2000 instance number
    String _calibrationDate;       // Last calibration date (ISO 8601)
    bool _enabled;                 // Is sensor enabled?

    // Current reading
    float _value;                  // Last sensor reading
    unsigned long _timestamp;      // millis() when reading was taken
    bool _valid;                   // Is current reading valid?
    SensorQuality _quality;        // Quality of current reading

    // Device info
    String _firmwareVersion;       // Firmware version from device
    String _deviceInfo;            // Full device info string

    /**
     * Check whether the calibration is older than maxAgeDays.
     * Returns true (stale) only when system time is known and date is parseable.
     */
    bool isCalibrationStale(int maxAgeDays) const;

    /**
     * Parse sensor reading from response string
     * Override this in derived classes for sensor-specific parsing
     * @param response Response string from sensor
     * @return true if parsing successful, false otherwise
     */
    virtual bool parseReading(const String& response);

    /**
     * Assess the quality of the current reading
     * Override this in derived classes for sensor-specific quality checks
     * @return SensorQuality enum
     */
    virtual SensorQuality assessQuality();

    /**
     * Load sensor metadata from device_config.h
     * Populates serial number, instance, calibration date, etc.
     * @return true if successful, false otherwise
     */
    bool loadMetadata();

    // System epoch (set from GPS); used for calibration age checks
    static time_t _systemEpoch;

private:
    // ========================================================================
    // Private I2C communication methods
    // ========================================================================

    /**
     * Write a command to the sensor via I2C
     * @param command ASCII command string
     * @return true if successful, false otherwise
     */
    bool writeI2C(const String& command);

    /**
     * Read response from sensor via I2C
     * @param buffer Buffer to store response
     * @param maxLength Maximum buffer length
     * @return Number of bytes read, or -1 on error
     */
    int readI2C(char* buffer, size_t maxLength);

    /**
     * Parse response code from first byte
     * @param responseByte First byte of response
     * @return EZOResponseCode enum
     */
    EZOResponseCode parseResponseCode(uint8_t responseByte);
};

#endif // EZO_SENSOR_H
