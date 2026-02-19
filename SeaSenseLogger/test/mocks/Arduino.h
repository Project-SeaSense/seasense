#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <string>
#include <cstdio>
#include <type_traits>

// ============================================================================
// Arduino types
// ============================================================================

typedef bool boolean;
typedef uint8_t byte;

// ============================================================================
// Mock millis() — injectable from tests
// ============================================================================

inline unsigned long _mock_millis = 0;
inline unsigned long millis() { return _mock_millis; }
inline void delay(unsigned long) {}

// ============================================================================
// Arduino constrain / min / max
// ============================================================================

// constrain — handles mixed types (e.g. unsigned long vs uint32_t on macOS)
template<typename T, typename U, typename V>
auto constrain(T x, U lo, V hi) -> typename std::common_type<T,U,V>::type {
    using C = typename std::common_type<T,U,V>::type;
    C cx = static_cast<C>(x), clo = static_cast<C>(lo), chi = static_cast<C>(hi);
    return (cx < clo) ? clo : (cx > chi) ? chi : cx;
}

#ifndef min
template<typename T, typename U = T>
auto min(T a, U b) -> typename std::common_type<T,U>::type { return (a < b) ? a : b; }
#endif
#ifndef max
template<typename T, typename U = T>
auto max(T a, U b) -> typename std::common_type<T,U>::type { return (a > b) ? a : b; }
#endif

// ============================================================================
// Arduino String class (subset used by firmware)
// ============================================================================

class String {
public:
    String() : _str() {}
    String(const char* s) : _str(s ? s : "") {}
    String(const String& s) : _str(s._str) {}
    String(String&& s) noexcept : _str(std::move(s._str)) {}
    String(int val) : _str(std::to_string(val)) {}
    String(unsigned int val) : _str(std::to_string(val)) {}
    String(long val) : _str(std::to_string(val)) {}
    String(unsigned long val) : _str(std::to_string(val)) {}

    String(float val, int decimals = 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", decimals, (double)val);
        _str = buf;
    }

    String(double val, int decimals = 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", decimals, val);
        _str = buf;
    }

    String& operator=(const String& rhs) { _str = rhs._str; return *this; }
    String& operator=(const char* rhs) { _str = rhs ? rhs : ""; return *this; }
    String& operator=(String&& rhs) noexcept { _str = std::move(rhs._str); return *this; }

    String operator+(const String& rhs) const { return String((_str + rhs._str).c_str()); }
    String operator+(const char* rhs) const { return String((_str + (rhs ? rhs : "")).c_str()); }
    friend String operator+(const char* lhs, const String& rhs) {
        return String((std::string(lhs ? lhs : "") + rhs._str).c_str());
    }

    String& operator+=(const String& rhs) { _str += rhs._str; return *this; }
    String& operator+=(const char* rhs) { if (rhs) _str += rhs; return *this; }
    String& operator+=(char c) { _str += c; return *this; }

    bool operator==(const String& rhs) const { return _str == rhs._str; }
    bool operator==(const char* rhs) const { return _str == (rhs ? rhs : ""); }
    bool operator!=(const String& rhs) const { return _str != rhs._str; }
    bool operator!=(const char* rhs) const { return _str != (rhs ? rhs : ""); }

    char operator[](unsigned int idx) const { return _str[idx]; }

    unsigned int length() const { return (unsigned int)_str.length(); }
    bool isEmpty() const { return _str.empty(); }
    const char* c_str() const { return _str.c_str(); }

    String substring(unsigned int from, unsigned int to) const {
        if (from >= _str.length()) return String();
        if (to > _str.length()) to = _str.length();
        return String(_str.substr(from, to - from).c_str());
    }

    String substring(unsigned int from) const {
        return substring(from, _str.length());
    }

    int indexOf(char c, unsigned int from = 0) const {
        auto pos = _str.find(c, from);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }

    int indexOf(const String& s, unsigned int from = 0) const {
        auto pos = _str.find(s._str, from);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }

    void trim() {
        auto start = _str.find_first_not_of(" \t\r\n");
        auto end = _str.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) { _str.clear(); return; }
        _str = _str.substr(start, end - start + 1);
    }

    long toInt() const { return _str.empty() ? 0 : std::stol(_str); }
    float toFloat() const { return _str.empty() ? 0.0f : std::stof(_str); }
    double toDouble() const { return _str.empty() ? 0.0 : std::stod(_str); }

    bool startsWith(const String& prefix) const {
        return _str.rfind(prefix._str, 0) == 0;
    }

    bool endsWith(const String& suffix) const {
        if (suffix._str.size() > _str.size()) return false;
        return _str.compare(_str.size() - suffix._str.size(), suffix._str.size(), suffix._str) == 0;
    }

    void replace(const String& from, const String& to) {
        size_t pos = 0;
        while ((pos = _str.find(from._str, pos)) != std::string::npos) {
            _str.replace(pos, from._str.length(), to._str);
            pos += to._str.length();
        }
    }

    void toLowerCase() {
        for (auto& c : _str) c = tolower(c);
    }

    void toUpperCase() {
        for (auto& c : _str) c = toupper(c);
    }

    // ArduinoJson compatibility — serializeJson(doc, String&) uses these
    size_t write(uint8_t c) { _str += (char)c; return 1; }
    size_t write(const uint8_t* buf, size_t len) {
        _str.append(reinterpret_cast<const char*>(buf), len);
        return len;
    }

private:
    std::string _str;

    friend struct MockSerial;
};

// Allow implicit conversion from std::string for convenience
inline bool operator==(const char* lhs, const String& rhs) { return rhs == lhs; }

// ============================================================================
// Mock Serial
// ============================================================================

struct MockSerial {
    void print(const char*) {}
    void print(const String&) {}
    void print(int) {}
    void print(unsigned int) {}
    void print(long) {}
    void print(unsigned long) {}
    void print(float) {}
    void print(double) {}
    void println(const char*) {}
    void println(const String&) {}
    void println(int) {}
    void println(unsigned int) {}
    void println(long) {}
    void println(unsigned long) {}
    void println(float) {}
    void println(double) {}
    void println() {}
    void begin(unsigned long) {}
};

inline MockSerial Serial;

// ============================================================================
// ESP mock (for ESP.getFreeHeap etc. — used by APIUploader)
// ============================================================================

struct MockESP {
    uint32_t getFreeHeap() const { return 200000; }
    uint32_t getMinFreeHeap() const { return 150000; }
    void restart() {}
};

inline MockESP ESP;

// ============================================================================
// ESP32 time sync stub
// ============================================================================

inline void configTime(long gmtOffset, int daylightOffset, const char* server) {
    (void)gmtOffset; (void)daylightOffset; (void)server;
}

#endif // MOCK_ARDUINO_H
