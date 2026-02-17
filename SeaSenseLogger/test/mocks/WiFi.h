#ifndef MOCK_WIFI_H
#define MOCK_WIFI_H

#include "Arduino.h"

#define WL_CONNECTED 3

class MockWiFi {
public:
    int status() { return WL_CONNECTED; }
    int32_t RSSI() { return -65; }
};

inline MockWiFi WiFi;

class IPAddress {
public:
    IPAddress() : _addr{0,0,0,0} {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : _addr{a,b,c,d} {}
    String toString() const {
        return String((int)_addr[0]) + "." + String((int)_addr[1]) + "." +
               String((int)_addr[2]) + "." + String((int)_addr[3]);
    }
private:
    uint8_t _addr[4];
};

#endif
