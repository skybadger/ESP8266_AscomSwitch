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
class String
{
    std::string text;

  public:
    /**
     * @brief Provide the test adapter operation for string.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @par Arguments
     * None.
     * @return No value (constructor).
     */
    String() = default;
    /**
     * @brief Provide the test adapter operation for string.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] value (const char*) Input value or parsed/output value as indicated by reference and operation.
     * @return No value (constructor).
     */
    String(const char *value) : text(value ? value : "")
    {
    }
    /**
     * @brief Provide the test adapter operation for string.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] value (std::string) Input value or parsed/output value as indicated by reference and operation.
     * @return No value (constructor).
     */
    String(std::string value) : text(std::move(value))
    {
    }
    /**
     * @brief Provide the test adapter operation for string.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] value (T) Input value or parsed/output value as indicated by reference and operation.
     * @param[in] base (int) Value used by the test adapter.
     * @return No value (constructor).
     */
    template <class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    String(T value, int base = 10)
    {
        std::ostringstream out;
        if (base == 16)
            out << std::hex;
        if constexpr (std::is_signed_v<T>)
            out << (long long)value;
        else
            out << (unsigned long long)value;
        text = out.str();
    }
    /**
     * @brief Provide the test adapter operation for string.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] value (double) Input value or parsed/output value as indicated by reference and operation.
     * @param[in] places (int) Value used by the test adapter.
     * @return No value (constructor).
     */
    String(double value, int places = 2)
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision(places) << value;
        text = out.str();
    }
    /**
     * @brief Provide the test adapter operation for length.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @par Arguments
     * None.
     * @return (size_t) Adapter value or regression result used by the calling test.
     */
    size_t length() const
    {
        return text.length();
    }
    /**
     * @brief Provide the test adapter operation for c str.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @par Arguments
     * None.
     * @return (const char*) Adapter value or regression result used by the calling test.
     */
    const char *c_str() const
    {
        return text.c_str();
    }
    /**
     * @brief Provide the test adapter operation for reserve.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] count (size_t) Number of entries or bytes requested.
     * @return void; effects are described above.
     */
    void reserve(size_t count)
    {
        text.reserve(count);
    }
    /**
     * @brief Provide the test adapter operation for operator[].
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] i (size_t) Value used by the test adapter.
     * @return (char) Adapter value or regression result used by the calling test.
     */
    char operator[](size_t i) const
    {
        return text[i];
    }
    /**
     * @brief Provide the test adapter operation for starts with.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] value (const char*) Input value or parsed/output value as indicated by reference and operation.
     * @return (bool) True when the described check/operation succeeds; otherwise false.
     */
    bool startsWith(const char *value) const
    {
        return text.rfind(value, 0) == 0;
    }
    /**
     * @brief Provide the test adapter operation for index of.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] c (char) Character to inspect.
     * @param[in] start (size_t) Value used by the test adapter.
     * @return (int) Adapter value or regression result used by the calling test.
     */
    int indexOf(char c, size_t start = 0) const
    {
        const auto p = text.find(c, start);
        return p == std::string::npos ? -1 : (int)p;
    }
    /**
     * @brief Provide the test adapter operation for substring.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] start (size_t) Value used by the test adapter.
     * @param[in] end (size_t) Value used by the test adapter.
     * @return (String) Generated text or the updated String reference.
     */
    String substring(size_t start, size_t end = std::string::npos) const
    {
        return text.substr(start, end == std::string::npos ? end : end - start);
    }
    /**
     * @brief Provide the test adapter operation for operator+=.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] other (const String&) Value used by the test adapter.
     * @return (String&) Generated text or the updated String reference.
     */
    String &operator+=(const String &other)
    {
        text += other.text;
        return *this;
    }
    /**
     * @brief Provide the test adapter operation for operator+=.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] c (char) Character to inspect.
     * @return (String&) Generated text or the updated String reference.
     */
    String &operator+=(char c)
    {
        text += c;
        return *this;
    }
    /**
     * @brief Provide the test adapter operation for operator+=.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] other (T) Value used by the test adapter.
     * @return (String&) Generated text or the updated String reference.
     */
    template <class T> String &operator+=(T other)
    {
        return *this += String(other);
    }
    friend
        /**
         * @brief Provide the test adapter operation for operator+.
         * @details Use deterministic in-memory state instead of physical hardware or network I/O.
         * @param[in] a (const String&) First physical source identity.
         * @param[in] b (const String&) Second physical source identity.
         * @return (String) Generated text or the updated String reference.
         */
        String
        operator+(const String &a, const String &b)
    {
        return a.text + b.text;
    }
    friend
        /**
         * @brief Provide the test adapter operation for operator==.
         * @details Use deterministic in-memory state instead of physical hardware or network I/O.
         * @param[in] a (const String&) First physical source identity.
         * @param[in] b (const String&) Second physical source identity.
         * @return (bool) True when the described check/operation succeeds; otherwise false.
         */
        bool
        operator==(const String &a, const String &b)
    {
        return a.text == b.text;
    }
    friend
        /**
         * @brief Provide the test adapter operation for operator!=.
         * @details Use deterministic in-memory state instead of physical hardware or network I/O.
         * @param[in] a (const String&) First physical source identity.
         * @param[in] b (const String&) Second physical source identity.
         * @return (bool) True when the described check/operation succeeds; otherwise false.
         */
        bool
        operator!=(const String &a, const String &b)
    {
        return !(a == b);
    }
};
inline uint32_t fakeNow = 0;
/**
 * @brief Provide the test adapter operation for millis.
 * @details Use deterministic in-memory state instead of physical hardware or network I/O.
 * @par Arguments
 * None.
 * @return (uint32_t) Adapter value or regression result used by the calling test.
 */
inline uint32_t millis()
{
    return fakeNow;
}
inline int pinLevels[257]{};
inline int pwmRange = 0;
/**
 * @brief Provide the test adapter operation for digital write.
 * @details Use deterministic in-memory state instead of physical hardware or network I/O.
 * @param[in] pin (int) Device-local pin number or native GPIO identifier.
 * @param[in] value (int) Input value or parsed/output value as indicated by reference and operation.
 * @return void; effects are described above.
 */
inline void digitalWrite(int pin, int value)
{
    pinLevels[pin] = value;
}
/**
 * @brief Provide the test adapter operation for digital read.
 * @details Use deterministic in-memory state instead of physical hardware or network I/O.
 * @param[in] pin (int) Device-local pin number or native GPIO identifier.
 * @return (int) Adapter value or regression result used by the calling test.
 */
inline int digitalRead(int pin)
{
    return pinLevels[pin];
}
/**
 * @brief Provide the test adapter operation for pin mode.
 * @details Use deterministic in-memory state instead of physical hardware or network I/O.
 * @par Unnamed argument
 * (int) Reserved or ignored by this test adapter.
 * @par Unnamed argument
 * (int) Reserved or ignored by this test adapter.
 * @return void; effects are described above.
 */
inline void pinMode(int, int)
{
}
/**
 * @brief Provide the test adapter operation for analog write range.
 * @details Use deterministic in-memory state instead of physical hardware or network I/O.
 * @param[in] range (unsigned) Value used by the test adapter.
 * @return void; effects are described above.
 */
inline void analogWriteRange(unsigned range)
{
    pwmRange = range;
}
/**
 * @brief Provide the test adapter operation for analog write.
 * @details Use deterministic in-memory state instead of physical hardware or network I/O.
 * @param[in] pin (int) Device-local pin number or native GPIO identifier.
 * @param[in] value (int) Input value or parsed/output value as indicated by reference and operation.
 * @return void; effects are described above.
 */
inline void analogWrite(int pin, int value)
{
    pinLevels[pin] = value;
}
/**
 * @brief Provide the test adapter operation for analog read.
 * @details Use deterministic in-memory state instead of physical hardware or network I/O.
 * @par Unnamed argument
 * (int) Reserved or ignored by this test adapter.
 * @return (int) Adapter value or regression result used by the calling test.
 */
inline int analogRead(int)
{
    return 512;
}
/**
 * @brief Provide the test adapter operation for debug i.
 * @details Use deterministic in-memory state instead of physical hardware or network I/O.
 * @par Unnamed argument
 * (const char*) Reserved or ignored by this test adapter.
 * @return void; effects are described above.
 */
inline void debugI(const char *, ...)
{
}
struct FakeWire
{
    uint8_t error = 0, address = 0, reg = 0;
    uint8_t lastValue = 0;
    uint16_t rawAdc = 0x1000;
    int writes = 0, position = 0, returned = 0;
    bool shortRead = false;
    std::vector<uint8_t> sent;
    /**
     * @brief Provide the test adapter operation for begin transmission.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] at (uint8_t) Value used by the test adapter.
     * @return void; effects are described above.
     */
    void beginTransmission(uint8_t at)
    {
        address = at;
        sent.clear();
    }
    /**
     * @brief Provide the test adapter operation for write.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] byte (uint8_t) Value used by the test adapter.
     * @return (size_t) Adapter value or regression result used by the calling test.
     */
    size_t write(uint8_t byte)
    {
        sent.push_back(byte);
        return 1;
    }
    /**
     * @brief Provide the test adapter operation for end transmission.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @par Arguments
     * None.
     * @return (uint8_t) Adapter value or regression result used by the calling test.
     */
    uint8_t endTransmission()
    {
        if (!sent.empty())
        {
            ++writes;
            lastValue = sent.back();
            reg = sent[0];
        }
        return error;
    }
    /**
     * @brief Provide the test adapter operation for request from.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] at (uint8_t) Value used by the test adapter.
     * @param[in] count (uint8_t) Number of entries or bytes requested.
     * @return (uint8_t) Adapter value or regression result used by the calling test.
     */
    uint8_t requestFrom(uint8_t at, uint8_t count)
    {
        address = at;
        position = 0;
        returned = shortRead ? 0 : count;
        return returned;
    }
    /**
     * @brief Provide the test adapter operation for available.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @par Arguments
     * None.
     * @return (int) Adapter value or regression result used by the calling test.
     */
    int available()
    {
        return returned - position;
    }
    /**
     * @brief Provide the test adapter operation for read.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @par Arguments
     * None.
     * @return (int) Adapter value or regression result used by the calling test.
     */
    int read()
    {
        uint16_t value = reg == 1 ? 0x8000 : rawAdc;
        return position++ == 0 ? value >> 8 : value & 255;
    }
};
inline FakeWire Wire;
struct FakeServer
{
    std::vector<std::pair<String, String>> values;
    /**
     * @brief Provide the test adapter operation for args.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @par Arguments
     * None.
     * @return (int) Adapter value or regression result used by the calling test.
     */
    int args() const
    {
        return (int)values.size();
    }
    /**
     * @brief Provide the test adapter operation for arg name.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] i (int) Value used by the test adapter.
     * @return (String) Generated text or the updated String reference.
     */
    String argName(int i) const
    {
        return values[i].first;
    }
    /**
     * @brief Provide the test adapter operation for arg.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] i (int) Value used by the test adapter.
     * @return (String) Generated text or the updated String reference.
     */
    String arg(int i) const
    {
        return values[i].second;
    }
};
inline FakeServer server;
inline String lastUiError;
inline int lastUiStatus;
/**
 * @brief Provide the test adapter operation for validate setting handler request.
 * @details Use deterministic in-memory state instead of physical hardware or network I/O.
 * @par Unnamed argument
 * (const char*) Reserved or ignored by this test adapter.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
inline bool validateSettingHandlerRequest(const char *)
{
    return true;
}
/**
 * @brief Provide the test adapter operation for send device setup.
 * @details Use deterministic in-memory state instead of physical hardware or network I/O.
 * @param[in] status (int) Value used by the test adapter.
 * @par Unnamed argument
 * (String&) Reserved or ignored by this test adapter.
 * @param[in,out] error (String&) Failure code or human-readable failure explanation.
 * @return void; effects are described above.
 */
inline void sendDeviceSetup(int status, String &, String &error)
{
    lastUiStatus = status;
    lastUiError = error;
}
/**
 * @brief Provide the test adapter operation for send driver0 setup.
 * @details Use deterministic in-memory state instead of physical hardware or network I/O.
 * @param[in] status (int) Value used by the test adapter.
 * @par Unnamed argument
 * (String&) Reserved or ignored by this test adapter.
 * @param[in,out] error (String&) Failure code or human-readable failure explanation.
 * @return void; effects are described above.
 */
inline void sendDriver0Setup(int status, String &, String &error)
{
    lastUiStatus = status;
    lastUiError = error;
}
