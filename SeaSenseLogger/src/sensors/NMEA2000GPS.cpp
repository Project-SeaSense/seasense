/**
 * SeaSense Logger - NMEA2000 GPS Source Implementation
 *
 * Custom ESP32-S3 TWAI hardware driver embedded here - no NMEA2000_esp32 library
 * needed. Uses driver/twai.h which is compatible with ESP-IDF 5.x / SDK 3.x.
 */

#include "NMEA2000GPS.h"
#include "../../config/hardware_config.h"
#include "driver/twai.h"

// ============================================================================
// Custom TWAI hardware driver for tNMEA2000
// Replaces the incompatible NMEA2000_esp32 library for ESP-IDF 5.x / SDK 3.x
// ============================================================================

class tNMEA2000_ESP32TWAI : public tNMEA2000 {
public:
    tNMEA2000_ESP32TWAI(int txPin, int rxPin)
        : _txPin(txPin), _rxPin(rxPin), _open(false) {}

    ~tNMEA2000_ESP32TWAI() {
        if (_open) {
            twai_stop();
            twai_driver_uninstall();
        }
    }

protected:
    bool CANOpen() override {
        twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
            (gpio_num_t)_txPin, (gpio_num_t)_rxPin, TWAI_MODE_LISTEN_ONLY);
        g_config.rx_queue_len = 32;
        g_config.tx_queue_len = 0;

        twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
        twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
            return false;
        }
        if (twai_start() != ESP_OK) {
            twai_driver_uninstall();
            return false;
        }
        _open = true;
        return true;
    }

    bool CANSendFrame(unsigned long id, unsigned char len,
                      const unsigned char *buf, bool wait_sent) override {
        (void)id; (void)len; (void)buf; (void)wait_sent;
        return false;  // listen-only: no transmit
    }

    bool CANGetFrame(unsigned long &id, unsigned char &len,
                     unsigned char *buf) override {
        twai_message_t msg;
        if (twai_receive(&msg, 0) != ESP_OK) return false;
        if (!msg.extd) return false;  // NMEA2000 uses 29-bit extended IDs
        id = msg.identifier;
        len = msg.data_length_code;
        memcpy(buf, msg.data, len);
        return true;
    }

private:
    int _txPin;
    int _rxPin;
    bool _open;
};

// ============================================================================
// Static instance pointer for message handler trampoline
// ============================================================================

NMEA2000GPS* NMEA2000GPS::_instance = nullptr;

// ============================================================================
// Constructor / Destructor
// ============================================================================

NMEA2000GPS::NMEA2000GPS()
    : _n2k(nullptr),
      _initialized(false),
      _lastUpdateMs(0),
      _hasPosition(false),
      _hasTime(false),
      _forwardCallback(nullptr)
{
    memset(&_data, 0, sizeof(_data));
    _data.valid = false;
    _data.hdop = 99.9;
}

NMEA2000GPS::~NMEA2000GPS() {
    delete _n2k;
}

// ============================================================================
// Initialization
// ============================================================================

bool NMEA2000GPS::begin() {
    _instance = this;

    _n2k = new tNMEA2000_ESP32TWAI(CAN_TX_PIN, CAN_RX_PIN);

    _n2k->SetProductInformation(
        "SEASENSE-001",    // Model serial code
        1,                  // Product code
        "SeaSense Logger", // Model ID
        "2.0.0",           // Software version
        "1.0"              // Hardware version
    );

    _n2k->SetDeviceInformation(
        1,     // Unique number
        145,   // Device function: Environmental sensor
        60,    // Device class: Sensor/Communication
        2040   // Manufacturer code
    );

    _n2k->SetMode(tNMEA2000::N2km_ListenOnly);
    _n2k->EnableForward(false);

    const unsigned long rxPGNs[] = {129029L, 126992L, 129025L, 0};
    _n2k->ExtendReceiveMessages(rxPGNs);

    _n2k->SetMsgHandler(staticMsgHandler);

    if (!_n2k->Open()) {
        Serial.println("[N2K] Failed to open CAN bus (check wiring on CAN_TX/RX pins)");
        delete _n2k;
        _n2k = nullptr;
        return false;
    }

    _initialized = true;
    Serial.println("[N2K] CAN bus opened in listen-only mode (250kbps)");
    return true;
}

// ============================================================================
// Update (call in loop)
// ============================================================================

void NMEA2000GPS::update() {
    if (!_initialized || !_n2k) return;
    _n2k->ParseMessages();

    if (_data.valid && (millis() - _lastUpdateMs) > N2K_GPS_STALE_MS) {
        _data.valid = false;
    }
}

// ============================================================================
// Public Accessors
// ============================================================================

bool NMEA2000GPS::hasValidFix() const {
    return _data.valid;
}

GPSData NMEA2000GPS::getData() const {
    return _data;
}

String NMEA2000GPS::getTimeUTC() const {
    if (!_data.valid || _data.epoch == 0) return "";

    char buf[25];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             _data.year, _data.month, _data.day,
             _data.hour, _data.minute, _data.second);
    return String(buf);
}

String NMEA2000GPS::getStatusString() {
    if (!_initialized) return "Not initialized";
    if (!_data.valid) {
        if (_lastUpdateMs == 0) return "Waiting for NMEA2000 GPS data...";
        return "Data stale (" + String((millis() - _lastUpdateMs) / 1000) + "s ago)";
    }
    return "Fix OK (" + String(_data.satellites) + " sats, HDOP " +
           String(_data.hdop, 1) + ")";
}

unsigned long NMEA2000GPS::getAgeMs() const {
    if (_lastUpdateMs == 0) return ULONG_MAX;
    return millis() - _lastUpdateMs;
}

// ============================================================================
// Message Handler
// ============================================================================

void NMEA2000GPS::staticMsgHandler(const tN2kMsg& msg) {
    if (_instance) _instance->handleMsg(msg);
}

void NMEA2000GPS::handleMsg(const tN2kMsg& msg) {
    switch (msg.PGN) {
        case 129029: handlePGN129029(msg); break;
        case 126992: handlePGN126992(msg); break;
        case 129025: handlePGN129025(msg); break;
    }

    // Forward to additional listeners (e.g., NMEA2000Environment)
    if (_forwardCallback) {
        _forwardCallback(msg);
    }
}

// ============================================================================
// PGN Handlers
// ============================================================================

void NMEA2000GPS::handlePGN129029(const tN2kMsg& msg) {
    unsigned char SID;
    uint16_t DaysSince1970;
    double SecondsSinceMidnight;
    double Latitude, Longitude, Altitude;
    tN2kGNSStype GNSStype;
    tN2kGNSSmethod GNSSmethod;
    unsigned char nSatellites;
    double HDOP, PDOP, GeoidalSeparation;
    unsigned char nReferenceStations;
    tN2kGNSStype ReferenceStationType;
    uint16_t ReferenceSationID;
    double AgeOfCorrection;

    if (!ParseN2kGNSS(msg, SID, DaysSince1970, SecondsSinceMidnight,
                      Latitude, Longitude, Altitude,
                      GNSStype, GNSSmethod, nSatellites,
                      HDOP, PDOP, GeoidalSeparation,
                      nReferenceStations, ReferenceStationType,
                      ReferenceSationID, AgeOfCorrection)) {
        return;
    }

    if (GNSSmethod == N2kGNSSm_noGNSS || N2kIsNA(Latitude) || N2kIsNA(Longitude)) {
        return;
    }

    time_t epoch = (time_t)DaysSince1970 * 86400;
    if (!N2kIsNA(SecondsSinceMidnight)) {
        epoch += (time_t)SecondsSinceMidnight;
    }
    struct tm* t = gmtime(&epoch);

    _data.year      = t->tm_year + 1900;
    _data.month     = t->tm_mon + 1;
    _data.day       = t->tm_mday;
    _data.hour      = t->tm_hour;
    _data.minute    = t->tm_min;
    _data.second    = t->tm_sec;
    _data.epoch     = epoch;
    _data.latitude  = Latitude;
    _data.longitude = Longitude;
    _data.altitude  = N2kIsNA(Altitude) ? 0.0 : Altitude;
    _data.satellites = nSatellites;
    _data.hdop      = N2kIsNA(HDOP) ? 99.9 : HDOP;
    _data.valid     = true;

    _hasPosition = true;
    _hasTime = true;
    _lastUpdateMs = millis();
}

void NMEA2000GPS::handlePGN126992(const tN2kMsg& msg) {
    if (_hasTime) return;

    unsigned char SID;
    uint16_t SystemDate;
    double SystemTime;
    tN2kTimeSource TimeSource;

    if (!ParseN2kSystemTime(msg, SID, SystemDate, SystemTime, TimeSource)) return;
    if (N2kIsNA(SystemDate) || N2kIsNA(SystemTime)) return;

    time_t epoch = (time_t)SystemDate * 86400 + (time_t)SystemTime;
    struct tm* t = gmtime(&epoch);

    _data.year   = t->tm_year + 1900;
    _data.month  = t->tm_mon + 1;
    _data.day    = t->tm_mday;
    _data.hour   = t->tm_hour;
    _data.minute = t->tm_min;
    _data.second = t->tm_sec;
    _data.epoch  = epoch;
    _hasTime = true;

    if (_hasPosition) {
        _data.valid = true;
        _lastUpdateMs = millis();
    }
}

void NMEA2000GPS::handlePGN129025(const tN2kMsg& msg) {
    if (_hasPosition) return;

    double Latitude, Longitude;
    if (!ParseN2kPositionRapid(msg, Latitude, Longitude)) return;
    if (N2kIsNA(Latitude) || N2kIsNA(Longitude)) return;

    _data.latitude   = Latitude;
    _data.longitude  = Longitude;
    _data.altitude   = 0.0;
    _data.satellites = 0;
    _data.hdop       = 99.9;
    _hasPosition = true;

    if (_hasTime) {
        _data.valid = true;
        _lastUpdateMs = millis();
    }
}
