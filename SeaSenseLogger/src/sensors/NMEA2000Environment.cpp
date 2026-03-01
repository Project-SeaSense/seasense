/**
 * SeaSense Logger - NMEA2000 Environmental Data Listener Implementation
 *
 * Listens for environmental PGNs on the NMEA2000 bus and caches latest values.
 * Shares the tNMEA2000 instance with NMEA2000GPS to avoid duplicate CAN drivers.
 */

#include "NMEA2000Environment.h"
#include "../../config/hardware_config.h"
#include <initializer_list>

// ============================================================================
// Constructor
// ============================================================================

NMEA2000Environment::NMEA2000Environment()
    : _initialized(false)
{
}

// ============================================================================
// Initialization
// ============================================================================

void NMEA2000Environment::begin(tNMEA2000* n2k) {
    if (!n2k) {
        Serial.println("[N2K-ENV] Error: null tNMEA2000 instance");
        return;
    }

    // Extend the receive PGN list with environmental PGNs
    const unsigned long envPGNs[] = {
        130306L, 128259L, 128267L, 130310L, 130311L,
        130312L, 130313L, 129026L, 127250L, 127257L,
        0
    };
    n2k->ExtendReceiveMessages(envPGNs);

    _initialized = true;
    Serial.println("[N2K-ENV] Environmental PGN listener initialized");
}

// ============================================================================
// Message Handler (called from shared trampoline)
// ============================================================================

void NMEA2000Environment::handleMsg(const tN2kMsg& msg) {
    if (!_initialized) return;

    switch (msg.PGN) {
        case 130306: handlePGN130306(msg); break;
        case 128259: handlePGN128259(msg); break;
        case 128267: handlePGN128267(msg); break;
        case 130310: handlePGN130310(msg); break;
        case 130311: handlePGN130311(msg); break;
        case 130312: handlePGN130312(msg); break;
        case 130313: handlePGN130313(msg); break;
        case 129026: handlePGN129026(msg); break;
        case 127250: handlePGN127250(msg); break;
        case 127257: handlePGN127257(msg); break;
    }
}

// ============================================================================
// Snapshot
// ============================================================================

N2kEnvironmentData NMEA2000Environment::getSnapshot() const {
    N2kEnvironmentData data;

    data.windSpeedTrue     = _windSpeedTrue.get();
    data.windAngleTrue     = _windAngleTrue.get();
    data.windSpeedApparent = _windSpeedApparent.get();
    data.windAngleApparent = _windAngleApparent.get();

    data.waterDepth        = _waterDepth.get();
    data.depthOffset       = _depthOffset.get();
    data.speedThroughWater = _speedThroughWater.get();

    data.waterTempExternal = _waterTempExternal.get();
    data.airTemp           = _airTemp.get();

    data.baroPressure      = _baroPressure.get();
    data.humidity          = _humidity.get();

    data.cogTrue           = _cogTrue.get();
    data.sog               = _sog.get();
    data.heading           = _heading.get();

    data.pitch             = _pitch.get();
    data.roll              = _roll.get();
    data.yaw               = _yaw.get();

    // Set validity flags
    data.hasWind              = _windSpeedTrue.isValid() || _windSpeedApparent.isValid();
    data.hasDepth             = _waterDepth.isValid();
    data.hasSpeedThroughWater = _speedThroughWater.isValid();
    data.hasWaterTempExternal = _waterTempExternal.isValid();
    data.hasAirTemp           = _airTemp.isValid();
    data.hasBaroPressure      = _baroPressure.isValid();
    data.hasHumidity          = _humidity.isValid();
    data.hasCOGSOG            = _cogTrue.isValid() || _sog.isValid();
    data.hasHeading           = _heading.isValid();
    data.hasAttitude          = _pitch.isValid() || _roll.isValid();

    return data;
}

bool NMEA2000Environment::hasAnyData() const {
    return _windSpeedTrue.isValid() || _windSpeedApparent.isValid() ||
           _waterDepth.isValid() || _speedThroughWater.isValid() ||
           _waterTempExternal.isValid() || _airTemp.isValid() ||
           _baroPressure.isValid() || _humidity.isValid() ||
           _cogTrue.isValid() || _sog.isValid() ||
           _heading.isValid() || _pitch.isValid() || _roll.isValid();
}

String NMEA2000Environment::getStatusString() const {
    if (!_initialized) return "Not initialized";
    if (!hasAnyData()) return "No environmental data received";

    String status = "Receiving:";
    if (_windSpeedTrue.isValid() || _windSpeedApparent.isValid()) status += " Wind";
    if (_waterDepth.isValid()) status += " Depth";
    if (_speedThroughWater.isValid()) status += " STW";
    if (_waterTempExternal.isValid()) status += " WaterTemp";
    if (_airTemp.isValid()) status += " AirTemp";
    if (_baroPressure.isValid()) status += " Baro";
    if (_humidity.isValid()) status += " Humidity";
    if (_cogTrue.isValid() || _sog.isValid()) status += " COG/SOG";
    if (_heading.isValid()) status += " Heading";
    if (_pitch.isValid() || _roll.isValid()) status += " Attitude";
    return status;
}

// Helper: return the smallest age among the given fields (most recent update)
static unsigned long minAge(std::initializer_list<unsigned long> ages) {
    unsigned long best = ULONG_MAX;
    for (auto a : ages) if (a < best) best = a;
    return best;
}

unsigned long NMEA2000Environment::getWindAgeMs() const {
    return minAge({_windSpeedTrue.ageMs(), _windAngleTrue.ageMs(),
                   _windSpeedApparent.ageMs(), _windAngleApparent.ageMs()});
}

unsigned long NMEA2000Environment::getWaterAgeMs() const {
    return minAge({_waterDepth.ageMs(), _speedThroughWater.ageMs(),
                   _waterTempExternal.ageMs()});
}

unsigned long NMEA2000Environment::getAtmoAgeMs() const {
    return minAge({_airTemp.ageMs(), _baroPressure.ageMs(), _humidity.ageMs()});
}

unsigned long NMEA2000Environment::getNavAgeMs() const {
    return minAge({_cogTrue.ageMs(), _sog.ageMs(), _heading.ageMs()});
}

unsigned long NMEA2000Environment::getAttitudeAgeMs() const {
    return minAge({_pitch.ageMs(), _roll.ageMs()});
}

// ============================================================================
// PGN Handlers
// ============================================================================

void NMEA2000Environment::handlePGN130306(const tN2kMsg& msg) {
    // Wind Data
    unsigned char SID;
    double WindSpeed;
    double WindAngle;
    tN2kWindReference WindReference;

    if (!ParseN2kWindSpeed(msg, SID, WindSpeed, WindAngle, WindReference)) return;

    if (!N2kIsNA(WindSpeed) && !N2kIsNA(WindAngle)) {
        // Convert angle from radians to degrees
        float angleDeg = WindAngle * 180.0 / M_PI;

        if (WindReference == N2kWind_True_North ||
            WindReference == N2kWind_True_boat) {
            _windSpeedTrue.set(WindSpeed);
            _windAngleTrue.set(angleDeg);
        } else if (WindReference == N2kWind_Apparent) {
            _windSpeedApparent.set(WindSpeed);
            _windAngleApparent.set(angleDeg);
        }
    }

    DEBUG_NMEA2000_PRINTLN("[N2K-ENV] PGN 130306 Wind received");
}

void NMEA2000Environment::handlePGN128259(const tN2kMsg& msg) {
    // Speed Through Water
    unsigned char SID;
    double WaterReferenced;
    double GroundReferenced;
    tN2kSpeedWaterReferenceType SWRT;

    if (!ParseN2kBoatSpeed(msg, SID, WaterReferenced, GroundReferenced, SWRT)) return;

    if (!N2kIsNA(WaterReferenced)) {
        _speedThroughWater.set(WaterReferenced);
    }

    DEBUG_NMEA2000_PRINTLN("[N2K-ENV] PGN 128259 Speed received");
}

void NMEA2000Environment::handlePGN128267(const tN2kMsg& msg) {
    // Water Depth
    unsigned char SID;
    double DepthBelowTransducer;
    double Offset;
    double Range;

    if (!ParseN2kWaterDepth(msg, SID, DepthBelowTransducer, Offset, Range)) return;

    if (!N2kIsNA(DepthBelowTransducer)) {
        _waterDepth.set(DepthBelowTransducer);
        if (!N2kIsNA(Offset)) {
            _depthOffset.set(Offset);
        }
    }

    DEBUG_NMEA2000_PRINTLN("[N2K-ENV] PGN 128267 Depth received");
}

void NMEA2000Environment::handlePGN130310(const tN2kMsg& msg) {
    // Environmental Parameters (outside)
    unsigned char SID;
    double WaterTemperature;
    double OutsideAmbientAirTemperature;
    double AtmosphericPressure;

    if (!ParseN2kOutsideEnvironmentalParameters(msg, SID, WaterTemperature,
            OutsideAmbientAirTemperature, AtmosphericPressure)) return;

    // Temperature comes in Kelvin from NMEA2000, convert to Celsius
    if (!N2kIsNA(WaterTemperature)) {
        _waterTempExternal.set(WaterTemperature - 273.15);
    }
    if (!N2kIsNA(OutsideAmbientAirTemperature)) {
        _airTemp.set(OutsideAmbientAirTemperature - 273.15);
    }
    if (!N2kIsNA(AtmosphericPressure)) {
        _baroPressure.set(AtmosphericPressure);  // Already in Pascals
    }

    DEBUG_NMEA2000_PRINTLN("[N2K-ENV] PGN 130310 Outside Env received");
}

void NMEA2000Environment::handlePGN130311(const tN2kMsg& msg) {
    // Environmental Parameters (with humidity)
    unsigned char SID;
    tN2kTempSource TempSource;
    double Temperature;
    tN2kHumiditySource HumiditySource;
    double Humidity;
    double AtmosphericPressure;

    if (!ParseN2kEnvironmentalParameters(msg, SID, TempSource, Temperature,
            HumiditySource, Humidity, AtmosphericPressure)) return;

    // Temperature in Kelvin → Celsius
    if (!N2kIsNA(Temperature)) {
        if (TempSource == N2kts_OutsideTemperature) {
            _airTemp.set(Temperature - 273.15);
        } else if (TempSource == N2kts_SeaTemperature) {
            _waterTempExternal.set(Temperature - 273.15);
        }
    }

    if (!N2kIsNA(Humidity)) {
        _humidity.set(Humidity);
    }

    if (!N2kIsNA(AtmosphericPressure)) {
        _baroPressure.set(AtmosphericPressure);
    }

    DEBUG_NMEA2000_PRINTLN("[N2K-ENV] PGN 130311 Env Params received");
}

void NMEA2000Environment::handlePGN130312(const tN2kMsg& msg) {
    // Temperature (extended)
    unsigned char SID;
    unsigned char TempInstance;
    tN2kTempSource TempSource;
    double ActualTemperature;
    double SetTemperature;

    if (!ParseN2kTemperature(msg, SID, TempInstance, TempSource,
            ActualTemperature, SetTemperature)) return;

    if (!N2kIsNA(ActualTemperature)) {
        float tempC = ActualTemperature - 273.15;
        if (TempSource == N2kts_SeaTemperature) {
            _waterTempExternal.set(tempC);
        } else if (TempSource == N2kts_OutsideTemperature) {
            _airTemp.set(tempC);
        }
    }

    DEBUG_NMEA2000_PRINTLN("[N2K-ENV] PGN 130312 Temperature received");
}

void NMEA2000Environment::handlePGN130313(const tN2kMsg& msg) {
    // Humidity
    unsigned char SID;
    unsigned char HumidityInstance;
    tN2kHumiditySource HumiditySource;
    double ActualHumidity;
    double SetHumidity;

    if (!ParseN2kHumidity(msg, SID, HumidityInstance, HumiditySource,
            ActualHumidity, SetHumidity)) return;

    if (!N2kIsNA(ActualHumidity)) {
        _humidity.set(ActualHumidity);
    }

    DEBUG_NMEA2000_PRINTLN("[N2K-ENV] PGN 130313 Humidity received");
}

void NMEA2000Environment::handlePGN129026(const tN2kMsg& msg) {
    // COG & SOG
    unsigned char SID;
    tN2kHeadingReference HeadingReference;
    double COG;
    double SOG;

    if (!ParseN2kCOGSOGRapid(msg, SID, HeadingReference, COG, SOG)) return;

    if (!N2kIsNA(COG)) {
        // Convert from radians to degrees
        _cogTrue.set(COG * 180.0 / M_PI);
    }
    if (!N2kIsNA(SOG)) {
        _sog.set(SOG);
    }

    DEBUG_NMEA2000_PRINTLN("[N2K-ENV] PGN 129026 COG/SOG received");
}

void NMEA2000Environment::handlePGN127250(const tN2kMsg& msg) {
    // Vessel Heading
    unsigned char SID;
    double Heading;
    double Deviation;
    double Variation;
    tN2kHeadingReference HeadingReference;

    if (!ParseN2kHeading(msg, SID, Heading, Deviation, Variation, HeadingReference)) return;

    if (!N2kIsNA(Heading)) {
        float headingDeg = Heading * 180.0 / M_PI;
        // If magnetic heading and we have variation, convert to true
        if (HeadingReference == N2khr_magnetic && !N2kIsNA(Variation)) {
            headingDeg += Variation * 180.0 / M_PI;
            if (headingDeg < 0) headingDeg += 360.0;
            if (headingDeg >= 360.0) headingDeg -= 360.0;
        }
        _heading.set(headingDeg);
    }

    DEBUG_NMEA2000_PRINTLN("[N2K-ENV] PGN 127250 Heading received");
}

void NMEA2000Environment::handlePGN127257(const tN2kMsg& msg) {
    // Attitude (pitch, roll, yaw)
    unsigned char SID;
    double Yaw;
    double Pitch;
    double Roll;

    if (!ParseN2kAttitude(msg, SID, Yaw, Pitch, Roll)) return;

    // Convert from radians to degrees
    if (!N2kIsNA(Pitch)) {
        _pitch.set(Pitch * 180.0 / M_PI);
    }
    if (!N2kIsNA(Roll)) {
        _roll.set(Roll * 180.0 / M_PI);
    }
    if (!N2kIsNA(Yaw)) {
        _yaw.set(Yaw * 180.0 / M_PI);
    }

    DEBUG_NMEA2000_PRINTLN("[N2K-ENV] PGN 127257 Attitude received");
}

