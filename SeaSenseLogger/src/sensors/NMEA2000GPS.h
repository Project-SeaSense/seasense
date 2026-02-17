/**
 * SeaSense Logger - NMEA2000 GPS Source
 *
 * Listens on NMEA2000 CAN bus for GPS position and time data.
 * Provides the same interface as GPSModule so either source can be used
 * interchangeably in the main loop.
 *
 * Uses ESP32 built-in TWAI controller (driver/twai.h) directly, compatible
 * with ESP32 Arduino SDK 3.x / ESP-IDF 5.x. No NMEA2000_esp32 library needed.
 *
 * PGNs handled:
 * - PGN 129029: GNSS Position Data (primary - lat/lon/alt/time/sats/HDOP)
 * - PGN 126992: System Time (time backup when 129029 not available)
 * - PGN 129025: Position Rapid Update (position backup when 129029 not available)
 *
 * Required library (git clone to Arduino/libraries/):
 * - NMEA2000 by Timo Lappalainen: https://github.com/ttlappalainen/NMEA2000
 */

#ifndef NMEA2000_GPS_H
#define NMEA2000_GPS_H

#include <Arduino.h>
#include <NMEA2000.h>
#include <N2kMessages.h>
#include <time.h>
#include "GPSModule.h"  // reuse GPSData struct

// Staleness threshold - mark data invalid if no update for this long
#define N2K_GPS_STALE_MS 5000

class NMEA2000GPS {
public:
    /**
     * Constructor
     * Uses CAN pins from hardware_config.h (CAN_TX_PIN, CAN_RX_PIN)
     */
    NMEA2000GPS();

    ~NMEA2000GPS();

    /**
     * Initialize CAN bus in listen-only mode
     * @return true if CAN bus opened successfully
     */
    bool begin();

    /**
     * Process pending CAN messages
     * Call frequently in loop() to receive GPS updates
     */
    void update();

    /**
     * Check if GPS data is valid and recent
     * @return true if position and time are valid and not stale
     */
    bool hasValidFix() const;

    /**
     * Get current GPS data
     * @return GPSData structure (same type as GPSModule)
     */
    GPSData getData() const;

    /**
     * Get time in ISO 8601 UTC format
     * @return String like "2024-05-15T10:28:23Z", or "" if no fix
     */
    String getTimeUTC() const;

    /**
     * Get status string for diagnostics
     * @return Human-readable status
     */
    String getStatusString();

    /**
     * Get milliseconds since last valid data update
     * @return Age in ms, or ULONG_MAX if never received
     */
    unsigned long getAgeMs() const;

    /**
     * Get the underlying tNMEA2000 instance.
     * Used by NMEA2000Environment to share the same CAN bus.
     * @return Pointer to tNMEA2000 instance, or nullptr if not initialized
     */
    tNMEA2000* getN2kInstance() const { return _n2k; }

    /**
     * Set a callback for forwarding messages to additional listeners
     * (e.g., NMEA2000Environment). Called after GPS handles each message.
     */
    typedef void (*MsgForwardCallback)(const tN2kMsg& msg);
    void setMsgForwardCallback(MsgForwardCallback cb) { _forwardCallback = cb; }

private:
    tNMEA2000* _n2k;  // owned NMEA2000 instance using custom TWAI driver
    GPSData _data;
    bool _initialized;
    unsigned long _lastUpdateMs;

    bool _hasPosition;
    bool _hasTime;

    MsgForwardCallback _forwardCallback;

    static NMEA2000GPS* _instance;
    static void staticMsgHandler(const tN2kMsg& msg);

    void handleMsg(const tN2kMsg& msg);
    void handlePGN129029(const tN2kMsg& msg);
    void handlePGN126992(const tN2kMsg& msg);
    void handlePGN129025(const tN2kMsg& msg);

    time_t calculateEpoch(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hour, uint8_t minute, uint8_t second) const;
};

#endif // NMEA2000_GPS_H
