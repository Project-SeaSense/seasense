#ifndef MOCK_WIRE_H
#define MOCK_WIRE_H

#include "Arduino.h"

class TwoWire {
public:
    void begin(int sda = -1, int scl = -1) { (void)sda; (void)scl; }
    void beginTransmission(uint8_t) {}
    uint8_t endTransmission(bool stop = true) { (void)stop; return 0; }
    size_t write(uint8_t) { return 1; }
    size_t write(const uint8_t*, size_t len) { return len; }
    uint8_t requestFrom(uint8_t, uint8_t) { return 0; }
    int available() { return 0; }
    int read() { return 0; }
    void setClock(uint32_t) {}
    void setTimeOut(uint16_t) {}
};

inline TwoWire Wire;

#endif
