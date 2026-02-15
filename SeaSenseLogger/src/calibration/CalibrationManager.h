/**
 * SeaSense Logger - Calibration Manager
 *
 * Manages sensor calibration workflows for web UI
 * - Guided calibration procedures
 * - Status tracking and progress updates
 * - Automatic metadata updates
 */

#ifndef CALIBRATION_MANAGER_H
#define CALIBRATION_MANAGER_H

#include <Arduino.h>
#include "../sensors/EZO_RTD.h"
#include "../sensors/EZO_EC.h"

/**
 * Calibration status
 */
enum class CalibrationStatus {
    IDLE,               // No calibration in progress
    PREPARING,          // Preparing sensor
    WAITING_STABLE,     // Waiting for reading to stabilize
    CALIBRATING,        // Performing calibration
    COMPLETE,           // Calibration successful
    ERROR               // Calibration failed
};

/**
 * Calibration type
 */
enum class CalibrationType {
    NONE,
    TEMPERATURE_SINGLE,     // EZO-RTD single point
    EC_DRY,                 // EZO-EC dry calibration
    EC_SINGLE,              // EZO-EC single point
    EC_TWO_LOW,             // EZO-EC two-point low
    EC_TWO_HIGH             // EZO-EC two-point high
};

/**
 * Calibration state
 */
struct CalibrationState {
    CalibrationStatus status;
    CalibrationType type;
    String sensorType;          // "temperature" or "conductivity"
    float referenceValue;       // Expected value for calibration
    float currentReading;       // Current sensor reading
    unsigned long startTime;    // When calibration started
    unsigned long stableTime;   // When reading became stable
    String message;             // Status message
    bool success;               // Final result
};

class CalibrationManager {
public:
    /**
     * Constructor
     * @param tempSensor Pointer to temperature sensor
     * @param ecSensor Pointer to conductivity sensor
     */
    CalibrationManager(EZO_RTD* tempSensor, EZO_EC* ecSensor);

    /**
     * Start calibration procedure
     * @param sensorType "temperature" or "conductivity"
     * @param calibrationType Type of calibration
     * @param referenceValue Reference value (for single/two-point)
     * @return true if calibration started successfully
     */
    bool startCalibration(
        const String& sensorType,
        CalibrationType calibrationType,
        float referenceValue = 0.0
    );

    /**
     * Update calibration state machine
     * Call this periodically (e.g., every 100ms)
     */
    void update();

    /**
     * Get current calibration state
     * @return CalibrationState structure
     */
    CalibrationState getState() const { return _state; }

    /**
     * Check if calibration is in progress
     * @return true if calibrating
     */
    bool isCalibrating() const {
        return _state.status != CalibrationStatus::IDLE &&
               _state.status != CalibrationStatus::COMPLETE &&
               _state.status != CalibrationStatus::ERROR;
    }

    /**
     * Cancel current calibration
     */
    void cancel();

private:
    EZO_RTD* _tempSensor;
    EZO_EC* _ecSensor;
    CalibrationState _state;

    // Stability detection
    static const int STABILITY_SAMPLES = 5;
    float _stabilityBuffer[STABILITY_SAMPLES];
    int _stabilityIndex;
    unsigned long _lastReadingTime;

    /**
     * Check if sensor reading is stable
     * @param currentValue Current sensor reading
     * @return true if stable
     */
    bool isReadingStable(float currentValue);

    /**
     * Perform the actual calibration command
     * @return true if successful
     */
    bool performCalibration();

    /**
     * Reset calibration state
     */
    void resetState();
};

#endif // CALIBRATION_MANAGER_H
