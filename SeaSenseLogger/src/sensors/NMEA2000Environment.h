/**
 * SeaSense Logger - NMEA2000 Environmental Data Listener
 *
 * Listens on NMEA2000 CAN bus for environmental parameters from the boat's
 * instruments (wind, depth, pressure, etc.) and caches the latest values.
 *
 * Designed to be sampled alongside EZO sensor readings - call getSnapshot()
 * at each measurement cycle to capture the current environmental context.
 *
 * Shares the same CAN bus / tNMEA2000 instance as NMEA2000GPS to avoid
 * duplicate CAN driver initialization.
 *
 * PGNs handled:
 * - PGN 130306: Wind Data (speed, angle, reference)
 * - PGN 128259: Speed Through Water
 * - PGN 128267: Water Depth
 * - PGN 130310: Environmental Parameters (water temp, air temp, pressure)
 * - PGN 130311: Environmental Parameters (temp, humidity, pressure)
 * - PGN 130312: Temperature (extended sources)
 * - PGN 130313: Humidity
 * - PGN 129026: COG & SOG
 * - PGN 127250: Vessel Heading
 * - PGN 127257: Attitude (pitch, roll, yaw)
 * - PGN 129033: Time & Date
 */

#ifndef NMEA2000_ENVIRONMENT_H
#define NMEA2000_ENVIRONMENT_H

#include <Arduino.h>
#include <NMEA2000.h>
#include <N2kMessages.h>

// Staleness threshold - mark individual fields invalid if no update for this long
#define N2K_ENV_STALE_MS 10000

/**
 * Snapshot of all cached NMEA2000 environmental data.
 * NaN values indicate "not available" (sensor not present on bus).
 */
struct N2kEnvironmentData {
    // Wind
    float windSpeedTrue;        // m/s (NaN if unavailable)
    float windAngleTrue;        // degrees (NaN if unavailable)
    float windSpeedApparent;    // m/s (NaN if unavailable)
    float windAngleApparent;    // degrees (NaN if unavailable)

    // Water
    float waterDepth;           // meters below transducer (NaN if unavailable)
    float depthOffset;          // transducer offset in meters (NaN if unavailable)
    float speedThroughWater;    // m/s (NaN if unavailable)

    // Temperature (from boat instruments, not EZO)
    float waterTempExternal;    // °C from transducer (NaN if unavailable)
    float airTemp;              // °C (NaN if unavailable)

    // Atmosphere
    float baroPressure;         // Pascals (NaN if unavailable)
    float humidity;             // % relative (NaN if unavailable)

    // Navigation
    float cogTrue;              // degrees (NaN if unavailable)
    float sog;                  // m/s (NaN if unavailable)
    float heading;              // degrees true (NaN if unavailable)

    // Attitude
    float pitch;                // degrees (NaN if unavailable)
    float roll;                 // degrees (NaN if unavailable)
    float yaw;                  // degrees (NaN if unavailable)

    // Validity flags
    bool hasWind;
    bool hasDepth;
    bool hasSpeedThroughWater;
    bool hasWaterTempExternal;
    bool hasAirTemp;
    bool hasBaroPressure;
    bool hasHumidity;
    bool hasCOGSOG;
    bool hasHeading;
    bool hasAttitude;
};

class NMEA2000Environment {
public:
    NMEA2000Environment();

    /**
     * Attach to an existing tNMEA2000 instance (shared with NMEA2000GPS).
     * Must be called after NMEA2000GPS::begin() has opened the CAN bus.
     * Extends the receive PGN list and installs a chained message handler.
     *
     * @param n2k Pointer to the tNMEA2000 instance from NMEA2000GPS
     */
    void begin(tNMEA2000* n2k);

    /**
     * Handle a received NMEA2000 message.
     * Called from the shared message handler trampoline.
     */
    void handleMsg(const tN2kMsg& msg);

    /**
     * Get a snapshot of the current cached environmental data.
     * Fields that haven't been received or are stale will be NaN.
     */
    N2kEnvironmentData getSnapshot() const;

    /**
     * Check if any environmental data has been received
     */
    bool hasAnyData() const;

    /**
     * Get a human-readable status string
     */
    String getStatusString() const;

private:
    // Cached values with per-field timestamps
    struct CachedField {
        float value;
        unsigned long lastUpdateMs;

        CachedField() : value(NAN), lastUpdateMs(0) {}

        void set(float v) {
            value = v;
            lastUpdateMs = millis();
        }

        bool isValid() const {
            return !isnan(value) && lastUpdateMs > 0 &&
                   (millis() - lastUpdateMs) < N2K_ENV_STALE_MS;
        }

        float get() const {
            return isValid() ? value : NAN;
        }
    };

    // Wind
    CachedField _windSpeedTrue;
    CachedField _windAngleTrue;
    CachedField _windSpeedApparent;
    CachedField _windAngleApparent;

    // Water
    CachedField _waterDepth;
    CachedField _depthOffset;
    CachedField _speedThroughWater;

    // Temperature
    CachedField _waterTempExternal;
    CachedField _airTemp;

    // Atmosphere
    CachedField _baroPressure;
    CachedField _humidity;

    // Navigation
    CachedField _cogTrue;
    CachedField _sog;
    CachedField _heading;

    // Attitude
    CachedField _pitch;
    CachedField _roll;
    CachedField _yaw;

    bool _initialized;

    // PGN handlers
    void handlePGN130306(const tN2kMsg& msg);  // Wind Data
    void handlePGN128259(const tN2kMsg& msg);  // Speed Through Water
    void handlePGN128267(const tN2kMsg& msg);  // Water Depth
    void handlePGN130310(const tN2kMsg& msg);  // Environmental Parameters
    void handlePGN130311(const tN2kMsg& msg);  // Environmental Parameters
    void handlePGN130312(const tN2kMsg& msg);  // Temperature
    void handlePGN130313(const tN2kMsg& msg);  // Humidity
    void handlePGN129026(const tN2kMsg& msg);  // COG & SOG
    void handlePGN127250(const tN2kMsg& msg);  // Vessel Heading
    void handlePGN127257(const tN2kMsg& msg);  // Attitude
};

#endif // NMEA2000_ENVIRONMENT_H
