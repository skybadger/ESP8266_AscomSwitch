#include "../SourceMapping.h"
#include <limits>
using namespace SourceMapping;

constexpr NativeResource pins[] = {{2, DigitalIn | DigitalOut | Pwm, false},
                                   {0, DigitalIn | DigitalOut, true}, {256, Adc, false}};
constexpr Device pcf() { return {Kind::Pcf8574, 0, 0x20, 8, DigitalIn | DigitalOut}; }
constexpr Binding relay(uint16_t pin = 5) {
    return {{Kind::Pcf8574, 0, 0x20, pin}, Mode::BinaryOutput, true, true};
}
constexpr Result mapped(Binding b, State state = State::Ready, bool write = false) {
    Device d = pcf(); d.state = state;
    size_t deviceIndex = 0;
    return resolve(b, pins, 3, &d, 1, write, deviceIndex);
}
constexpr Binding native(Mode mode, uint16_t pin, bool writable = true) {
    return {{Kind::NativePin, 0, 0, pin}, mode, writable, false};
}
static_assert(mapped(relay(), State::Ready, true) == Result::Ok, "PCF pin resolves");
static_assert(mapped(relay(8)) == Result::InvalidSource, "Bounds checked");
static_assert(mapped(relay(), State::Missing) == Result::MissingDevice, "Lost hardware rejected");
static_assert(mapped(relay(), State::Unknown) == Result::MissingDevice, "Startup unknown rejected");
static_assert(mapped(relay(), State::Recovering) == Result::RecoveringDevice, "ACK not readiness");
static_assert(mapped(relay(), State::Fault) == Result::IoFailure, "Transaction fault rejected");
static_assert(mapped(Binding{}) == Result::Unbound, "Unallocated source rejected");
static_assert(mapped(native(Mode::PwmOutput, 2), State::Ready, true) == Result::Ok, "Native PWM");
static_assert(mapped(native(Mode::BinaryInput, 2), State::Ready, true) == Result::ReadOnly, "Input cannot write");
static_assert(mapped(native(Mode::AnalogInput, 256)) == Result::Ok, "Dedicated native ADC ID");
static_assert(mapped(native(Mode::AnalogInput, 256), State::Ready, true) == Result::ReadOnly, "ADC readonly");
static_assert(mapped(native(Mode::BinaryOutput, 0)) == Result::InvalidSource, "Reserved pin denied");
static_assert(mapped(native(Mode::PwmOutput, 256)) == Result::InvalidSource, "ADC not PWM");
static_assert(mapped(native(Mode::BinaryOutput, 9)) == Result::InvalidSource, "Missing board resource");
static_assert(mapped(native(static_cast<Mode>(99), 2)) == Result::InvalidSource, "Corrupt mode denied");
static_assert(sameResource(native(Mode::BinaryOutput, 2).source, native(Mode::PwmOutput, 2).source), "GPIO modes share resource");
static_assert(!sameResource(native(Mode::AnalogInput, 256).source, native(Mode::BinaryOutput, 0).source), "A0 not GPIO0");
static_assert(sameResource(relay().source, {Kind::I2cAdc, 0, 0x20, 5}), "Driver change cannot duplicate physical pin");
static_assert(!sameResource(relay().source, {Kind::Pcf8574, 1, 0x20, 5}), "Different buses distinct");

struct Bus {
    uint8_t status = 0;
    uint8_t address = 0;
    uint8_t value = 0;
    unsigned writes = 0;
    bool full = false;
    constexpr void beginTransmission(uint8_t a) { address = a; }
    constexpr size_t write(uint8_t v) { ++writes; value = v; return full ? 0 : 1; }
    constexpr uint8_t endTransmission() { return status; }
};

constexpr bool recoveryAndWrite() {
    Device d = pcf(); PcfShadow shadow; ValueCache cache; Bus bus;
    d.observeProbe(10, true, 0);
    if (d.state != State::Recovering) return false;
    if (recoverPcf8574(bus, d, 0xff, true, false, shadow, 11) != Result::RecoveringDevice || bus.writes) return false;
    if (recoverPcf8574(bus, d, 0xff, false, true, shadow, 11) != Result::RecoveringDevice || bus.writes) return false;
    if (recoverPcf8574(bus, d, 0xff, true, true, shadow, 12) != Result::Ok) return false;
    if (shadow.levels != 0xff || bus.writes != 1 || d.state != State::Ready) return false;
    if (writePcf8574Bit(bus, d, 5, true, true, shadow, cache, 20) != Result::Ok) return false;
    return bus.address == 0x20 && bus.value == 0xdf && cache.value == 1 && cache.valid && d.lastSuccessMs == 20;
}
static_assert(recoveryAndWrite(), "Approved safe recovery then active-low mapped pin write");

constexpr bool failureAndReappearance(uint8_t error, bool bufferFull) {
    Device d = pcf(); d.state = State::Ready;
    PcfShadow shadow{0xff, d.generation, true}; ValueCache cache{1, 5, d.generation, true};
    ValueCache otherCache = cache; Bus bus; bus.status = error; bus.full = bufferFull;
    if (writePcf8574Bit(bus, d, 1, true, false, shadow, cache, 10) != Result::IoFailure) return false;
    if (d.state != State::Fault || cache.valid || shadow.valid || d.lastSuccessMs != 0 || d.lastFailureMs != 10) return false;
    d.observeProbe(20, true, 0);
    const unsigned writes = bus.writes;
    if (writePcf8574Bit(bus, d, 1, true, false, shadow, cache, 21) != Result::MissingDevice || bus.writes != writes) return false;
    double value = 123;
    return cachedValue(otherCache, &d, 21, 100, value) != Result::Ok && value == 123;
}
static_assert(failureAndReappearance(2, false), "Address NACK invalidates device and all channels");
static_assert(failureAndReappearance(3, false), "Data NACK invalidates outputs");
static_assert(failureAndReappearance(5, false), "Timeout invalidates outputs");
static_assert(failureAndReappearance(0, true), "Buffer failure invalidates outputs even with ACK");

constexpr bool lostProbeAndStaleEpoch() {
    Device d = pcf(); d.state = State::Ready;
    ValueCache cache{3.3, 10, d.generation, true}; double out = 99;
    if (cachedValue(cache, &d, 20, 100, out) != Result::Ok || out != 3.3) return false;
    d.observeProbe(30, false, 2);
    if (d.state != State::Missing || d.lastProbeMs != 30) return false;
    d.observeProbe(40, true, 0);
    if (!d.completeRecovery(41, true, true)) return false;
    return cachedValue(cache, &d, 42, 100, out) == Result::NoValidValue;
}
static_assert(lostProbeAndStaleEpoch(), "Old data remains invalid after device recovery");

constexpr bool cacheTiming() {
    ValueCache cache{7, UINT32_MAX - 5, 0, true}; double out = 0;
    return cachedValue(cache, nullptr, 2, 9, out) == Result::Ok && out == 7 &&
           cachedValue(cache, nullptr, 3, 9, out) == Result::StaleValue;
}
static_assert(cacheTiming(), "Freshness deadline and millis wraparound");

constexpr bool registryMovement() {
    Device devices[] = {{Kind::I2cAdc, 0, 0x48, 4, Adc}, pcf()};
    devices[0].state = State::Ready; devices[1].state = State::Ready; size_t slot = 0;
    if (resolve(relay(), pins, 3, devices, 2, true, slot) != Result::Ok || slot != 1) return false;
    Binding sensor{{Kind::I2cAdc, 0, 0x48, 3}, Mode::AnalogInput, true, false};
    if (resolve(sensor, pins, 3, devices, 2, false, slot) != Result::Ok || slot != 0) return false;
    if (resolve(sensor, pins, 3, devices, 2, true, slot) != Result::ReadOnly) return false;
    devices[0] = devices[1];
    return resolve(sensor, pins, 3, devices, 1, false, slot) == Result::MissingDevice;
}
static_assert(registryMovement(), "Bindings do not depend on inventory array index");

constexpr bool invalidDeviceAndNoShadow() {
    Device d = pcf(); d.state = State::Ready; PcfShadow shadow; ValueCache cache; Bus bus;
    if (writePcf8574Bit(bus, d, 256, true, true, shadow, cache, 0) != Result::InvalidSource || bus.writes) return false;
    if (writePcf8574Bit(bus, d, 1, true, true, shadow, cache, 0) != Result::NoValidValue || bus.writes) return false;
    d.address = 160;
    return writePcf8574Bit(bus, d, 1, true, true, shadow, cache, 0) == Result::InvalidSource && !bus.writes;
}
static_assert(invalidDeviceAndNoShadow(), "No uninitialized port write; reject 8-bit address and untruncated wide pin");

constexpr bool failedRecovery() {
    Device d = pcf(); d.observeProbe(1, true, 0); Bus bus; bus.status = 2; PcfShadow shadow;
    return recoverPcf8574(bus, d, 0xff, true, true, shadow, 2) == Result::IoFailure &&
           d.state == State::Fault && !shadow.valid;
}
static_assert(failedRecovery(), "NACK during safe initialization does not enable writes");

constexpr bool probeTiming() {
    Device d = pcf(); d.lastProbeMs = UINT32_MAX - 5;
    return !d.probeDue(2, 9) && d.probeDue(3, 9);
}
static_assert(probeTiming(), "Probe scheduling across millis wraparound");

constexpr bool allocationChecks() {
    Binding bindings[] = {relay(1), native(Mode::BinaryOutput, 2)};
    Device d = pcf(); //Unknown at boot, but profile is configured.
    if (assignBinding(bindings, 2, 1, relay(1), pins, 3, &d, 1) != Result::ResourceInUse) return false;
    if (!sameResource(bindings[1].source, native(Mode::BinaryOutput, 2).source)) return false;
    if (assignBinding(bindings, 2, 2, relay(2), pins, 3, &d, 1) != Result::InvalidSwitchId) return false;
    if (assignBinding(bindings, 2, 1, relay(2), pins, 3, &d, 1) != Result::Ok) return false;
    return bindings[1].source.channel == 2 && d.state == State::Unknown;
}
static_assert(allocationChecks(), "Offline configured bindings retained; duplicates and invalid IDs do not mutate");

constexpr bool unknownAddressAndReadOnlyAllocation() {
    Binding binding; Device d = pcf(); Binding next = relay(); next.source.address = 0x21;
    if (assignBinding(&binding, 1, 0, next, pins, 3, &d, 1) != Result::MissingDevice) return false;
    if (assignBinding(&binding, 1, 0, native(Mode::AnalogInput, 256), pins, 3, &d, 1) != Result::ReadOnly) return false;
    if (assignBinding(&binding, 1, 0, native(Mode::AnalogInput, 256, false), pins, 3, &d, 1) != Result::Ok) return false;
    return assignBinding(&binding, 1, 0, Binding{}, pins, 3, &d, 1) == Result::Ok && binding.source.kind == Kind::None;
}
static_assert(unknownAddressAndReadOnlyAllocation(), "Unknown device not auto-typed; ADC readonly; explicit unbind");

constexpr bool sampleChecks() {
    Device d{Kind::I2cAdc, 0, 0x48, 4, Adc}; d.state = State::Ready; ValueCache cache;
    if (recordSample(d, cache, 2, 2, 0.0, 1) != Result::Ok || !cache.valid || cache.value != 0.0) return false;
    if (recordSample(d, cache, 2, 1, 123, 2) != Result::IoFailure || cache.valid) return false;
    return d.state == State::Fault && d.lastError == 0xfe && d.lastSuccessMs == 1;
}
static_assert(sampleChecks(), "Zero volts valid only with successful complete sample; short read faults");

constexpr bool invalidSample() {
    Device d{Kind::I2cAdc, 0, 0x48, 4, Adc}; d.state = State::Ready; ValueCache cache;
    return recordSample(d, cache, 2, 2, std::numeric_limits<double>::infinity(), 1) == Result::IoFailure &&
           !cache.valid && d.state == State::Fault && d.lastError == 0xfd;
}
static_assert(invalidSample(), "Nonfinite scaled sample faults without publishing fabricated value");
