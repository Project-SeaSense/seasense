/**
 * SeaSense Logger - GPS Module
 *
 * NEO-6M GPS module for self-reliant time and location
 * - Accurate time without internet (atomic clock accuracy)
 * - Location for every datapoint (lat/lon)
 * - Works anywhere (no WiFi needed)
 */

#ifndef GPS_MODULE_H
#define GPS_MODULE_H

#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <time.h>

/**
 * GPS data structure
 */
struct GPSData {
    bool valid;              // GPS fix valid
    double latitude;         // Degrees
    double longitude;        // Degrees
    double altitude;         // Meters above sea level
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    time_t epoch;            // Unix timestamp
    uint8_t satellites;      // Number of satellites
    double hdop;             // Horizontal dilution of precision
};

class GPSModule {
public:
    /**
     * Constructor
     * @param rxPin RX pin (ESP32 receives from GPS TX)
     * @param txPin TX pin (ESP32 transmits to GPS RX)
     */
    GPSModule(uint8_t rxPin, uint8_t txPin);

    /**
     * Initialize GPS module
     * @param baudRate Serial baud rate (default 9600 for NEO-6M)
     * @return true if successful
     */
    bool begin(unsigned long baudRate = 9600);

    /**
     * Update GPS data
     * Call this frequently in loop() to process incoming NMEA sentences
     */
    void update();

    /**
     * Check if GPS has valid fix
     * @return true if position and time are valid
     */
    bool hasValidFix() const;

    /**
     * Get current GPS data
     * @return GPSData structure
     */
    GPSData getData() const;

    /**
     * Get time in ISO 8601 UTC format
     * @return String like "2024-05-15T10:28:23Z"
     */
    String getTimeUTC() const;

    /**
     * Get status string for diagnostics
     * @return Human-readable status
     */
    String getStatusString();

    /**
     * Get age of last GPS update
     * @return Milliseconds since last valid data
     */
    unsigned long getAgeMs() const;

private:
    HardwareSerial* _serial;
    TinyGPSPlus _gps;
    GPSData _data;
    uint8_t _rxPin;
    uint8_t _txPin;
    unsigned long _lastUpdateTime;

    /**
     * Update internal GPSData structure from TinyGPS
     */
    void updateData();

    /**
     * Calculate Unix epoch timestamp from GPS date/time
     */
    time_t calculateEpoch(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hour, uint8_t minute, uint8_t second) const;
};

#endif // GPS_MODULE_H
