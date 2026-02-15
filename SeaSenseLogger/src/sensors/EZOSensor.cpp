/**
 * SeaSense Logger - EZO Sensor Base Class Implementation
 */

#include "EZOSensor.h"
#include "../../config/hardware_config.h"
#include <ArduinoJson.h>

// External function declarations (implemented in main .ino file)
extern JsonObject getSensorMetadata(const String& sensorType);

// ============================================================================
// Constructor
// ============================================================================

EZOSensor::EZOSensor(
    uint8_t i2cAddress,
    uint16_t responseTimeMs,
    const String& sensorType,
    const String& sensorModel,
    const String& unit
)
    : _i2cAddress(i2cAddress),
      _responseTimeMs(responseTimeMs),
      _sensorType(sensorType),
      _sensorModel(sensorModel),
      _unit(unit),
      _serialNumber(""),
      _instance(0),
      _calibrationDate(""),
      _enabled(true),
      _value(0.0),
      _timestamp(0),
      _valid(false),
      _quality(SensorQuality::NOT_CALIBRATED),
      _firmwareVersion(""),
      _deviceInfo("")
{
}

// ============================================================================
// ISensor Interface Implementation
// ============================================================================

bool EZOSensor::begin() {
    DEBUG_SENSOR_PRINT("Initializing ");
    DEBUG_SENSOR_PRINT(_sensorModel);
    DEBUG_SENSOR_PRINT(" at I2C address 0x");
    DEBUG_SENSOR_PRINTLN(String(_i2cAddress, HEX));

    // Check if sensor is present on I2C bus
    if (!isPresent()) {
        DEBUG_SENSOR_PRINTLN("Sensor not found on I2C bus");
        return false;
    }

    // Load metadata from device_config.h
    if (!loadMetadata()) {
        DEBUG_SENSOR_PRINTLN("Warning: Could not load sensor metadata");
        // Continue anyway - metadata is not critical for operation
    }

    // Get device information
    _deviceInfo = getDeviceInfo();
    DEBUG_SENSOR_PRINT("Device info: ");
    DEBUG_SENSOR_PRINTLN(_deviceInfo);

    // Parse firmware version from device info
    // Device info format: "?I,RTD,1.0"
    int commaIndex = _deviceInfo.indexOf(',');
    if (commaIndex > 0) {
        int secondCommaIndex = _deviceInfo.indexOf(',', commaIndex + 1);
        if (secondCommaIndex > 0) {
            _firmwareVersion = _deviceInfo.substring(secondCommaIndex + 1);
        }
    }

    DEBUG_SENSOR_PRINTLN("Sensor initialized successfully");
    return true;
}

bool EZOSensor::read() {
    if (!_enabled) {
        DEBUG_SENSOR_PRINTLN("Sensor is disabled");
        return false;
    }

    if (!isPresent()) {
        DEBUG_SENSOR_PRINTLN("Sensor not present");
        _valid = false;
        _quality = SensorQuality::ERROR;
        return false;
    }

    // Send read command
    String response;
    EZOResponseCode code = sendCommand("R", response, _responseTimeMs);

    if (code != EZOResponseCode::SUCCESS) {
        DEBUG_SENSOR_PRINT("Read failed with code: ");
        DEBUG_SENSOR_PRINTLN((int)code);
        _valid = false;
        _quality = SensorQuality::ERROR;
        return false;
    }

    // Parse the response
    if (!parseReading(response)) {
        DEBUG_SENSOR_PRINTLN("Failed to parse reading");
        _valid = false;
        _quality = SensorQuality::ERROR;
        return false;
    }

    // Record timestamp
    _timestamp = millis();
    _valid = true;

    // Assess quality
    _quality = assessQuality();

    DEBUG_SENSOR_PRINT("Read successful: ");
    DEBUG_SENSOR_PRINT(_value);
    DEBUG_SENSOR_PRINT(" ");
    DEBUG_SENSOR_PRINT(_unit);
    DEBUG_SENSOR_PRINT(" (quality: ");
    DEBUG_SENSOR_PRINT(sensorQualityToString(_quality));
    DEBUG_SENSOR_PRINTLN(")");

    return true;
}

SensorData EZOSensor::getData() const {
    SensorData data;
    data.sensorType = _sensorType;
    data.sensorModel = _sensorModel;
    data.sensorSerial = _serialNumber;
    data.sensorInstance = _instance;
    data.calibrationDate = _calibrationDate;
    data.value = _value;
    data.unit = _unit;
    data.quality = _quality;
    data.timestamp = _timestamp;
    data.valid = _valid;
    return data;
}

float EZOSensor::getValue() const {
    return _value;
}

String EZOSensor::getUnit() const {
    return _unit;
}

String EZOSensor::getSensorType() const {
    return _sensorType;
}

String EZOSensor::getSensorModel() const {
    return _sensorModel;
}

String EZOSensor::getSerialNumber() const {
    return _serialNumber;
}

uint8_t EZOSensor::getInstance() const {
    return _instance;
}

bool EZOSensor::isValid() const {
    return _valid;
}

SensorQuality EZOSensor::getQuality() const {
    return _quality;
}

String EZOSensor::getQualityString() const {
    return sensorQualityToString(_quality);
}

bool EZOSensor::isEnabled() const {
    return _enabled;
}

void EZOSensor::setEnabled(bool enabled) {
    _enabled = enabled;
}

String EZOSensor::getLastCalibrationDate() const {
    return _calibrationDate;
}

bool EZOSensor::selfTest() {
    // Check if sensor is present
    if (!isPresent()) {
        return false;
    }

    // Try to get device status
    String status = getDeviceStatus();
    if (status.length() == 0) {
        return false;
    }

    // Try a read operation
    String response;
    EZOResponseCode code = sendCommand("R", response, _responseTimeMs);

    return (code == EZOResponseCode::SUCCESS);
}

String EZOSensor::getStatusString() const {
    String status = _sensorModel;
    status += " (0x" + String(_i2cAddress, HEX) + ")";
    status += " - ";

    if (!_enabled) {
        status += "DISABLED";
    } else if (!_valid) {
        status += "ERROR";
    } else {
        status += String(_value, 2) + " " + _unit;
        status += " [" + sensorQualityToString(_quality) + "]";
    }

    return status;
}

// ============================================================================
// EZO-Specific Methods
// ============================================================================

EZOResponseCode EZOSensor::sendCommand(
    const String& command,
    String& response,
    uint16_t waitTime
) {
    // Use sensor's default response time if not specified
    if (waitTime == 0) {
        waitTime = _responseTimeMs;
    }

    DEBUG_SENSOR_PRINT("Sending command: ");
    DEBUG_SENSOR_PRINTLN(command);

    // Write command to sensor
    if (!writeI2C(command)) {
        DEBUG_SENSOR_PRINTLN("Failed to write command");
        return EZOResponseCode::ERROR;
    }

    // Wait for sensor to process command
    delay(waitTime);

    // Read response
    char buffer[64];
    int bytesRead = readI2C(buffer, sizeof(buffer));

    if (bytesRead <= 0) {
        DEBUG_SENSOR_PRINTLN("No response from sensor");
        return EZOResponseCode::NO_DATA;
    }

    // Parse response code (first byte)
    EZOResponseCode code = parseResponseCode(buffer[0]);

    // Extract response text (skip first byte which is response code)
    if (bytesRead > 1) {
        response = String(buffer + 1);
        response.trim();
    } else {
        response = "";
    }

    DEBUG_SENSOR_PRINT("Response code: ");
    DEBUG_SENSOR_PRINT((int)code);
    DEBUG_SENSOR_PRINT(", Response: ");
    DEBUG_SENSOR_PRINTLN(response);

    return code;
}

String EZOSensor::getDeviceInfo() {
    String response;
    EZOResponseCode code = sendCommand("I", response, 300);

    if (code == EZOResponseCode::SUCCESS) {
        return response;
    }

    return "";
}

String EZOSensor::getDeviceStatus() {
    String response;
    EZOResponseCode code = sendCommand("Status", response, 300);

    if (code == EZOResponseCode::SUCCESS) {
        return response;
    }

    return "";
}

bool EZOSensor::sleep() {
    String response;
    EZOResponseCode code = sendCommand("Sleep", response, 300);
    return (code == EZOResponseCode::SUCCESS);
}

bool EZOSensor::wake() {
    // Any command will wake the device
    // Use a simple read command
    String response;
    EZOResponseCode code = sendCommand("R", response, _responseTimeMs);
    return (code == EZOResponseCode::SUCCESS || code == EZOResponseCode::PROCESSING);
}

bool EZOSensor::clearCalibration() {
    String response;
    EZOResponseCode code = sendCommand("Cal,clear", response, 300);
    return (code == EZOResponseCode::SUCCESS);
}

bool EZOSensor::isPresent() {
    Wire.beginTransmission(_i2cAddress);
    return (Wire.endTransmission() == 0);
}

// ============================================================================
// Protected Methods
// ============================================================================

bool EZOSensor::parseReading(const String& response) {
    // Default implementation: parse as simple float
    // Override in derived classes if needed
    _value = response.toFloat();
    return true;
}

SensorQuality EZOSensor::assessQuality() {
    // Default implementation: assume good if reading is valid
    // Override in derived classes for sensor-specific quality checks

    if (!_valid) {
        return SensorQuality::ERROR;
    }

    if (_calibrationDate.length() == 0) {
        return SensorQuality::NOT_CALIBRATED;
    }

    // TODO: Check calibration age and return FAIR if old (>1 year)
    // For now, just return GOOD
    return SensorQuality::GOOD;
}

bool EZOSensor::loadMetadata() {
    // Get sensor metadata from device_config.h
    JsonObject metadata = getSensorMetadata(_sensorType);

    if (metadata.isNull()) {
        DEBUG_SENSOR_PRINTLN("No metadata found for sensor type");
        return false;
    }

    // Load metadata fields
    if (metadata.containsKey("serial_number")) {
        _serialNumber = metadata["serial_number"].as<String>();
    }

    if (metadata.containsKey("instance")) {
        _instance = metadata["instance"].as<uint8_t>();
    }

    if (metadata.containsKey("enabled")) {
        _enabled = metadata["enabled"].as<bool>();
    }

    // Get last calibration date
    if (metadata.containsKey("calibration")) {
        JsonArray calibrations = metadata["calibration"].as<JsonArray>();
        if (calibrations.size() > 0) {
            // Get the last calibration entry
            JsonObject lastCal = calibrations[calibrations.size() - 1];
            if (lastCal.containsKey("date")) {
                _calibrationDate = lastCal["date"].as<String>();
            }
        }
    }

    return true;
}

// ============================================================================
// Private I2C Communication Methods
// ============================================================================

bool EZOSensor::writeI2C(const String& command) {
    Wire.beginTransmission(_i2cAddress);

    // Write command string
    for (size_t i = 0; i < command.length(); i++) {
        Wire.write(command[i]);
    }

    // EZO sensors expect commands to end with \r
    Wire.write('\r');

    uint8_t error = Wire.endTransmission();

    return (error == 0);
}

int EZOSensor::readI2C(char* buffer, size_t maxLength) {
    Wire.requestFrom(_i2cAddress, (uint8_t)maxLength);

    int i = 0;
    while (Wire.available() && i < maxLength - 1) {
        buffer[i] = Wire.read();

        // Stop reading if we hit a null terminator
        if (buffer[i] == 0) {
            break;
        }

        i++;
    }

    // Null terminate the string
    buffer[i] = '\0';

    return i;
}

EZOResponseCode EZOSensor::parseResponseCode(uint8_t responseByte) {
    switch (responseByte) {
        case 1:
            return EZOResponseCode::SUCCESS;
        case 2:
            return EZOResponseCode::ERROR;
        case 254:
            return EZOResponseCode::PROCESSING;
        case 255:
            return EZOResponseCode::NO_DATA;
        default:
            // If the first byte is not a response code, it's part of the data
            // This happens with older firmware
            return EZOResponseCode::SUCCESS;
    }
}
