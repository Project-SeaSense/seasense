/**
 * SeaSense Logger - BNO085 IMU Module Implementation
 */

#include "BNO085Module.h"

#ifndef NATIVE_TEST

#include <Wire.h>

BNO085Module::BNO085Module()
    : _bno(-1),  // no reset pin
      _lastDcdSaveMs(0),
      _initialized(false)
{
}

bool BNO085Module::begin() {
    if (!_bno.begin_I2C(BNO085_I2C_ADDR, &Wire)) {
        _initialized = false;
        return false;
    }

    if (!enableReports()) {
        _initialized = false;
        return false;
    }

    _initialized = true;
    return true;
}

bool BNO085Module::enableReports() {
    // Rotation vector at 100Hz (10ms interval) — fused orientation
    if (!_bno.enableReport(SH2_ROTATION_VECTOR, 10000)) {  // 10000 µs = 10ms
        Serial.println("[IMU] Failed to enable rotation vector report");
        return false;
    }

    // Linear acceleration at 100Hz — gravity-free acceleration
    if (!_bno.enableReport(SH2_LINEAR_ACCELERATION, 10000)) {
        Serial.println("[IMU] Failed to enable linear acceleration report");
        // Non-fatal — orientation is more important
    }

    return true;
}

void BNO085Module::update() {
    if (!_initialized) return;

    sh2_SensorValue_t event;

    // Drain all pending events (non-blocking)
    while (_bno.getSensorEvent(&event)) {
        switch (event.sensorId) {
            case SH2_ROTATION_VECTOR: {
                float qr = event.un.rotationVector.real;
                float qi = event.un.rotationVector.i;
                float qj = event.un.rotationVector.j;
                float qk = event.un.rotationVector.k;
                quaternionToEuler(qr, qi, qj, qk);
                break;
            }
            case SH2_LINEAR_ACCELERATION: {
                _linAccelX.set(event.un.linearAcceleration.x);
                _linAccelY.set(event.un.linearAcceleration.y);
                _linAccelZ.set(event.un.linearAcceleration.z);
                break;
            }
        }
    }

    // Periodically save Dynamic Calibration Data to BNO085 flash
    saveDcdIfDue();
}

void BNO085Module::saveDcdIfDue() {
    unsigned long now = millis();
    if (now - _lastDcdSaveMs < DCD_SAVE_INTERVAL_MS) return;
    _lastDcdSaveMs = now;

    if (sh2_saveDcdNow() == SH2_OK) {
        Serial.println("[IMU] DCD calibration saved to flash");
    } else {
        Serial.println("[IMU] DCD save failed");
    }
}

void BNO085Module::quaternionToEuler(float qr, float qi, float qj, float qk) {
    // Convert quaternion to Euler angles (Tait-Bryan: yaw/pitch/roll)
    // Using aerospace convention: Z-Y-X rotation order

    float sqr = qr * qr;
    float sqi = qi * qi;
    float sqj = qj * qj;
    float sqk = qk * qk;

    // Yaw (heading) — rotation around Z
    float yaw = atan2(2.0f * (qi * qj + qk * qr), sqi - sqj - sqk + sqr);

    // Pitch — rotation around Y
    float sinp = 2.0f * (qi * qk - qj * qr);
    float pitch;
    if (fabs(sinp) >= 1.0f) {
        pitch = copysign(M_PI / 2.0f, sinp);  // gimbal lock
    } else {
        pitch = asin(sinp);
    }

    // Roll — rotation around X
    float roll = atan2(2.0f * (qj * qk + qi * qr), -sqi - sqj + sqk + sqr);

    // Convert to degrees
    const float radToDeg = 180.0f / M_PI;
    _pitch.set(pitch * radToDeg);
    _roll.set(roll * radToDeg);

    // Heading: convert yaw to [0, 360)
    float headingDeg = yaw * radToDeg;
    if (headingDeg < 0.0f) headingDeg += 360.0f;
    _heading.set(headingDeg);
}

IMUData BNO085Module::getSnapshot() const {
    IMUData data;

    data.pitch   = _pitch.get();
    data.roll    = _roll.get();
    data.heading = _heading.get();
    data.hasOrientation = _pitch.isValid() || _roll.isValid() || _heading.isValid();

    data.linAccelX = _linAccelX.get();
    data.linAccelY = _linAccelY.get();
    data.linAccelZ = _linAccelZ.get();
    data.hasLinAccel = _linAccelX.isValid();

    return data;
}

String BNO085Module::getStatusString() const {
    if (!_initialized) return "Not detected";

    String s = "P:";
    s += _pitch.isValid() ? String(_pitch.value, 1) : "?";
    s += " R:";
    s += _roll.isValid() ? String(_roll.value, 1) : "?";
    s += " H:";
    s += _heading.isValid() ? String(_heading.value, 0) : "?";
    return s;
}

unsigned long BNO085Module::getOrientationAgeMs() const {
    if (!_initialized) return ULONG_MAX;
    return _pitch.ageMs();  // pitch/roll/heading update together from rotation vector
}

unsigned long BNO085Module::getAccelAgeMs() const {
    if (!_initialized) return ULONG_MAX;
    return _linAccelX.ageMs();  // X/Y/Z update together
}

#else
// Native test stub — BNO085 cannot compile on Mac
BNO085Module::BNO085Module() : _initialized(false) {}
bool BNO085Module::begin() { return false; }
void BNO085Module::update() {}
IMUData BNO085Module::getSnapshot() const {
    IMUData d;
    d.pitch = NAN; d.roll = NAN; d.heading = NAN;
    d.linAccelX = NAN; d.linAccelY = NAN; d.linAccelZ = NAN;
    d.hasOrientation = false; d.hasLinAccel = false;
    return d;
}
String BNO085Module::getStatusString() const { return "Native test stub"; }
unsigned long BNO085Module::getOrientationAgeMs() const { return ULONG_MAX; }
unsigned long BNO085Module::getAccelAgeMs() const { return ULONG_MAX; }
#endif
