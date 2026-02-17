#ifndef MOCK_SPIFFS_H
#define MOCK_SPIFFS_H

#include "FS.h"

class SPIFFSClass {
public:
    bool begin(bool formatOnFail = false) { (void)formatOnFail; return true; }
    void end() {}
    bool format() { return true; }
    MockFile open(const char*, const char* = "r") { return MockFile(); }
    bool exists(const char*) { return false; }
    bool remove(const char*) { return true; }
    bool rename(const char*, const char*) { return true; }
    size_t totalBytes() { return 1500000; }
    size_t usedBytes() { return 0; }
};

inline SPIFFSClass SPIFFS;

#endif
