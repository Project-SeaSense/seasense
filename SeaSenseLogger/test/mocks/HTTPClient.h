#ifndef MOCK_HTTPCLIENT_H
#define MOCK_HTTPCLIENT_H

#include "Arduino.h"

class HTTPClient {
public:
    void begin(const String&) {}
    void addHeader(const String&, const String&) {}
    void setConnectTimeout(int) {}
    void setTimeout(int) {}
    int POST(const String&) { return 201; }
    String getString() { return String("{\"ok\":true}"); }
    String errorToString(int) { return String("mock error"); }
    void end() {}
};

#endif
