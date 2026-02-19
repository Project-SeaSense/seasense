#ifndef MOCK_FS_H
#define MOCK_FS_H

#include "Arduino.h"

#define FILE_READ   "r"
#define FILE_WRITE  "w"
#define FILE_APPEND "a"

class MockFile {
public:
    bool _valid = true;
    operator bool() const { return _valid; }
    bool available() { return false; }
    String readStringUntil(char) { return String(); }
    size_t println(const String&) { return 1; }
    size_t println(const char*) { return 1; }
    size_t print(const String&) { return 1; }
    // Required by ArduinoJson
    int read() { return -1; }
    size_t write(uint8_t) { return 1; }
    size_t write(const uint8_t* buf, size_t len) { (void)buf; return len; }
    size_t readBytes(char*, size_t) { return 0; }
    int peek() { return -1; }
    void flush() {}
    void close() {}
};

typedef MockFile File;

#endif
