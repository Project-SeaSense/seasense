/**
 * Tests for WindCorrection — tilt-corrected apparent wind
 *
 * Pure math tests, no hardware dependencies.
 */

#include "test_framework.h"
#include "../src/sensors/WindCorrection.h"

// Test: zero pitch/roll → output equals input (passthrough)
void test_no_tilt_passthrough() {
    float corrSpeed, corrAngle;
    bool ok = correctWindForTilt(10.0f, 90.0f, 0.0f, 0.0f, corrSpeed, corrAngle);

    ASSERT_TRUE(ok);
    ASSERT_FLOAT_EQ(10.0, corrSpeed, 0.01);
    ASSERT_FLOAT_EQ(90.0, corrAngle, 0.1);

    TEST_PASS();
}

// Test: NaN input returns false + NaN output
void test_nan_input_returns_false() {
    float corrSpeed, corrAngle;

    ASSERT_FALSE(correctWindForTilt(NAN, 90.0f, 0.0f, 0.0f, corrSpeed, corrAngle));
    ASSERT_NAN(corrSpeed);
    ASSERT_NAN(corrAngle);

    ASSERT_FALSE(correctWindForTilt(10.0f, NAN, 0.0f, 0.0f, corrSpeed, corrAngle));
    ASSERT_FALSE(correctWindForTilt(10.0f, 90.0f, NAN, 0.0f, corrSpeed, corrAngle));
    ASSERT_FALSE(correctWindForTilt(10.0f, 90.0f, 0.0f, NAN, corrSpeed, corrAngle));

    TEST_PASS();
}

// Test: 30° heel (roll), beam wind (90°) → speed reduced by cos(30°) ≈ 0.866
void test_roll_reduces_speed() {
    float corrSpeed, corrAngle;
    bool ok = correctWindForTilt(10.0f, 90.0f, 0.0f, 30.0f, corrSpeed, corrAngle);

    ASSERT_TRUE(ok);
    // Vx = 10·sin(90°)·cos(30°) = 10·1·0.866 = 8.66
    // Vy = 10·cos(90°)·cos(0°) = 10·0·1 = 0
    // corrSpeed = √(8.66² + 0²) = 8.66
    ASSERT_FLOAT_EQ(8.66, corrSpeed, 0.02);
    ASSERT_FLOAT_EQ(90.0, corrAngle, 0.1);

    TEST_PASS();
}

// Test: headwind (0°) with 10° pitch → speed reduced by cos(10°) ≈ 0.985
void test_headwind_pitch_correction() {
    float corrSpeed, corrAngle;
    bool ok = correctWindForTilt(10.0f, 0.0f, 10.0f, 0.0f, corrSpeed, corrAngle);

    ASSERT_TRUE(ok);
    // Vx = 10·sin(0°)·cos(0°) = 0
    // Vy = 10·cos(0°)·cos(10°) = 10·1·0.985 = 9.85
    // corrSpeed = √(0² + 9.85²) = 9.85
    ASSERT_FLOAT_EQ(9.85, corrSpeed, 0.02);
    // angle: atan2(0, 9.85) = 0°
    ASSERT_FLOAT_EQ(0.0, corrAngle, 0.1);

    TEST_PASS();
}

// Test: output angle is normalized to [0, 360)
void test_angle_normalization() {
    float corrSpeed, corrAngle;

    // Wind from stern (180°), no tilt
    bool ok = correctWindForTilt(10.0f, 180.0f, 0.0f, 0.0f, corrSpeed, corrAngle);
    ASSERT_TRUE(ok);
    ASSERT_FLOAT_EQ(180.0, corrAngle, 0.1);

    // Wind from port quarter (270°), no tilt
    ok = correctWindForTilt(10.0f, 270.0f, 0.0f, 0.0f, corrSpeed, corrAngle);
    ASSERT_TRUE(ok);
    ASSERT_FLOAT_EQ(270.0, corrAngle, 0.1);

    TEST_PASS();
}

int main() {
    TEST_SUITE("Wind Correction (tilt)");

    RUN_TEST(no_tilt_passthrough);
    RUN_TEST(nan_input_returns_false);
    RUN_TEST(roll_reduces_speed);
    RUN_TEST(headwind_pitch_correction);
    RUN_TEST(angle_normalization);

    TEST_SUMMARY();
}
