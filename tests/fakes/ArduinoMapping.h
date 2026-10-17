#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <type_traits>
#include <cstring>
#include <cmath>
#include <stdint.h>
#include <stdlib.h>
using std::isfinite;
#define PROGMEM
#define ESP8266_01
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define A0 256
#define HEX 16
class String {
    std::string text;
public:
    String() = default;
    String(const char* value) : text(value ? value : "") {}
    String(std::string value) : text(std::move(value)) {}
    template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    String(T value, int base = 10) {
        std::ostringstream out; if (base == 16) out << std::hex;
        if constexpr (std::is_signed_v<T>) out << (long long)value; else out << (unsigned long long)value;
        text = out.str();
    }
    String(double value, int places = 2) { std::ostringstream out; out << std::fixed << std::setprecision(places) << value; text = out.str(); }
    size_t length() const { return text.length(); }
    const char* c_str() const { return text.c_str(); }
    void reserve(size_t count) { text.reserve(count); }
    char operator[](size_t i) const { return text[i]; }
    bool startsWith(const char* value) const { return text.rfind(value, 0) == 0; }
    int indexOf(char c, size_t start = 0) const { const auto p = text.find(c, start); return p == std::string::npos ? -1 : (int)p; }
    String substring(size_t start, size_t end = std::string::npos) const { return text.substr(start, end == std::string::npos ? end : end - start); }
    String& operator+=(const String& other) { text += other.text; return *this; }
    String& operator+=(char c) { text += c; return *this; }
    template<class T> String& operator+=(T other) { return *this += String(other); }
    friend String operator+(const String& a, const String& b) { return a.text + b.text; }
    friend bool operator==(const String& a, const String& b) { return a.text == b.text; }
    friend bool operator!=(const String& a, const String& b) { return !(a == b); }
};
inline uint32_t fakeNow = 0;
inline uint32_t millis() { return fakeNow; }
inline int pinLevels[257]{};
inline int pwmRange = 0;
inline void digitalWrite(int pin, int value) { pinLevels[pin] = value; }
inline int digitalRead(int pin) { return pinLevels[pin]; }
inline void pinMode(int, int) {}
inline void analogWriteRange(unsigned range) { pwmRange = range; }
inline void analogWrite(int pin, int value) { pinLevels[pin] = value; }
inline int analogRead(int) { return 512; }
inline void debugI(const char*, ...) {}
struct FakeWire {
    uint8_t error = 0, address = 0, reg = 0;
    uint8_t lastValue = 0;
    uint16_t rawAdc = 0x1000;
    int writes = 0, position = 0, returned = 0;
    bool shortRead = false;
    std::vector<uint8_t> sent;
    void beginTransmission(uint8_t at) { address = at; sent.clear(); }
    size_t write(uint8_t byte) { sent.push_back(byte); return 1; }
    uint8_t endTransmission() {
        if (!sent.empty()) { ++writes; lastValue = sent.back(); reg = sent[0]; }
        return error;
    }
    uint8_t requestFrom(uint8_t at, uint8_t count) { address = at; position = 0; returned = shortRead ? 0 : count; return returned; }
    int available() { return returned - position; }
    int read() { uint16_t value = reg == 1 ? 0x8000 : rawAdc; return position++ == 0 ? value >> 8 : value & 255; }
};
inline FakeWire Wire;
struct FakeServer {
    std::vector<std::pair<String, String>> values;
    int args() const { return (int)values.size(); }
    String argName(int i) const { return values[i].first; }
    String arg(int i) const { return values[i].second; }
};
inline FakeServer server;
inline String lastUiError;
inline int lastUiStatus;
inline bool validateSettingHandlerRequest(const char*) { return true; }
inline void sendDeviceSetup(int status, String&, String& error) { lastUiStatus = status; lastUiError = error; }
inline void sendDriver0Setup(int status, String&, String& error) { lastUiStatus = status; lastUiError = error; }
