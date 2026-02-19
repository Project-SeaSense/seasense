#ifndef MOCK_ESP32_TARGZ_H
#define MOCK_ESP32_TARGZ_H

#include <cstdint>
#include <cstddef>
#include <cstdlib>

struct LZPacker {
    static size_t compress(uint8_t* /*src*/, size_t srcLen, uint8_t** dst) {
        // Mock: return 0 to indicate compression failed/no savings
        *dst = nullptr;
        (void)srcLen;
        return 0;
    }
};

#endif
