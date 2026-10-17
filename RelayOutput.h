#ifndef SKYBADGER_RELAY_OUTPUT_H
#define SKYBADGER_RELAY_OUTPUT_H

enum RelayWriteResult
{
    RELAY_WRITE_OK,
    RELAY_INVALID_VALUE,
    RELAY_INVALID_PIN,
    RELAY_WRITE_FAILED
};

// The configured relay index maps directly to one of this PCF8574's eight pins.
// Cache logical state only after the bus write succeeds. constexpr also permits
// compile-time checks with a simulated expander using the target toolchain.
/**
 * @brief Validate and apply one expander relay output.
 * @details Check pin and binary value, apply polarity, and update the cache only after a successful transaction.
 * @param[in,out] device (Expander&) Hardware adapter or runtime device descriptor.
 * @param[in] pin (unsigned int) Device-local pin number or native GPIO identifier.
 * @param[in] value (double) Input value or parsed/output value as indicated by reference and operation.
 * @param[in] reverseLogic (bool) Invert physical binary output levels when true.
 * @param[in,out] cachedValue (float&) Last successful command cache to update.
 * @return (RelayWriteResult) Success or the specific validation/hardware failure status.
 */
template <class Expander>
constexpr RelayWriteResult writeRelayOutput(Expander &device, unsigned int pin, double value,
                                            bool reverseLogic, float &cachedValue)
{
    if (value != 0.0 && value != 1.0)
        return RELAY_INVALID_VALUE;
    if (pin >= 8)
        return RELAY_INVALID_PIN;

    const bool on = value == 1.0;
    device.write(pin, reverseLogic ? !on : on);
    if (device.lastError() != 0)
        return RELAY_WRITE_FAILED;

    cachedValue = on ? 1.0F : 0.0F;
    return RELAY_WRITE_OK;
}

#endif
