/**
 * SeaSense Logger - GPS Module Implementation
 */

#include "GPSModule.h"

// ============================================================================
// Constructor
// ============================================================================

GPSModule::GPSModule(uint8_t rxPin, uint8_t txPin)
    : _serial(nullptr),
      _rxPin(rxPin),
      _txPin(txPin),
      _lastUpdateTime(0)
{
    _data.valid = false;
}

// ============================================================================
// Public Methods
// ============================================================================

bool GPSModule::begin(unsigned long baudRate) {
    // Use UART2 for GPS
    _serial = new HardwareSerial(2);

    if (!_serial) {
        Serial.println("[GPS] Failed to allocate serial port");
        return false;
    }

    _serial->begin(baudRate, SERIAL_8N1, _rxPin, _txPin);

    // Wait briefly for NMEA data to confirm a module is connected
    unsigned long start = millis();
    bool detected = false;
    while (millis() - start < 1500) {
        if (_serial->available()) {
            detected = true;
            break;
        }
        delay(10);
    }

    if (!detected) {
        Serial.println("[GPS] No GPS module detected on UART2");
        return false;
    }

    return true;
}

void GPSModule::update() {
    if (!_serial) {
        return;
    }

    // Process all available GPS data
    while (_serial->available() > 0) {
        char c = _serial->read();
        if (_gps.encode(c)) {
            // New data available
            updateData();
        }
    }

    // Check if GPS data is stale (no updates for 2 seconds)
    if (_gps.location.age() > 2000) {
        _data.valid = false;
    }
}

bool GPSModule::hasValidFix() const {
    return _data.valid &&
           _gps.location.isValid() &&
           _gps.date.isValid() &&
           _gps.time.isValid();
}

GPSData GPSModule::getData() const {
    return _data;
}

String GPSModule::getTimeUTC() const {
    if (!hasValidFix()) {
        return "";
    }

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             _data.year, _data.month, _data.day,
             _data.hour, _data.minute, _data.second);

    return String(buffer);
}

String GPSModule::getStatusString() {
    if (!hasValidFix()) {
        if (_gps.satellites.value() > 0) {
            return "Acquiring fix (" + String(_gps.satellites.value()) + " satellites)";
        } else {
            return "No satellites";
        }
    }

    char buffer[128];
    snprintf(buffer, sizeof(buffer),
             "Fixed (%d satellites, HDOP: %.1f)",
             _data.satellites, _data.hdop);

    return String(buffer);
}

unsigned long GPSModule::getAgeMs() const {
    return _gps.location.age();
}

// ============================================================================
// Private Methods
// ============================================================================

void GPSModule::updateData() {
    // Check if we have valid location and time
    if (!_gps.location.isValid() || !_gps.date.isValid() || !_gps.time.isValid()) {
        _data.valid = false;
        return;
    }

    // Update location
    _data.latitude = _gps.location.lat();
    _data.longitude = _gps.location.lng();
    _data.altitude = _gps.altitude.meters();

    // Update time
    _data.year = _gps.date.year();
    _data.month = _gps.date.month();
    _data.day = _gps.date.day();
    _data.hour = _gps.time.hour();
    _data.minute = _gps.time.minute();
    _data.second = _gps.time.second();

    // Calculate epoch timestamp
    _data.epoch = calculateEpoch(_data.year, _data.month, _data.day,
                                  _data.hour, _data.minute, _data.second);

    // Update quality metrics
    _data.satellites = _gps.satellites.value();
    _data.hdop = _gps.hdop.hdop();

    // Mark as valid
    _data.valid = true;
    _lastUpdateTime = millis();

    // Log first fix
    static bool firstFix = true;
    if (firstFix) {
        Serial.println();
        Serial.println("[GPS] ✓ GPS fix acquired!");
        Serial.print("[GPS] Location: ");
        Serial.print(_data.latitude, 6);
        Serial.print("° N, ");
        Serial.print(_data.longitude, 6);
        Serial.println("° E");
        Serial.print("[GPS] Time: ");
        Serial.println(getTimeUTC());
        Serial.print("[GPS] Satellites: ");
        Serial.print(_data.satellites);
        Serial.print(", HDOP: ");
        Serial.println(_data.hdop, 1);
        firstFix = false;
    }
}

time_t GPSModule::calculateEpoch(uint16_t year, uint8_t month, uint8_t day,
                                  uint8_t hour, uint8_t minute, uint8_t second) const {
    // Convert GPS time to Unix epoch timestamp
    // This is a simplified calculation assuming GPS time is already UTC
    struct tm timeinfo;
    timeinfo.tm_year = year - 1900;  // Years since 1900
    timeinfo.tm_mon = month - 1;      // Months since January (0-11)
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;
    timeinfo.tm_isdst = 0;            // No daylight saving time for UTC

    return mktime(&timeinfo);
}
