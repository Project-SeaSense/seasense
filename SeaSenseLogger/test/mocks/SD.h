#ifndef MOCK_SD_H
#define MOCK_SD_H

#include "FS.h"

class SDClass {
public:
    bool begin(uint8_t cs = 0) { (void)cs; return true; }
    void end() {}
    MockFile open(const char*, const char* = "r") { return MockFile(); }
    bool exists(const char*) { return false; }
    bool remove(const char*) { return true; }
    bool rename(const char*, const char*) { return true; }
    uint64_t totalBytes() { return 0; }
    uint64_t usedBytes() { return 0; }
};

inline SDClass SD;

#endif
