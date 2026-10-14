#include "../RelayOutput.h"

struct TestExpander {
    unsigned int calls = 0;
    unsigned int lastPin = 0;
    bool level = false;
    int error = 0;
    constexpr void write(unsigned int pin, bool value) {
        ++calls;
        lastPin = pin;
        level = value;
    }
    constexpr int lastError() { return error; }
};

constexpr bool checkRelayRoundTrip(bool reverse)
{
    TestExpander device;
    float cached = 0;
    if (writeRelayOutput(device, 7, 1.0, reverse, cached) != RELAY_WRITE_OK)
        return false;
    if (device.calls != 1 || device.lastPin != 7 || device.level == reverse || cached != 1.0F)
        return false;
    if (writeRelayOutput(device, 7, 0.0, reverse, cached) != RELAY_WRITE_OK)
        return false;
    return device.calls == 2 && device.level == reverse && cached == 0.0F;
}

constexpr bool checkInvalidValue(double value)
{
    TestExpander device;
    float cached = 1;
    return writeRelayOutput(device, 0, value, true, cached) == RELAY_INVALID_VALUE
        && device.calls == 0 && cached == 1.0F;
}

constexpr bool checkBadPin()
{
    TestExpander device;
    float cached = 1;
    return writeRelayOutput(device, 8, 0.0, true, cached) == RELAY_INVALID_PIN
        && device.calls == 0 && cached == 1.0F;
}

constexpr bool checkBusFailure()
{
    TestExpander device;
    device.error = 2;
    float cached = 1;
    return writeRelayOutput(device, 0, 0.0, true, cached) == RELAY_WRITE_FAILED
        && device.calls == 1 && cached == 1.0F;
}

static_assert(checkRelayRoundTrip(false), "Active-high relay transitions");
static_assert(checkRelayRoundTrip(true), "Active-low relay transitions");
static_assert(checkInvalidValue(-1.0), "Negative values must not write");
static_assert(checkInvalidValue(0.5), "Fractional values must not write");
static_assert(checkInvalidValue(2.0), "Values above one must not write");
static_assert(checkInvalidValue(__builtin_nan("")), "NaN must not write");
static_assert(checkInvalidValue(__builtin_inf()), "Infinity must not write");
static_assert(checkBadPin(), "Nonexistent PCF8574 pin must not write");
static_assert(checkBusFailure(), "Failed bus write must preserve cached state");
