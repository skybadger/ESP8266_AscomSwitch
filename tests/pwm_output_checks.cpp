#include "../PwmOutput.h"

struct TestPwmOutput {
    int range = 0;
    int rangeCalls = 0;
    int writes = 0;
    int pin = -1;
    int duty = -1;
    constexpr void setRange(unsigned int value) { range = value; ++rangeCalls; }
    constexpr void write(int newPin, int value) { pin = newPin; duty = value; ++writes; }
};

constexpr bool checkBooleanRoundTrip()
{
    TestPwmOutput output;
    float cached = 300;
    if (writePwmOutput(output, 3, 0, 3, pwmBooleanValue(true, 100, 900),
                       100, 900, cached) != PWM_WRITE_OK)
        return false;
    if (cached != 900 || output.range != 1024 || output.writes != 1
        || output.pin != 3 || output.duty != 900 || !pwmSwitchState(cached, 100))
        return false;
    if (writePwmOutput(output, 3, 0, 3, pwmBooleanValue(false, 100, 900),
                       100, 900, cached) != PWM_WRITE_OK)
        return false;
    return cached == 100 && output.rangeCalls == 2 && output.writes == 2
        && output.duty == 100 && !pwmSwitchState(cached, 100);
}

constexpr bool checkRejected(int pin, double value, double minimum,
                            double maximum, PwmWriteResult expected)
{
    TestPwmOutput output;
    float cached = 300;
    return writePwmOutput(output, pin, 0, 3, value, minimum, maximum, cached) == expected
        && output.writes == 0 && output.rangeCalls == 0 && cached == 300;
}

static_assert(!pwmSwitchState(100, 100), "At minimum is false");
static_assert(pwmSwitchState(101, 100), "Above minimum is true, not a midpoint threshold");
static_assert(pwmSwitchState(900, 100), "At maximum is true");
static_assert(checkBooleanRoundTrip(), "Both boolean endpoints must drive PWM and update cache");
static_assert(checkRejected(-1, 300, 100, 900, PWM_INVALID_PIN), "Unassigned pin");
static_assert(checkRejected(4, 300, 100, 900, PWM_INVALID_PIN), "Pin beyond board range");
static_assert(checkRejected(3, 99, 100, 900, PWM_INVALID_VALUE), "Below minimum");
static_assert(checkRejected(3, 901, 100, 900, PWM_INVALID_VALUE), "Above maximum");
static_assert(checkRejected(3, 300.5, 100, 900, PWM_INVALID_VALUE), "Fractional duty count");
static_assert(checkRejected(3, __builtin_nan(""), 100, 900, PWM_INVALID_VALUE), "NaN");
static_assert(checkRejected(3, __builtin_inf(), 100, 900, PWM_INVALID_VALUE), "Infinity");
static_assert(checkRejected(3, 100, 100, 100, PWM_INVALID_CONFIG), "Equal range limits");
static_assert(checkRejected(3, 100, 900, 100, PWM_INVALID_CONFIG), "Reversed range");
static_assert(checkRejected(3, 100, 0, 1025, PWM_INVALID_CONFIG), "Range beyond hardware scale");
static_assert(checkRejected(3, 100, 0.5, 900, PWM_INVALID_CONFIG), "Fractional endpoint");
static_assert(checkRejected(3, 100, 0, __builtin_inf(), PWM_INVALID_CONFIG), "Infinite endpoint");
