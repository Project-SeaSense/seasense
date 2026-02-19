/**
 * Tests for GPS NaN guard in payload building
 *
 * Validates that NaN latitude/longitude are excluded from JSON payloads,
 * that (0,0) coordinates are excluded, and that valid coordinates are
 * included. Uses the same isnan() + zero check logic as buildPayload().
 *
 * We extract the GPS filtering predicate rather than compiling all of
 * APIUploader, which pulls in HTTPClient, WiFi, etc.
 */

#include "test_framework.h"
#include <cmath>

// ============================================================================
// Extract of GPS filtering logic (mirrors buildPayload exactly)
// ============================================================================

struct GPSFields {
    double latitude;
    double longitude;
    double altitude;
    double hdop;
};

// Returns true if GPS data should be included in payload
static bool gpsIsValid(const GPSFields& gps) {
    return !isnan(gps.latitude) && !isnan(gps.longitude)
        && (gps.latitude != 0.0 || gps.longitude != 0.0);
}

// ============================================================================
// Tests
// ============================================================================

// Test: NaN latitude and longitude are rejected
void test_nan_lat_lon_rejected() {
    GPSFields gps = { NAN, NAN, NAN, NAN };
    ASSERT_FALSE(gpsIsValid(gps));

    TEST_PASS();
}

// Test: NaN latitude only is rejected
void test_nan_lat_only_rejected() {
    GPSFields gps = { NAN, 15.5, 10.0, 1.2 };
    ASSERT_FALSE(gpsIsValid(gps));

    TEST_PASS();
}

// Test: NaN longitude only is rejected
void test_nan_lon_only_rejected() {
    GPSFields gps = { 45.0, NAN, 10.0, 1.2 };
    ASSERT_FALSE(gpsIsValid(gps));

    TEST_PASS();
}

// Test: (0, 0) coordinates are rejected (Null Island)
void test_zero_zero_rejected() {
    GPSFields gps = { 0.0, 0.0, 0.0, 1.0 };
    ASSERT_FALSE(gpsIsValid(gps));

    TEST_PASS();
}

// Test: valid coordinates are accepted
void test_valid_coords_accepted() {
    GPSFields gps = { 43.5081, 16.4402, 12.5, 0.9 };  // Split, Croatia
    ASSERT_TRUE(gpsIsValid(gps));

    TEST_PASS();
}

// Test: negative coordinates are accepted (Southern/Western hemisphere)
void test_negative_coords_accepted() {
    GPSFields gps = { -33.8688, 151.2093, 5.0, 1.1 };  // Sydney, Australia
    ASSERT_TRUE(gpsIsValid(gps));

    TEST_PASS();
}

// Test: (0, nonzero) is accepted — equator at non-zero longitude
void test_equator_nonzero_lon_accepted() {
    GPSFields gps = { 0.0, 29.5, 500.0, 1.5 };  // Equator, Uganda
    ASSERT_TRUE(gpsIsValid(gps));

    TEST_PASS();
}

// Test: (nonzero, 0) is accepted — prime meridian at non-zero latitude
void test_prime_meridian_nonzero_lat_accepted() {
    GPSFields gps = { 51.4769, 0.0, 10.0, 1.0 };  // Greenwich, London
    ASSERT_TRUE(gpsIsValid(gps));

    TEST_PASS();
}

// Test: very small but nonzero coords accepted (not confused with zero)
void test_tiny_coords_accepted() {
    GPSFields gps = { 0.0001, 0.0001, 0.0, 2.0 };
    ASSERT_TRUE(gpsIsValid(gps));

    TEST_PASS();
}

// Test: extreme valid coordinates accepted (poles)
void test_extreme_coords_accepted() {
    // North pole
    GPSFields north = { 90.0, 0.0, 0.0, 5.0 };
    ASSERT_TRUE(gpsIsValid(north));

    // South pole
    GPSFields south = { -90.0, 0.0, 0.0, 5.0 };
    ASSERT_TRUE(gpsIsValid(south));

    // Date line
    GPSFields dateline = { 0.0, 180.0, 0.0, 3.0 };
    ASSERT_TRUE(gpsIsValid(dateline));

    TEST_PASS();
}

// Test: NaN altitude does not affect GPS validity (only lat/lon matter)
void test_nan_altitude_still_valid() {
    GPSFields gps = { 43.5, 16.4, NAN, NAN };
    ASSERT_TRUE(gpsIsValid(gps));

    TEST_PASS();
}

int main() {
    TEST_SUITE("GPS NaN Guard");

    RUN_TEST(nan_lat_lon_rejected);
    RUN_TEST(nan_lat_only_rejected);
    RUN_TEST(nan_lon_only_rejected);
    RUN_TEST(zero_zero_rejected);
    RUN_TEST(valid_coords_accepted);
    RUN_TEST(negative_coords_accepted);
    RUN_TEST(equator_nonzero_lon_accepted);
    RUN_TEST(prime_meridian_nonzero_lat_accepted);
    RUN_TEST(tiny_coords_accepted);
    RUN_TEST(extreme_coords_accepted);
    RUN_TEST(nan_altitude_still_valid);

    TEST_SUMMARY();
}
