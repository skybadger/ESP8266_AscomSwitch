#ifndef SKYBADGER_SOURCE_MAPPING_H
#define SKYBADGER_SOURCE_MAPPING_H

#include <stddef.h>
#include <stdint.h>
#include <float.h>

// Shared allocation, resolution and transaction contracts for the mapped drivers.
namespace SourceMapping {
enum class Kind : uint8_t { None, NativePin, Pcf8574, I2cAdc, I2cDac };
enum class Mode : uint8_t { BinaryInput, BinaryOutput, PwmOutput, AnalogInput, AnalogOutput };
enum class State : uint8_t { Unknown, Missing, Recovering, Ready, Fault };
enum class Result : uint8_t {
    Ok, Unbound, InvalidSource, ReadOnly, MissingDevice, RecoveringDevice,
    IoFailure, NoValidValue, StaleValue, InvalidSwitchId, ResourceInUse, OutOfRange, StorageFailure
};

enum Capability : uint8_t { DigitalIn = 1, DigitalOut = 2, Pwm = 4, Adc = 8, Dac = 16 };

struct SourceRef {
    Kind kind = Kind::None;
    uint8_t bus = 0;
    uint8_t address = 0; //7-bit address; zero for a native resource.
    uint16_t channel = 0; //Native board resource ID or device-local pin/channel.
};

struct Binding {
    SourceRef source;
    Mode mode = Mode::BinaryOutput;
    bool userWritable = false;
    bool activeLow = false;
};

struct NativeResource {
    uint16_t id; //Board-defined: ADC-only A0 is not GPIO0. Shared ADC/GPIO uses one ID.
    uint8_t capabilities;
    bool reserved;
    const char* label = nullptr;
    bool startupActiveLow = false;
};

struct Device {
    Kind kind;
    uint8_t bus;
    uint8_t address;
    uint8_t channelCount;
    uint8_t capabilities;
    State state = State::Unknown;
    uint32_t generation = 0;
    uint32_t lastSuccessMs = 0;
    uint32_t lastProbeMs = 0;
    uint32_t lastFailureMs = 0;
    uint16_t failures = 0;
    uint8_t lastError = 0;

    constexpr void fail(uint32_t now, uint8_t error, State reason = State::Fault) {
        lastFailureMs = now; //A failure must never advance lastSuccessMs.
        state = reason;
        ++generation; //Invalidate every value/shadow belonging to this device.
        lastError = error;
        if (failures != UINT16_MAX) ++failures;
    }
    constexpr bool probeDue(uint32_t now, uint32_t periodMs) const {
        return static_cast<uint32_t>(now - lastProbeMs) >= periodMs;
    }
    constexpr void observeProbe(uint32_t now, bool acknowledged, uint8_t error) {
        lastProbeMs = now;
        if (!acknowledged) { fail(now, error, State::Missing); return; }
        //ACK alone cannot identify a device or authorize restoring its outputs.
        if (state != State::Ready) state = State::Recovering;
    }
    constexpr bool completeRecovery(uint32_t now, bool validatedAndInitialized,
                                    bool operatorApproved) {
        if (state != State::Recovering || !validatedAndInitialized || !operatorApproved)
            return false;
        ++generation;
        state = State::Ready;
        lastError = 0;
        failures = 0;
        lastSuccessMs = now;
        return true;
    }
    constexpr void operationSucceeded(uint32_t now) {
        lastSuccessMs = now;
        lastError = 0;
        failures = 0;
    }
};

struct ValueCache {
    double value = 0;
    uint32_t atMs = 0;
    uint32_t generation = 0;
    bool valid = false;
};

constexpr bool isI2c(Kind kind) {
    return kind == Kind::Pcf8574 || kind == Kind::I2cAdc || kind == Kind::I2cDac;
}
constexpr uint8_t requiredCapability(Mode mode) {
    return mode == Mode::BinaryInput ? DigitalIn :
           mode == Mode::BinaryOutput ? DigitalOut :
           mode == Mode::PwmOutput ? Pwm : mode == Mode::AnalogInput ? Adc : Dac;
}
constexpr bool isOutput(Mode mode) {
    return mode == Mode::BinaryOutput || mode == Mode::PwmOutput || mode == Mode::AnalogOutput;
}
//Physical uniqueness deliberately ignores input/output mode and driver kind.
constexpr bool sameResource(const SourceRef& a, const SourceRef& b) {
    if (a.kind == Kind::None || b.kind == Kind::None) return false;
    if (a.kind == Kind::NativePin && b.kind == Kind::NativePin) return a.channel == b.channel;
    return isI2c(a.kind) && isI2c(b.kind) && a.bus == b.bus &&
           a.address == b.address && a.channel == b.channel;
}

constexpr Result resolve(const Binding& binding, const NativeResource* pins, size_t pinCount,
                         const Device* devices, size_t deviceCount, bool forWrite,
                         size_t& deviceIndex) {
    deviceIndex = deviceCount; //Sentinel for native sources; never a persisted registry index.
    const auto& source = binding.source;
    if (source.kind == Kind::None) return Result::Unbound;
    if (static_cast<uint8_t>(binding.mode) > static_cast<uint8_t>(Mode::AnalogOutput))
        return Result::InvalidSource;
    if ((binding.mode == Mode::AnalogInput && source.kind != Kind::NativePin && source.kind != Kind::I2cAdc) ||
        (binding.mode == Mode::AnalogOutput && source.kind != Kind::I2cDac) ||
        (binding.mode == Mode::PwmOutput && source.kind != Kind::NativePin) ||
        ((binding.mode == Mode::BinaryInput || binding.mode == Mode::BinaryOutput) &&
         source.kind != Kind::NativePin && source.kind != Kind::Pcf8574)) return Result::InvalidSource;
    if (forWrite && (!binding.userWritable || !isOutput(binding.mode))) return Result::ReadOnly;
    const uint8_t required = requiredCapability(binding.mode);
    if (source.kind == Kind::NativePin) {
        if (source.bus != 0 || source.address != 0) return Result::InvalidSource;
        for (size_t i = 0; i < pinCount; ++i)
            if (pins[i].id == source.channel)
                return !pins[i].reserved && (pins[i].capabilities & required) ? Result::Ok : Result::InvalidSource;
        return Result::InvalidSource;
    }
    if (!isI2c(source.kind) || source.address < 0x08 || source.address > 0x77)
        return Result::InvalidSource;
    for (size_t i = 0; i < deviceCount; ++i) {
        const Device& device = devices[i];
        if (device.bus != source.bus || device.address != source.address) continue;
        if (device.kind != source.kind || source.channel >= device.channelCount ||
            (source.kind == Kind::Pcf8574 && source.channel >= 8) ||
            !(device.capabilities & required)) return Result::InvalidSource;
        deviceIndex = i;
        if (device.state == State::Unknown || device.state == State::Missing) return Result::MissingDevice;
        if (device.state == State::Recovering) return Result::RecoveringDevice;
        return device.state == State::Ready ? Result::Ok : Result::IoFailure;
    }
    return Result::MissingDevice;
}

constexpr Result cachedValue(const ValueCache& cache, const Device* device, uint32_t now,
                             uint32_t maxAgeMs, double& result) {
    if (device && device->state != State::Ready) return Result::MissingDevice;
    if (!cache.valid || (device && cache.generation != device->generation)) return Result::NoValidValue;
    if (static_cast<uint32_t>(now - cache.atMs) >= maxAgeMs) return Result::StaleValue;
    result = cache.value;
    return Result::Ok;
}

//Call after the ADC driver checks status/conversion-ready and scales the sample.
//Zero/short reads or invalid conversion data are faults, never valid zero volts.
constexpr Result recordSample(Device& device, ValueCache& cache, size_t expectedBytes,
                              size_t receivedBytes, double value, uint32_t now) {
    if (device.state != State::Ready) return Result::MissingDevice;
    if (!expectedBytes || receivedBytes != expectedBytes || value != value ||
        value > DBL_MAX || value < -DBL_MAX) {
        device.fail(now, receivedBytes != expectedBytes || !expectedBytes ? 0xfe : 0xfd);
        cache.valid = false;
        return Result::IoFailure;
    }
    device.operationSucceeded(now);
    cache = {value, now, device.generation, true};
    return Result::Ok;
}

//Configuration validation is independent of current online state. Configured
//device records remain in the registry while absent; unknown addresses cannot be
//allocated until a driver profile has been explicitly created/confirmed.
constexpr Result assignBinding(Binding* bindings, size_t count, size_t switchId,
                               const Binding& candidate, const NativeResource* pins,
                               size_t pinCount, const Device* devices, size_t deviceCount) {
    if (switchId >= count) return Result::InvalidSwitchId;
    if (candidate.source.kind == Kind::None) { bindings[switchId] = candidate; return Result::Ok; }
    size_t slot = deviceCount;
    const Result checked = resolve(candidate, pins, pinCount, devices, deviceCount, false, slot);
    if (checked != Result::Ok && !((checked == Result::MissingDevice || checked == Result::RecoveringDevice ||
                                   checked == Result::IoFailure) && slot < deviceCount)) return checked;
    if (candidate.userWritable && !isOutput(candidate.mode)) return Result::ReadOnly;
    for (size_t i = 0; i < count; ++i)
        if (i != switchId && sameResource(bindings[i].source, candidate.source)) return Result::ResourceInUse;
    bindings[switchId] = candidate; //Commit last; failed validation preserves old assignment.
    return Result::Ok;
}

struct PcfShadow { uint8_t levels = 0xff; uint32_t generation = 0; bool valid = false; };

//profileConfirmed is a configured-driver/physical-wiring approval, not inferred
//from an ACK. safeLevels covers ALL eight pins, including unallocated inputs.
template<class Bus>
constexpr Result recoverPcf8574(Bus& bus, Device& device, uint8_t safeLevels,
                               bool profileConfirmed, bool operatorApproved,
                               PcfShadow& shadow, uint32_t now) {
    if (device.kind != Kind::Pcf8574 || device.channelCount != 8 ||
        device.address < 0x08 || device.address > 0x77) return Result::InvalidSource;
    if (device.state != State::Recovering || !profileConfirmed || !operatorApproved)
        return Result::RecoveringDevice;
    shadow.valid = false;
    bus.beginTransmission(device.address);
    const bool buffered = bus.write(safeLevels) == 1;
    const uint8_t status = bus.endTransmission();
    if (!buffered || status != 0) {
        device.fail(now, status ? status : 0xff);
        return Result::IoFailure;
    }
    device.completeRecovery(now, true, true);
    shadow = {safeLevels, device.generation, true};
    return Result::Ok;
}

//Wire-compatible adapter. Caller serializes the complete transaction. The shadow
//must be initialized using an approved whole-device safe-state byte beforehand.
template<class Bus>
constexpr Result writePcf8574Bit(Bus& bus, Device& device, uint16_t pin, bool logical,
                                bool activeLow, PcfShadow& shadow, ValueCache& cache,
                                uint32_t now) {
    if (device.kind != Kind::Pcf8574 || device.address < 0x08 || device.address > 0x77 ||
        pin >= 8 || pin >= device.channelCount || !(device.capabilities & DigitalOut)) return Result::InvalidSource;
    if (device.state != State::Ready) return Result::MissingDevice;
    if (!shadow.valid || shadow.generation != device.generation) return Result::NoValidValue;
    const bool physical = logical != activeLow;
    const uint8_t mask = static_cast<uint8_t>(1u << pin);
    const uint8_t next = physical ? static_cast<uint8_t>(shadow.levels | mask) :
                                    static_cast<uint8_t>(shadow.levels & ~mask);
    bus.beginTransmission(device.address);
    const bool buffered = bus.write(next) == 1;
    const uint8_t status = bus.endTransmission();
    if (!buffered || status != 0) {
        device.fail(now, status ? status : 0xff);
        shadow.valid = false;
        cache.valid = false; //A failed write leaves the physical output unknown.
        return Result::IoFailure;
    }
    shadow.levels = next;
    device.operationSucceeded(now);
    cache = {logical ? 1.0 : 0.0, now, device.generation, true};
    return Result::Ok;
}
} //namespace SourceMapping
#endif
