#ifndef SKYBADGER_PWM_OUTPUT_H
#define SKYBADGER_PWM_OUTPUT_H

enum PwmWriteResult {
    PWM_WRITE_OK,
    PWM_INVALID_CONFIG,
    PWM_INVALID_PIN,
    PWM_INVALID_VALUE
};

constexpr bool pwmSwitchState(double value, double minimum)
{
    return value != minimum;
}

constexpr double pwmBooleanValue(bool state, double minimum, double maximum)
{
    return state ? maximum : minimum;
}

// PWM values are integer duty counts in the application's existing 0..1024
// scale. Reject invalid settings before changing the range, pin or cache.
template <class Output>
constexpr PwmWriteResult writePwmOutput(Output& output, int pin, int minimumPin,
                                      int maximumPin, double value, double minimum,
                                      double maximum, float& cachedValue)
{
    if (minimum != minimum || maximum != maximum
        || !(minimum >= 0.0 && minimum < maximum && maximum <= 1024.0)
        || minimum != (int) minimum || maximum != (int) maximum)
        return PWM_INVALID_CONFIG;
    if (pin < minimumPin || pin > maximumPin)
        return PWM_INVALID_PIN;
    if (value != value || !(value >= minimum && value <= maximum) || value != (int) value)
        return PWM_INVALID_VALUE;

    output.setRange(1024);
    output.write(pin, (int) value);
    cachedValue = (float) value;
    return PWM_WRITE_OK;
}

#endif
