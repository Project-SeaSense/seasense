/**
 * SeaSense Logger - BNO085 IMU Module
 *
 * Hull-mounted BNO085 provides pitch, roll and linear acceleration.
 * When present, overrides N2K pitch/roll (but NOT heading — magnetometer
 * is unreliable on metal hulls; N2K compass is more accurate).
 *
 * Uses Adafruit BNO08x library (SH2 protocol over I2C).
 * Reports enabled: SH2_ROTATION_VECTOR (100Hz), SH2_LINEAR_ACCELERATION (100Hz).
 * DCD (Dynamic Calibration Data) is saved to BNO085 flash every 5 minutes.
 */

#ifndef BNO085_MODULE_H
#define BNO085_MODULE_H

#include <Arduino.h>
#include "../../config/hardware_config.h"

// Forward declarations — avoid pulling in full Adafruit headers in every
// translation unit.  The .cpp includes the real headers.
#ifndef NATIVE_TEST
#include <Adafruit_BNO08x.h>
#endif

/**
 * Snapshot of IMU data at a single point in time.
 * NaN values indicate "not available" or stale.
 */
struct IMUData {
    // Orientation (degrees)
    float pitch;            // degrees (positive = bow up)
    float roll;             // degrees (positive = starboard down)
    float heading;          // degrees true (0-360)

    // Linear acceleration (m/s², gravity removed)
    float linAccelX;
    float linAccelY;
    float linAccelZ;

    // Validity
    bool hasOrientation;
    bool hasLinAccel;
};

class BNO085Module {
public:
    BNO085Module();

    /**
     * Initialize the BNO085 over I2C.
     * @return true if sensor detected and reports enabled
     */
    bool begin();

    /**
     * Non-blocking update — drain SH2 event queue, convert quaternion to euler.
     * Call this every loop iteration.
     */
    void update();

    /**
     * Get a snapshot of the current IMU data.
     * Stale fields (>BNO085_STALE_MS) are set to NaN.
     */
    IMUData getSnapshot() const;

    /**
     * @return true if the sensor was detected and initialized
     */
    bool isInitialized() const { return _initialized; }

    /**
     * Human-readable status string for serial/web output.
     */
    String getStatusString() const;

    /**
     * Age in ms since last orientation/accel update (ULONG_MAX if never).
     */
    unsigned long getOrientationAgeMs() const;
    unsigned long getAccelAgeMs() const;

private:
#ifndef NATIVE_TEST
    Adafruit_BNO08x _bno;

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
                   (millis() - lastUpdateMs) < BNO085_STALE_MS;
        }

        unsigned long ageMs() const {
            if (lastUpdateMs == 0) return ULONG_MAX;
            return millis() - lastUpdateMs;
        }

        float get() const {
            return isValid() ? value : NAN;
        }
    };

    CachedField _pitch;
    CachedField _roll;
    CachedField _heading;
    CachedField _linAccelX;
    CachedField _linAccelY;
    CachedField _linAccelZ;

    static constexpr unsigned long DCD_SAVE_INTERVAL_MS = 300000;  // 5 minutes
    unsigned long _lastDcdSaveMs;

    bool enableReports();
    void quaternionToEuler(float qr, float qi, float qj, float qk);
    void saveDcdIfDue();
#endif

    bool _initialized;
};

#endif // BNO085_MODULE_H
