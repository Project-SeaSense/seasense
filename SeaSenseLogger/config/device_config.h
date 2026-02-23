/**
 * SeaSense Logger v2 - Device Configuration
 *
 * Device metadata and sensor configuration
 * This file stores complete sensor lifecycle information including
 * purchase dates, deployment dates, and full calibration history.
 *
 * Format: C++ header with JSON-like structure stored as a raw string
 * The JSON is parsed at runtime using ArduinoJson library
 */

#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

// ============================================================================
// Device Configuration JSON
// ============================================================================

const char DEVICE_CONFIG_JSON[] PROGMEM = R"JSON({
  "device_guid": "seasense-v2-001",
  "partner_id": "test-partner",
  "firmware_version": "2.0.0",
  "sensors": [
    {
      "name": "Atlas EZO-RTD",
      "type": "Temperature",
      "manufacturer": "Atlas Scientific",
      "model": "EZO-RTD",
      "serial_number": "RTD-12345",
      "i2c_address": "0x66",
      "instance": 1,
      "unit": "°C",
      "depth_cm": 10,
      "purchase_date": "2024-01-15T00:00:00Z",
      "deploy_date": "2024-05-01T00:00:00Z",
      "calibration": [
        {
          "date": "2024-01-15T12:00:00Z",
          "type": "factory",
          "note": "Factory calibration"
        }
      ],
      "enabled": true
    },
    {
      "name": "Atlas EZO-EC",
      "type": "Conductivity",
      "manufacturer": "Atlas Scientific",
      "model": "EZO-EC",
      "serial_number": "EC-67890",
      "i2c_address": "0x64",
      "instance": 0,
      "unit": "µS/cm",
      "depth_cm": 10,
      "purchase_date": "2024-01-15T00:00:00Z",
      "deploy_date": "2024-05-01T00:00:00Z",
      "calibration": [
        {
          "date": "2024-05-10T12:00:00Z",
          "type": "single",
          "value": 1413,
          "note": "Single point at 1413µS/cm solution"
        }
      ],
      "enabled": true
    },
    {
      "name": "Atlas EZO-DO",
      "type": "Dissolved Oxygen",
      "manufacturer": "Atlas Scientific",
      "model": "EZO-DO",
      "serial_number": "DO-00000",
      "i2c_address": "0x61",
      "instance": 0,
      "unit": "mg/L",
      "depth_cm": 10,
      "purchase_date": null,
      "deploy_date": null,
      "calibration": [],
      "enabled": false,
      "note": "Future sensor - not yet deployed"
    },
    {
      "name": "Atlas EZO-pH",
      "type": "pH",
      "manufacturer": "Atlas Scientific",
      "model": "EZO-pH",
      "serial_number": "PH-00000",
      "i2c_address": "0x63",
      "instance": 0,
      "unit": "pH",
      "depth_cm": 10,
      "purchase_date": null,
      "deploy_date": null,
      "calibration": [],
      "enabled": false,
      "note": "Future sensor - not yet deployed"
    }
  ]
})JSON";

// ============================================================================
// Configuration Helper Functions
// ============================================================================

/**
 * Get device GUID from configuration
 * Must be called after parseDeviceConfig()
 */
extern String getDeviceGUID();

/**
 * Get partner ID from configuration
 * Must be called after parseDeviceConfig()
 */
extern String getPartnerID();

/**
 * Get firmware version from configuration
 * Must be called after parseDeviceConfig()
 */
extern String getFirmwareVersion();

/**
 * Get sensor metadata by type
 * @param sensorType Type of sensor (e.g., "Temperature", "Conductivity")
 * @return JSON object with sensor metadata, or null if not found
 */
extern JsonObject getSensorMetadata(const String& sensorType);

/**
 * Get all enabled sensors
 * @return Array of enabled sensor metadata
 */
extern JsonArray getEnabledSensors();

/**
 * Update sensor calibration
 * Adds a new calibration entry to the sensor's calibration history
 * @param sensorType Type of sensor
 * @param calibrationType Type of calibration (e.g., "dry", "single", "two-point")
 * @param calibrationValue Optional calibration value
 * @param note Optional note about the calibration
 * @return true if successful, false if sensor not found
 */
extern bool updateSensorCalibration(
    const String& sensorType,
    const String& calibrationType,
    float calibrationValue = 0,
    const String& note = ""
);

/**
 * Save updated device configuration to SPIFFS
 * @return true if successful, false otherwise
 */
extern bool saveDeviceConfig();

/**
 * Parse device configuration JSON
 * Must be called during setup() to load configuration
 * @return true if successful, false if JSON parsing failed
 */
extern bool parseDeviceConfig();

/**
 * Get last calibration date for a sensor
 * @param sensorType Type of sensor
 * @return ISO 8601 formatted date string, or empty string if no calibration
 */
extern String getLastCalibrationDate(const String& sensorType);

/**
 * Check if sensor is enabled
 * @param sensorType Type of sensor
 * @return true if enabled, false otherwise
 */
extern bool isSensorEnabled(const String& sensorType);

/**
 * Enable or disable a sensor
 * @param sensorType Type of sensor
 * @param enabled true to enable, false to disable
 * @return true if successful, false if sensor not found
 */
extern bool setSensorEnabled(const String& sensorType, bool enabled);

// ============================================================================
// Notes on JSON Structure
// ============================================================================

/*
 * Sensor Metadata Fields:
 *
 * - name: Human-readable sensor name
 * - type: Sensor type identifier (used for lookups in code)
 * - manufacturer: Sensor manufacturer name
 * - model: Specific model number/name
 * - serial_number: Unique serial number for this physical sensor
 * - i2c_address: I2C address in hex format (e.g., "0x66")
 * - instance: NMEA2000 instance number (0-255) for this sensor type
 * - unit: Measurement unit
 * - depth_cm: Deployment depth in centimeters below waterline
 * - purchase_date: ISO 8601 date when sensor was purchased
 * - deploy_date: ISO 8601 date when sensor was deployed
 * - calibration: Array of calibration records (chronological order)
 *   - date: ISO 8601 timestamp of calibration
 *   - type: Calibration type (factory, dry, single, two-point, etc.)
 *   - value: Optional calibration value (e.g., solution concentration)
 *   - note: Human-readable note about the calibration
 * - enabled: Whether this sensor is currently active
 * - note: Optional notes about the sensor
 *
 * Calibration History:
 * The calibration array maintains a complete history of all calibrations
 * performed on the sensor. This provides full traceability for data quality
 * and regulatory compliance. The most recent calibration is used for
 * metadata in CSV files and NMEA2000 PGNs.
 *
 * Linkage to CSV Data:
 * Each CSV row includes sensor_serial and sensor_instance, which link back
 * to this configuration for complete sensor provenance. The last calibration
 * date is included in the CSV for quick reference.
 */

#endif // DEVICE_CONFIG_H
