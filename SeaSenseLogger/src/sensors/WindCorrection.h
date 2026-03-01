/**
 * SeaSense Logger - Wind Correction for Hull Tilt
 *
 * Corrects apparent wind speed/angle for pitch and roll.
 * Pure math, no hardware dependencies — fully testable on native.
 */

#ifndef WIND_CORRECTION_H
#define WIND_CORRECTION_H

/**
 * Correct apparent wind for hull tilt (pitch/roll).
 *
 * Projects the horizontal wind vector through pitch and roll rotations
 * to recover the true horizontal components.
 *
 * @param windSpeed     Apparent wind speed (m/s)
 * @param windAngle     Apparent wind angle (degrees, 0=bow)
 * @param pitchDeg      Pitch in degrees (positive = bow up)
 * @param rollDeg       Roll in degrees (positive = starboard down)
 * @param corrSpeed     [out] Corrected wind speed (m/s), NaN on failure
 * @param corrAngle     [out] Corrected wind angle (degrees, 0-360), NaN on failure
 * @return true if correction succeeded, false if any input is NaN
 */
bool correctWindForTilt(float windSpeed, float windAngle,
                        float pitchDeg, float rollDeg,
                        float& corrSpeed, float& corrAngle);

#endif // WIND_CORRECTION_H
