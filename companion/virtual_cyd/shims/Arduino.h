#pragma once

#define ARDUINO 10819
#define ARDUINOJSON_ENABLE_PROGMEM 0

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>

#include "../virtual_hw.h"

class __FlashStringHelper;

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

inline uint8_t pgm_read_byte(const void* addr) {
    return *reinterpret_cast<const uint8_t*>(addr);
}
inline uint16_t pgm_read_word(const void* addr) {
    return *reinterpret_cast<const uint16_t*>(addr);
}
inline uint32_t pgm_read_dword(const void* addr) {
    return *reinterpret_cast<const uint32_t*>(addr);
}

#define F(str_lit) (str_lit)

#if defined(_MSC_VER)
inline size_t strlcpy(char* dst, const char* src, size_t size) {
    if (!src) src = "";
    size_t srclen = std::strlen(src);
    if (size > 0 && dst) {
        size_t n = (srclen >= size) ? (size - 1) : srclen;
        std::memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return srclen;
}
#endif

template <typename T, typename U>
constexpr auto min(T a, U b) -> typename std::common_type<T, U>::type {
    using R = typename std::common_type<T, U>::type;
    return (static_cast<R>(a) < static_cast<R>(b)) ? static_cast<R>(a) : static_cast<R>(b);
}

template <typename T, typename U>
constexpr auto max(T a, U b) -> typename std::common_type<T, U>::type {
    using R = typename std::common_type<T, U>::type;
    return (static_cast<R>(a) > static_cast<R>(b)) ? static_cast<R>(a) : static_cast<R>(b);
}

template <typename T, typename A, typename B>
constexpr T constrain(T x, A lo, B hi) {
    return (x < static_cast<T>(lo)) ? static_cast<T>(lo) : ((x > static_cast<T>(hi)) ? static_cast<T>(hi) : x);
}

inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    const long run = in_max - in_min;
    if (run == 0) return out_min;
    const long rise = out_max - out_min;
    const long delta = x - in_min;
    return (delta * rise) / run + out_min;
}

class String {
public:
    String() = default;
    String(const char* s) : _s(s ? s : "") {}
    String(const std::string& s) : _s(s) {}
    String(char c) : _s(1, c) {}
    String(int v) : _s(std::to_string(v)) {}
    String(unsigned int v) : _s(std::to_string(v)) {}
    String(long v) : _s(std::to_string(v)) {}
    String(unsigned long v) : _s(std::to_string(v)) {}
    String(float v, unsigned int digits = 2) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.*f", static_cast<int>(digits), static_cast<double>(v));
        _s = buf;
    }

    const char* c_str() const { return _s.c_str(); }
    size_t length() const { return _s.size(); }
    bool isEmpty() const { return _s.empty(); }
    void reserve(size_t n) { _s.reserve(n); }
    void clear() { _s.clear(); }

    char charAt(size_t idx) const { return idx < _s.size() ? _s[idx] : '\0'; }
    char operator[](size_t idx) const { return charAt(idx); }

    bool concat(const char* s) {
        if (s) _s.append(s);
        return true;
    }
    bool concat(const char* s, size_t len) {
        if (s && len) _s.append(s, len);
        return true;
    }
    bool concat(char c) {
        _s.push_back(c);
        return true;
    }
    bool concat(const String& o) {
        _s.append(o._s);
        return true;
    }
    bool concat(int v) {
        _s.append(std::to_string(v));
        return true;
    }

    void trim() {
        size_t a = 0;
        while (a < _s.size() && static_cast<unsigned char>(_s[a]) <= ' ') a++;
        size_t b = _s.size();
        while (b > a && static_cast<unsigned char>(_s[b - 1]) <= ' ') b--;
        _s = _s.substr(a, b - a);
    }

    int indexOf(char c, size_t from = 0) const {
        size_t p = _s.find(c, from);
        return p == std::string::npos ? -1 : static_cast<int>(p);
    }
    int indexOf(const char* sub, size_t from = 0) const {
        if (!sub) return -1;
        size_t p = _s.find(sub, from);
        return p == std::string::npos ? -1 : static_cast<int>(p);
    }
    String substring(size_t begin, size_t end = std::string::npos) const {
        if (begin >= _s.size()) return String("");
        if (end == std::string::npos || end > _s.size()) return String(_s.substr(begin));
        if (end <= begin) return String("");
        return String(_s.substr(begin, end - begin));
    }
    bool startsWith(const char* pfx) const {
        if (!pfx) return false;
        size_t n = std::strlen(pfx);
        return _s.size() >= n && _s.compare(0, n, pfx) == 0;
    }
    bool equalsIgnoreCase(const char* other) const {
        if (!other) return false;
        size_t n = std::strlen(other);
        if (n != _s.size()) return false;
        for (size_t i = 0; i < n; ++i) {
            char a = _s[i];
            char b = other[i];
            if (a >= 'A' && a <= 'Z') a += ('a' - 'A');
            if (b >= 'A' && b <= 'Z') b += ('a' - 'A');
            if (a != b) return false;
        }
        return true;
    }
    bool equalsIgnoreCase(const String& o) const { return equalsIgnoreCase(o.c_str()); }

    int toInt() const { return std::atoi(_s.c_str()); }
    float toFloat() const { return static_cast<float>(std::atof(_s.c_str())); }

    String& operator+=(const String& rhs) { _s += rhs._s; return *this; }
    String& operator+=(const char* rhs) { if (rhs) _s += rhs; return *this; }
    String& operator+=(char c) { _s.push_back(c); return *this; }

    friend String operator+(const String& a, const String& b) { return String(a._s + b._s); }
    friend String operator+(const String& a, const char* b) { return String(a._s + (b ? b : "")); }
    friend String operator+(const char* a, const String& b) { return String(std::string(a ? a : "") + b._s); }
    friend String operator+(const String& a, int b) { return String(a._s + std::to_string(b)); }
    friend bool operator==(const String& a, const String& b) { return a._s == b._s; }
    friend bool operator==(const String& a, const char* b) { return a._s == (b ? b : ""); }
    friend bool operator!=(const String& a, const char* b) { return !(a == b); }

private:
    std::string _s;
};

class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t b) = 0;
    virtual size_t write(const uint8_t* buf, size_t size) {
        size_t n = 0;
        for (size_t i = 0; i < size; ++i) n += write(buf[i]);
        return n;
    }
    size_t write(const char* str) {
        if (!str) return 0;
        return write(reinterpret_cast<const uint8_t*>(str), std::strlen(str));
    }
    size_t print(const char* s) { return write(s); }
    size_t print(const String& s) { return write(reinterpret_cast<const uint8_t*>(s.c_str()), s.length()); }
    size_t print(int v) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%d", v);
        return write(buf);
    }
    size_t println(const char* s) {
        size_t n = print(s);
        n += write(reinterpret_cast<const uint8_t*>("\r\n"), 2);
        return n;
    }
    size_t println(const String& s) {
        size_t n = print(s);
        n += write(reinterpret_cast<const uint8_t*>("\r\n"), 2);
        return n;
    }
    size_t println() { return write(reinterpret_cast<const uint8_t*>("\r\n"), 2); }
    int printf(const char* fmt, ...) {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        int r = std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (r > 0) write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(min<int>(r, static_cast<int>(sizeof(buf) - 1))));
        return r;
    }
};

class Printable {
public:
    virtual ~Printable() = default;
    virtual size_t printTo(Print& p) const = 0;
};

class Stream : public Print {
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
    virtual size_t readBytes(char* buf, size_t len) {
        size_t n = 0;
        while (n < len) {
            int c = read();
            if (c < 0) break;
            buf[n++] = static_cast<char>(c);
        }
        return n;
    }
    size_t readBytes(uint8_t* buf, size_t len) {
        return readBytes(reinterpret_cast<char*>(buf), len);
    }
};

class HardwareSerial : public Stream {
public:
    void begin(unsigned long baud) { (void)baud; }
    void setRxBufferSize(size_t sz) { (void)sz; }
    int available() override { return vcyd::VirtualHw::instance().serialAvailable(); }
    int read() override { return vcyd::VirtualHw::instance().serialRead(); }
    int peek() override { return vcyd::VirtualHw::instance().serialPeek(); }
    size_t write(uint8_t b) override {
        vcyd::VirtualHw::instance().serialTxWrite(&b, 1);
        return 1;
    }
    size_t write(const uint8_t* buf, size_t size) override {
        vcyd::VirtualHw::instance().serialTxWrite(buf, size);
        return size;
    }
};

inline HardwareSerial Serial;

inline uint32_t millis() { return vcyd::VirtualHw::instance().millis(); }
inline uint32_t micros() { return vcyd::VirtualHw::instance().micros(); }
inline void delay(uint32_t ms) { vcyd::VirtualHw::instance().delayMs(ms); }
inline void delayMicroseconds(uint32_t us) { vcyd::VirtualHw::instance().delayUs(us); }
inline void pinMode(uint8_t pin, uint8_t mode) { vcyd::VirtualHw::instance().pinMode(pin, mode); }
inline void digitalWrite(uint8_t pin, uint8_t val) { vcyd::VirtualHw::instance().digitalWrite(pin, val); }
inline int digitalRead(uint8_t pin) { return vcyd::VirtualHw::instance().digitalRead(pin); }
inline uint16_t analogReadMilliVolts(uint8_t pin) { return vcyd::VirtualHw::instance().analogReadMilliVolts(pin); }

inline double ledcSetup(uint8_t ch, double freq, uint8_t bits) {
    return vcyd::VirtualHw::instance().ledcSetup(ch, freq, bits);
}
inline void ledcAttachPin(uint8_t pin, uint8_t ch) {
    vcyd::VirtualHw::instance().ledcAttachPin(pin, ch);
}
inline void ledcWrite(uint8_t ch, uint32_t duty) {
    vcyd::VirtualHw::instance().ledcWrite(ch, duty);
}
inline double ledcWriteTone(uint8_t ch, double freq) {
    return vcyd::VirtualHw::instance().ledcWriteTone(ch, freq);
}
inline double ledcChangeFrequency(uint8_t ch, double freq, uint8_t bits) {
    return vcyd::VirtualHw::instance().ledcChangeFrequency(ch, freq, bits);
}

class EspClass {
public:
    uint32_t getFreeHeap() const { return 184320u; }
    uint32_t getMinFreeHeap() const { return 152000u; }
    uint32_t getFlashChipSize() const { return 4194304u; }
    uint32_t getCpuFreqMHz() const { return 240u; }
    const char* getChipModel() const { return "ESP32-D0WDQ6-V3 (Virtual)"; }
    void restart() {}
};

inline EspClass ESP;
