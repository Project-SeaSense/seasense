/**
 * SeaSense Logger - Wind Correction for Hull Tilt
 */

#include "WindCorrection.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool correctWindForTilt(float windSpeed, float windAngle,
                        float pitchDeg, float rollDeg,
                        float& corrSpeed, float& corrAngle) {
    // Guard: any NaN input → fail with NaN output
    if (std::isnan(windSpeed) || std::isnan(windAngle) ||
        std::isnan(pitchDeg) || std::isnan(rollDeg)) {
        corrSpeed = NAN;
        corrAngle = NAN;
        return false;
    }

    const float degToRad = (float)(M_PI / 180.0);
    const float radToDeg = (float)(180.0 / M_PI);

    float angleRad = windAngle * degToRad;
    float pitchRad = pitchDeg * degToRad;
    float rollRad  = rollDeg  * degToRad;

    // Decompose wind into athwartships (Vx) and fore-aft (Vy),
    // then project each through the relevant tilt axis.
    float Vx = windSpeed * std::sin(angleRad) * std::cos(rollRad);
    float Vy = windSpeed * std::cos(angleRad) * std::cos(pitchRad);

    corrSpeed = std::sqrt(Vx * Vx + Vy * Vy);

    // atan2 → [-180, 180], normalize to [0, 360)
    float rawAngle = std::atan2(Vx, Vy) * radToDeg;
    if (rawAngle < 0.0f) rawAngle += 360.0f;
    corrAngle = rawAngle;

    return true;
}
