#ifndef SKYBADGER_PWM_OUTPUT_H
#define SKYBADGER_PWM_OUTPUT_H

enum PwmWriteResult
{
    PWM_WRITE_OK,
    PWM_INVALID_CONFIG,
    PWM_INVALID_PIN,
    PWM_INVALID_VALUE
};

/**
 * @brief Convert a numeric PWM value to Boolean state.
 * @details Treat any value unequal to the configured minimum as on.
 * @param[in] value (double) Input value or parsed/output value as indicated by reference and operation.
 * @param[in] minimum (double) Minimum permitted output value.
 * @return (bool) True when value differs from minimum; false at the minimum.
 */
constexpr bool pwmSwitchState(double value, double minimum)
{
    return value != minimum;
}

/**
 * @brief Convert Boolean state to a PWM range endpoint.
 * @details Choose the maximum for on and minimum for off.
 * @param[in] state (bool) Logical state or device availability to inspect.
 * @param[in] minimum (double) Minimum permitted output value.
 * @param[in] maximum (double) Maximum permitted output value.
 * @return (double) Computed value for the described operation.
 */
constexpr double pwmBooleanValue(bool state, double minimum, double maximum)
{
    return state ? maximum : minimum;
}

// PWM values are integer duty counts in the application's existing 0..1024
// scale. Reject invalid settings before changing the range, pin or cache.
/**
 * @brief Validate and apply a direct PWM output.
 * @details Check the allowed pin and numeric limits before setting range/duty and updating the cache.
 * @param[in,out] output (Output&) Output adapter or destination buffer.
 * @param[in] pin (int) Device-local pin number or native GPIO identifier.
 * @param[in] minimumPin (int) Lowest permitted native pin number.
 * @param[in] maximumPin (int) Highest permitted native pin number.
 * @param[in] value (double) Input value or parsed/output value as indicated by reference and operation.
 * @param[in] minimum (double) Minimum permitted output value.
 * @param[in] maximum (double) Maximum permitted output value.
 * @param[in,out] cachedValue (float&) Last successful command cache to update.
 * @return (PwmWriteResult) Success or the specific validation/hardware failure status.
 */
template <class Output>
constexpr PwmWriteResult writePwmOutput(Output &output, int pin, int minimumPin, int maximumPin,
                                        double value, double minimum, double maximum,
                                        float &cachedValue)
{
    if (minimum != minimum || maximum != maximum ||
        !(minimum >= 0.0 && minimum < maximum && maximum <= 1024.0) || minimum != (int)minimum ||
        maximum != (int)maximum)
        return PWM_INVALID_CONFIG;
    if (pin < minimumPin || pin > maximumPin)
        return PWM_INVALID_PIN;
    if (value != value || !(value >= minimum && value <= maximum) || value != (int)value)
        return PWM_INVALID_VALUE;

    output.setRange(1024);
    output.write(pin, (int)value);
    cachedValue = (float)value;
    return PWM_WRITE_OK;
}

#endif
