#ifndef SKYBADGER_MAPPED_SOURCES_H
#define SKYBADGER_MAPPED_SOURCES_H

#include "SourceMapping.h"
#include "AlpacaRequest.h"
#include "BoardPinMap.h"
#include <math.h>
using SourceMapping::Kind;
using SourceMapping::Mode;
using SourceMapping::State;
using SourceMapping::Result;
constexpr size_t MAX_SOURCE_DEVICES = 8;
enum class SourceDriver : uint8_t { None, Pcf8574, Ads1015, Ads1115, Mcp4725 };
struct SourceProfile { SourceDriver driver = SourceDriver::None; bool confirmed = false; uint8_t safeLevels = 0xff; };
SourceMapping::Device sourceDevices[MAX_SOURCE_DEVICES]{};
SourceProfile sourceProfiles[MAX_SOURCE_DEVICES];
SourceMapping::PcfShadow sourceShadows[MAX_SOURCE_DEVICES];
size_t sourceDeviceCount = 0;
bool detectedAddresses[128]{};
uint32_t sourceInventoryRevision = 1;
bool sourceBusStarted = false;
bool sourceAutoRecover = true;

size_t sourceResidentCount = nativeSourceCount;
static_assert(nativeSourceCount + MAX_CONFIGURABLE_SWITCHES <= MAXSWITCH, "Too many native connectors for this storage budget");

int maximumConfigurableSources() {
    const int available = MAXSWITCH - sourceResidentCount;
    return available < MAX_CONFIGURABLE_SWITCHES ? available : MAX_CONFIGURABLE_SWITCHES;
}
int configuredSourceCount() { return numSwitches - sourceResidentCount; }

const char* sourceStateName(State state) {
    switch (state) {
      case State::Ready: return "Ready";
      case State::Missing: return "Missing";
      case State::Recovering: return "Recovery required";
      case State::Fault: return "I/O fault";
      default: return "Not probed";
    }
}
const char* sourceResultMessage(Result result) {
    switch (result) {
      case Result::Unbound: return "No source assigned";
      case Result::ReadOnly: return "Source is read-only";
      case Result::MissingDevice: return "Configured hardware is unavailable; assignment retained";
      case Result::RecoveringDevice: return "Hardware requires safe initialization and approval in setup";
      case Result::NoValidValue: return "Output/sample state is unknown";
      case Result::StaleValue: return "ADC sample is stale";
      case Result::OutOfRange: return "Sample is outside the configured measurement range";
      case Result::StorageFailure: return "Configuration changed in RAM but could not be saved to flash";
      case Result::ResourceInUse: return "Physical source is already allocated";
      case Result::IoFailure: return "Hardware transaction failed; state is unknown";
      default: return "Invalid source, capability or configuration";
    }
}
String sourceKey(const SourceMapping::SourceRef& source) {
    if (source.kind == Kind::None) return "none";
    if (source.kind == Kind::NativePin) return String("n:") + source.channel;
    return String((unsigned)source.kind) + ":" + source.address + ":" + source.channel;
}
bool parseSourceKey(const String& text, SourceMapping::SourceRef& source) {
    source = {};
    if (text == "none") return true;
    if (text.startsWith("n:")) {
        uint32_t pin;
        if (!parseAlpacaUInt32(text.substring(2).c_str(), pin) || pin > UINT16_MAX) return false;
        source = {Kind::NativePin, 0, 0, (uint16_t)pin}; return true;
    }
    const int a = text.indexOf(':'); const int b = text.indexOf(':', a + 1);
    uint32_t kind, address, channel;
    if (a <= 0 || b <= a || text.indexOf(':', b + 1) >= 0 ||
        !parseAlpacaUInt32(text.substring(0, a).c_str(), kind) ||
        !parseAlpacaUInt32(text.substring(a + 1, b).c_str(), address) ||
        !parseAlpacaUInt32(text.substring(b + 1).c_str(), channel) ||
        kind < 2 || kind > 4 || address < 8 || address > 0x77 || channel > UINT16_MAX) return false;
    source = {(Kind)kind, 0, (uint8_t)address, (uint16_t)channel}; return true;
}
Result resolveMapped(const SwitchEntry& entry, bool write, size_t& slot) {
    auto binding = entry.binding;
    binding.userWritable = entry.writeable;
    return SourceMapping::resolve(binding, nativeSources.data(), nativeSourceCount,
                                  sourceDevices, sourceDeviceCount, write, slot);
}
bool validMappedRange(const SwitchEntry& entry) {
    if (!(isfinite(entry.min) && isfinite(entry.max) && isfinite(entry.step) &&
           isfinite(entry.scale) && isfinite(entry.offset) && entry.scale > 0 &&
           entry.min < entry.max && entry.step > 0 && entry.step <= entry.max - entry.min)) return false;
    const auto mode = entry.binding.mode;
    if (mode == Mode::BinaryInput || mode == Mode::BinaryOutput)
        return entry.min == 0 && entry.max == 1 && entry.step == 1;
    if (mode == Mode::PwmOutput || mode == Mode::AnalogOutput)
        return entry.min >= 0 && entry.max <= (mode == Mode::PwmOutput ? 1024 : 4095) &&
            entry.min == round(entry.min) && entry.max == round(entry.max) && entry.step == round(entry.step);
    return mode == Mode::AnalogInput;
}
Result validateMappedBinding(size_t id, const SwitchEntry& candidate) {
    SourceMapping::Binding bindings[MAXSWITCH];
    for (int i = 0; i < numSwitches; ++i) bindings[i] = switchEntry[i]->binding;
    auto binding = candidate.binding; binding.userWritable = candidate.writeable;
    return SourceMapping::assignBinding(bindings, numSwitches, id, binding,
        nativeSources.data(), nativeSourceCount, sourceDevices, sourceDeviceCount);
}
void invalidateDeviceSamples(size_t slot) {
    sourceShadows[slot].valid = false;
    for (int i = 0; i < numSwitches; ++i) {
        const auto& source = switchEntry[i]->binding.source;
        if (source.kind != Kind::NativePin && source.bus == sourceDevices[slot].bus &&
            source.address == sourceDevices[slot].address) {
                switchEntry[i]->sample.valid = false; switchEntry[i]->sampleError = Result::IoFailure;
            }
    }
}
void mappedFailure(size_t slot, uint8_t error) {
    sourceDevices[slot].fail(millis(), error);
    invalidateDeviceSamples(slot); ++sourceInventoryRevision;
}
bool writeSourceRegister(size_t slot, uint8_t reg, uint16_t value) {
    Wire.beginTransmission(sourceDevices[slot].address);
    const bool buffered = Wire.write(reg) == 1 && Wire.write((uint8_t)(value >> 8)) == 1 &&
                          Wire.write((uint8_t)value) == 1;
    const uint8_t error = Wire.endTransmission();
    if (!buffered || error) { mappedFailure(slot, error ? error : 0xff); return false; }
    return true;
}
bool readSourceRegister(size_t slot, uint8_t reg, uint16_t& value) {
    Wire.beginTransmission(sourceDevices[slot].address);
    const bool buffered = Wire.write(reg) == 1;
    const uint8_t error = Wire.endTransmission();
    if (!buffered || error) { mappedFailure(slot, error ? error : 0xff); return false; }
    if (Wire.requestFrom(sourceDevices[slot].address, (uint8_t)2) != 2) {
        while (Wire.available()) Wire.read();
        mappedFailure(slot, 0xfe); return false;
    }
    value = ((uint16_t)Wire.read() << 8) | (uint8_t)Wire.read(); return true;
}
bool writeDacCode(size_t slot, uint16_t code) {
    if (code > 4095 || sourceProfiles[slot].driver != SourceDriver::Mcp4725) return false;
    Wire.beginTransmission(sourceDevices[slot].address);
    //0x40 writes the volatile DAC register, never the device's EEPROM (0x60).
    const bool buffered = Wire.write((uint8_t)0x40) == 1 && Wire.write((uint8_t)(code >> 4)) == 1 &&
                          Wire.write((uint8_t)(code << 4)) == 1;
    const uint8_t error = Wire.endTransmission();
    if (!buffered || error) { mappedFailure(slot, error ? error : 0xff); return false; }
    return true;
}
Result recoverMappedDevice(size_t slot, bool approved) {
    if (slot >= sourceDeviceCount) return Result::InvalidSource;
    auto& device = sourceDevices[slot];
    if (!sourceProfiles[slot].confirmed || device.state != State::Recovering || !approved)
        return Result::RecoveringDevice;
    if (device.kind == Kind::Pcf8574) {
        uint8_t safe = sourceProfiles[slot].safeLevels;
        for (int i = 0; i < numSwitches; ++i) {
            const auto& b = switchEntry[i]->binding;
            if (b.source.kind == Kind::Pcf8574 && b.source.address == device.address &&
                b.source.channel < 8 && b.mode == Mode::BinaryOutput && !b.activeLow)
                safe &= ~(1u << b.source.channel);
            if (b.source.kind == Kind::Pcf8574 && b.source.address == device.address &&
                b.source.channel < 8 && b.mode == Mode::BinaryInput) safe |= 1u << b.source.channel;
        }
        const Result result = SourceMapping::recoverPcf8574(Wire, device, safe, true,
            approved, sourceShadows[slot], millis());
        if (result != Result::Ok) { invalidateDeviceSamples(slot); return result; }
        for (int i = 0; i < numSwitches; ++i) {
            auto& entry = *switchEntry[i];
            if (entry.binding.source.kind == Kind::Pcf8574 && entry.binding.source.address == device.address &&
                entry.binding.mode == Mode::BinaryOutput) {
                entry.value = 0; entry.sample = {0, millis(), device.generation, true};
            }
        }
    } else if (device.kind == Kind::I2cAdc) {
        uint16_t config;
        if (!readSourceRegister(slot, 1, config)) return Result::IoFailure;
        //Saved/explicit profile supplies ADS1015 vs ADS1115: no identification register.
        device.completeRecovery(millis(), true, true);
    } else if (device.kind == Kind::I2cDac) {
        if (!writeDacCode(slot, 0)) return Result::IoFailure;
        device.completeRecovery(millis(), true, true);
        for (int i = 0; i < numSwitches; ++i) {
            auto& e = *switchEntry[i];
            if (e.binding.source.kind == Kind::I2cDac && e.binding.source.address == device.address) {
                e.sample = {0, millis(), device.generation, e.min == 0}; e.value = 0;
            }
        }
    } else return Result::InvalidSource;
    ++sourceInventoryRevision;
    return Result::Ok;
}
void initializeNativeSource(SwitchEntry& entry) {
    entry.sample.valid = false;
    if (entry.binding.source.kind != Kind::NativePin) return;
    size_t slot;
    if (resolveMapped(entry, false, slot) != Result::Ok) return;
    const int pin = entry.binding.source.channel;
    if (entry.binding.mode == Mode::BinaryOutput) {
        digitalWrite(pin, entry.binding.activeLow ? HIGH : LOW); pinMode(pin, OUTPUT);
        entry.value = 0; entry.sample = {0, millis(), 0, true};
    } else if (entry.binding.mode == Mode::PwmOutput) {
        digitalWrite(pin, entry.binding.activeLow ? HIGH : LOW); pinMode(pin, OUTPUT);
        analogWriteRange(1024); analogWrite(pin, entry.binding.activeLow ? 1024 : 0);
        entry.value = 0; entry.sample = {0, millis(), 0, entry.min == 0};
    } else if (entry.binding.mode == Mode::BinaryInput) pinMode(pin, INPUT);
}
void releaseMappedSource(SwitchEntry& entry) {
    entry.sample.valid = false;
    if (entry.binding.source.kind == Kind::NativePin) {
        size_t slot; if (resolveMapped(entry, false, slot) != Result::Ok) return;
        const int pin = entry.binding.source.channel;
        if (entry.binding.mode == Mode::PwmOutput) analogWrite(pin, 0);
        if (SourceMapping::isOutput(entry.binding.mode)) {
            digitalWrite(pin, entry.binding.activeLow ? HIGH : LOW); pinMode(pin, INPUT);
        }
    }
}
//Configuration-only safety operation. It can deactivate a read-only output, but
//cannot apply arbitrary values. HTTP configuration authorization is checked above it.
Result quiesceMappedSource(int id) {
    auto& e = *switchEntry[id]; size_t slot;
    if (!SourceMapping::isOutput(e.binding.mode)) return Result::Ok;
    if (resolveMapped(e, false, slot) != Result::Ok) { e.sample.valid = false; return Result::Ok; }
    if (e.binding.source.kind == Kind::Pcf8574)
        return SourceMapping::writePcf8574Bit(Wire, sourceDevices[slot], e.binding.source.channel, false,
            e.binding.activeLow, sourceShadows[slot], e.sample, millis());
    if (e.binding.source.kind == Kind::I2cDac) {
        if (!writeDacCode(slot, 0)) return Result::IoFailure;
        sourceDevices[slot].operationSucceeded(millis());
    } else if (e.binding.source.kind == Kind::NativePin) {
        if (e.binding.mode == Mode::PwmOutput) { analogWriteRange(1024); analogWrite(e.binding.source.channel, e.binding.activeLow ? 1024 : 0); }
        else digitalWrite(e.binding.source.channel, e.binding.activeLow ? HIGH : LOW);
    }
    e.sample = {0, millis(), slot < sourceDeviceCount ? sourceDevices[slot].generation : 0, e.min == 0};
    e.sampleError = Result::NoValidValue; return Result::Ok;
}
Result writeMappedValue(int id, double value) {
    if (id < 0 || id >= numSwitches) return Result::InvalidSwitchId;
    auto& entry = *switchEntry[id]; size_t slot;
    const Result resolved = resolveMapped(entry, true, slot);
    if (resolved != Result::Ok) return resolved;
    if (!validMappedRange(entry) || !isfinite(value) || value < entry.min || value > entry.max ||
        fabs((value - entry.min) / entry.step - round((value - entry.min) / entry.step)) > 0.00001)
        return Result::InvalidSource;
    if (entry.binding.mode == Mode::BinaryOutput && value != 0 && value != 1) return Result::InvalidSource;
    if (entry.binding.source.kind == Kind::Pcf8574) {
        const auto result = SourceMapping::writePcf8574Bit(Wire, sourceDevices[slot],
            entry.binding.source.channel, value == 1, entry.binding.activeLow,
            sourceShadows[slot], entry.sample, millis());
        if (result != Result::Ok) { invalidateDeviceSamples(slot); ++sourceInventoryRevision; return result; }
    } else if (entry.binding.source.kind == Kind::NativePin) {
        if (entry.binding.mode == Mode::BinaryOutput)
            digitalWrite(entry.binding.source.channel, (value == 1) != entry.binding.activeLow ? HIGH : LOW);
        else if (entry.binding.mode == Mode::PwmOutput) {
            if (value != round(value) || entry.min < 0 || entry.max > 1024) return Result::InvalidSource;
            analogWriteRange(1024); analogWrite(entry.binding.source.channel, entry.binding.activeLow ? 1024 - value : value);
        } else return Result::ReadOnly;
        entry.sample = {value, millis(), 0, true};
    } else if (entry.binding.source.kind == Kind::I2cDac) {
        if (value != round(value) || entry.min < 0 || entry.max > 4095) return Result::InvalidSource;
        if (!writeDacCode(slot, (uint16_t)value)) return Result::IoFailure;
        sourceDevices[slot].operationSucceeded(millis());
        entry.sample = {value, millis(), sourceDevices[slot].generation, true};
    } else return Result::InvalidSource;
    entry.value = value; entry.sampleError = Result::Ok; return Result::Ok;
}
Result readMappedValue(int id, double& value) {
    if (id < 0 || id >= numSwitches) return Result::InvalidSwitchId;
    auto& entry = *switchEntry[id]; size_t slot;
    const Result resolved = resolveMapped(entry, false, slot);
    if (resolved != Result::Ok) return resolved;
    if (entry.binding.source.kind == Kind::NativePin && !SourceMapping::isOutput(entry.binding.mode)) {
        if (entry.binding.mode != Mode::AnalogInput || !entry.acquisitionStarted ||
            (uint32_t)(millis() - entry.lastAcquisitionMs) >= 100) {
            const double value = entry.binding.mode == Mode::AnalogInput ? analogRead(A0) / 1023.0 * entry.scale + entry.offset :
                ((digitalRead(entry.binding.source.channel) != 0) != entry.binding.activeLow ? 1.0 : 0.0);
            entry.lastAcquisitionMs = millis(); entry.acquisitionStarted = true;
            if (!isfinite(value) || value < entry.min || value > entry.max) {
                entry.sample.valid = false; entry.sampleError = Result::OutOfRange;
            } else { entry.sample = {value, millis(), 0, true}; entry.sampleError = Result::Ok; }
        }
    } else if (entry.binding.source.kind == Kind::Pcf8574 && entry.binding.mode == Mode::BinaryInput) {
        if (Wire.requestFrom(sourceDevices[slot].address, (uint8_t)1) != 1) { mappedFailure(slot, 0xfe); return Result::IoFailure; }
        const bool physical = ((uint8_t)Wire.read() & (1u << entry.binding.source.channel)) != 0;
        entry.sample = {physical != entry.binding.activeLow ? 1.0 : 0.0, millis(), sourceDevices[slot].generation, true};
    }
    if (!entry.sample.valid) return entry.sampleError;
    const auto result = SourceMapping::cachedValue(entry.sample,
        slot < sourceDeviceCount ? &sourceDevices[slot] : nullptr, millis(),
        entry.binding.mode == Mode::AnalogInput ? 5000 : UINT32_MAX, value);
    if (result == Result::Ok) entry.value = value;
    return result;
}

int pendingAdcId = -1;
size_t pendingAdcSlot = 0;
uint32_t pendingAdcAt = 0;
void serviceMappedSources() {
    if (!sourceBusStarted) return;
    static uint32_t discoveryAt = 0; static uint8_t scanAddress = 8;
    if ((uint32_t)(millis() - discoveryAt) >= 100) {
        discoveryAt = millis(); Wire.beginTransmission(scanAddress);
        const bool seen = Wire.endTransmission() == 0;
        if (detectedAddresses[scanAddress] != seen) { detectedAddresses[scanAddress] = seen; ++sourceInventoryRevision; }
        if (++scanAddress > 0x77) scanAddress = 8;
    }
    for (size_t i = 0; i < sourceDeviceCount; ++i) {
        auto& device = sourceDevices[i];
        if (!device.probeDue(millis(), device.state == State::Ready ? 1000 : 5000)) continue;
        Wire.beginTransmission(device.address); const uint8_t error = Wire.endTransmission();
        const State previous = device.state;
        device.observeProbe(millis(), error == 0, error);
        if (error) invalidateDeviceSamples(i);
        if (previous != device.state) ++sourceInventoryRevision;
        if (device.state == State::Recovering && (sourceAutoRecover || device.kind == Kind::I2cAdc)) recoverMappedDevice(i, true);
        break;
    }
    if (pendingAdcId >= 0) {
        if (pendingAdcId >= numSwitches || sourceDevices[pendingAdcSlot].state != State::Ready) { pendingAdcId = -1; return; }
        if ((uint32_t)(millis() - pendingAdcAt) < 10) return;
        uint16_t config, raw;
        if (!readSourceRegister(pendingAdcSlot, 1, config)) { pendingAdcId = -1; return; }
        if (!(config & 0x8000)) {
            if ((uint32_t)(millis() - pendingAdcAt) > 200) { mappedFailure(pendingAdcSlot, 0xfc); pendingAdcId = -1; }
            return;
        }
        if (readSourceRegister(pendingAdcSlot, 0, raw)) {
            const int16_t signedRaw = (int16_t)raw;
            auto& entry = *switchEntry[pendingAdcId];
            const bool ads1015 = sourceProfiles[pendingAdcSlot].driver == SourceDriver::Ads1015;
            const int counts = ads1015 ? (signedRaw / 16) : signedRaw;
            const double lsb = ads1015 ? 0.003 : 0.0001875;
            const double volts = counts * lsb;
            debugI("ADC reading for channel %d is %d using gain factor %f and output %f\n",
                (int)entry.binding.source.channel, counts, lsb, volts);
            const double calibrated = volts * entry.scale + entry.offset;
            if (calibrated < entry.min || calibrated > entry.max) {
                entry.sample.valid = false; entry.sampleError = Result::OutOfRange;
            } else entry.sampleError = SourceMapping::recordSample(sourceDevices[pendingAdcSlot], entry.sample, 2, 2,
                calibrated, millis());
        }
        pendingAdcId = -1; return;
    }
    static int cursor = 0;
    if (!numSwitches) return;
    cursor %= numSwitches; const int id = cursor++;
    auto& entry = *switchEntry[id];
    if (entry.binding.source.kind != Kind::I2cAdc ||
        (entry.acquisitionStarted && (uint32_t)(millis() - entry.lastAcquisitionMs) < 500)) return;
    size_t slot;
    if (resolveMapped(entry, false, slot) != Result::Ok) return;
    const uint16_t config = 0x8000 | ((4 + entry.binding.source.channel) << 12) | 0x0100 | 0x0080 | 0x0003;
    if (writeSourceRegister(slot, 1, config)) {
        pendingAdcId = id; pendingAdcSlot = slot; pendingAdcAt = millis();
        entry.acquisitionStarted = true; entry.lastAcquisitionMs = millis();
    }
}

#endif
