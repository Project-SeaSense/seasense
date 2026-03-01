#ifndef MOCK_UPDATE_H
#define MOCK_UPDATE_H

#include <cstdint>
#include <cstddef>

// Mock ESP32 Update library for native tests
class MockUpdate {
public:
    bool begin(size_t size) { _size = size; _written = 0; _begun = true; return _mockBeginResult; }
    size_t write(uint8_t* data, size_t len) { (void)data; _written += len; return len; }
    bool end(bool evenIfRemaining = false) { (void)evenIfRemaining; return _mockEndResult; }
    void abort() { _begun = false; }
    bool hasError() const { return false; }

    // Test control
    bool _mockBeginResult = true;
    bool _mockEndResult = true;
    bool _begun = false;
    size_t _size = 0;
    size_t _written = 0;
};

inline MockUpdate Update;

#endif // MOCK_UPDATE_H
