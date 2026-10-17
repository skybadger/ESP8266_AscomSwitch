//Executes actual runtime, storage and setup functions with simulated hardware.
#include "fakes/ArduinoMapping.h"
#include "../Webrelay_common.h"
int numSwitches = 0;
SwitchEntry** switchEntry = nullptr;
#include "../MappedSources.h"
#ifdef _MSC_VER
#define __attribute__(x)
#pragma pack(push, 1)
#endif
#include "../MappingStorage.h"
#ifdef _MSC_VER
#pragma pack(pop)
#endif
#include "../MappingUi.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
void configure(int id, const char* key, int mode, bool writable = true, bool activeLow = true) {
    server.values = {{"switchId", String(id)}, {"revision", String(sourceInventoryRevision)},
        {"source", key}, {"mode", String(mode)}, {"name", "Test <switch>"}, {"description", "A & B"},
        {"min", "0"}, {"max", mode == 2 ? "1024" : mode == 3 ? "6.144" : mode == 4 ? "4095" : "1"},
        {"step", mode == 3 ? "0.003" : "1"}, {"scale", "1"}, {"offset", "0"},
        {"confirmChange", "true"}, {"activeLow", activeLow ? "true" : "false"},
        {"writeable", writable ? "true" : "false"}};
    handlerMappedSetupSwitches();
}
int externalId(int offset) { return sourceResidentCount + offset; }
int main() {
    try {
        setDefaults(); sourceBusStarted = true; sourceAutoRecover = false;
        check(numSwitches == (int)sourceResidentCount + 4, "Defaults allocated");
        check(addSourceProfile(SourceDriver::Pcf8574, 0x20, true, 0xff), "PCF profile");
        check(!addSourceProfile(SourceDriver::Pcf8574, 0x20, true), "Duplicate device rejected");
        check(!addSourceProfile(SourceDriver::Pcf8574, 160, true), "Invalid address rejected");
        configure(externalId(0), "2:32:5", 1);
        check(lastUiStatus == 200, lastUiError.c_str());
        check(sourceDevices[0].state == State::Recovering, "Assignment requires recovery");
        check(writeMappedValue(externalId(0), 1) != Result::Ok, "Cannot write before recovery");
        sourceDevices[0].observeProbe(10, true, 0);
        check(recoverMappedDevice(0, true) == Result::Ok, "Approved recovery");
        check(writeMappedValue(externalId(0), 1) == Result::Ok && Wire.lastValue == 0xdf, "Mapped P5, not switch ID");
        configure(externalId(1), "2:32:5", 1);
        check(lastUiStatus == 400 && switchEntry[externalId(1)]->binding.source.kind == Kind::None, "Duplicate allocation has no mutation");
        configure(0, "none", 1);
        check(lastUiStatus == 400 && switchEntry[0]->binding.source.kind == Kind::NativePin, "Native connector cannot disappear");
        configure(0, "n:3", 3, false);
        check(lastUiStatus == 400 && switchEntry[0]->binding.mode == Mode::BinaryInput, "GPIO cannot pretend to be ADC");
        configure(externalId(1), "n:3", 1);
        check(lastUiStatus == 400 && switchEntry[externalId(1)]->binding.source.kind == Kind::None, "Fixed connector cannot be double allocated");
        double value = 99;
        check(readMappedValue(externalId(0), value) == Result::Ok && value == 1, "Logical read cache");
        Wire.error = 2;
        check(writeMappedValue(externalId(0), 0) == Result::IoFailure, "Real dispatcher write failure");
        check(!switchEntry[externalId(0)]->sample.valid && !sourceShadows[0].valid, "Failure invalidates output state");
        check(readMappedValue(externalId(0), value) != Result::Ok, "Unavailable state not returned as zero");
        Wire.error = 0; sourceDevices[0].observeProbe(20, true, 0);
        check(writeMappedValue(externalId(0), 1) != Result::Ok, "ACK does not resume output");
        check(recoverMappedDevice(0, true) == Result::Ok, "Return safely initialized");
        check(readMappedValue(externalId(0), value) == Result::Ok && value == 0, "Old ON command not replayed");
        configure(0, "n:3", 2, true, false);
        check(lastUiStatus == 200 && pinLevels[3] == 0, "Native PWM starts inactive");
        check(writeMappedValue(0, 512) == Result::Ok && pinLevels[3] == 512 && pwmRange == 1024, "PWM backend");
        check(writeMappedValue(0, 512.5) != Result::Ok && pinLevels[3] == 512, "Invalid PWM unchanged");
        check(addSourceProfile(SourceDriver::Ads1015, 0x48, true), "ADC profile");
        check(sourceResidentCount == nativeSourceCount + 4 && configuredSourceCount() == 4 &&
              switchEntry[externalId(0)]->binding.source.channel == 5, "ADC prefix insertion preserves external physical bindings");
        configure(nativeSourceCount, "3:72:0", 3, false, false);
        check(lastUiStatus == 200 && !switchEntry[nativeSourceCount]->writeable, "ADC readonly mapping");
        sourceDevices[1].observeProbe(0, true, 0);
        check(recoverMappedDevice(1, true) == Result::Ok, "ADC checked initialization");
        check(writeMappedValue(nativeSourceCount, 1) == Result::ReadOnly, "ADC cannot write");
        fakeNow = 100;
        for (int i = 0; i < 4 && pendingAdcId < 0; ++i) serviceMappedSources();
        check(pendingAdcId == (int)nativeSourceCount, "ADC conversion scheduled");
        fakeNow += 10; serviceMappedSources();
        check(readMappedValue(nativeSourceCount, value) == Result::Ok && fabs(value - 0.768) < 0.00001, "Signed checked ADS1015 sample");
        Wire.shortRead = true; uint16_t raw;
        check(!readSourceRegister(1, 0, raw) && sourceDevices[1].state == State::Fault, "Short actual bus read faults device");
        check(readMappedValue(nativeSourceCount, value) != Result::Ok, "Failed ADC cannot return stale voltage"); Wire.shortRead = false;
        check(addSourceProfile(SourceDriver::Mcp4725, 0x60, true), "MCP4725 profile");
        configure(externalId(3), "4:96:0", 4, true, false);
        check(lastUiStatus == 200, "DAC source mapping");
        sourceDevices[2].observeProbe(0, true, 0);
        check(recoverMappedDevice(2, true) == Result::Ok && Wire.sent[0] == 0x40 && Wire.sent[1] == 0 && Wire.sent[2] == 0,
              "DAC inactive volatile initialization");
        check(writeMappedValue(externalId(3), 2048) == Result::Ok && Wire.sent.size() == 3 && Wire.sent[0] == 0x40 &&
              Wire.sent[1] == 0x80 && Wire.sent[2] == 0, "MCP4725 code and no EEPROM command");
        check(writeMappedValue(externalId(3), 4096) != Result::Ok && switchEntry[externalId(3)]->sample.value == 2048, "DAC bounds preserve state");
        Wire.error = 3;
        check(writeMappedValue(externalId(3), 0) == Result::IoFailure && !switchEntry[externalId(3)]->sample.valid, "DAC NACK invalidates channel");
        Wire.error = 0; sourceAutoRecover = true; fakeNow = 6000; serviceMappedSources(); serviceMappedSources(); serviceMappedSources();
        check(sourceDevices[2].state == State::Ready && readMappedValue(externalId(3), value) == Result::Ok && value == 0,
              "Automatic DAC recovery writes inactive, not old command");
        saveToEeprom(); check(sourceStorageValid, "CRC record saved");
        sourceBusStarted = false; allocateMappedSwitches(0); sourceDeviceCount = 0;
        setupFromEeprom();
        check(numSwitches == (int)sourceResidentCount + 4 && sourceDeviceCount == 3 && sourceStorageValid, "Versioned profile round trip");
        check(switchEntry[externalId(0)]->binding.source.channel == 5 && sourceProfiles[0].safeLevels == 0xff, "Physical binding round trip");
        check(!switchEntry[externalId(0)]->sample.valid && sourceDevices[0].state == State::Unknown, "Output state is not persisted");
        EEPROM.bytes[200] ^= 1;
        setupFromEeprom();
        check(!sourceStorageValid && switchEntry[externalId(0)]->binding.source.kind == Kind::None, "Corrupt CRC stays unassigned");
        check(addSourceProfile(SourceDriver::Pcf8574, 0x20, true, 0xff), "Reset profile");
        configure(externalId(0), "2:32:5", 1); sourceDevices[0].state = State::Missing;
        check(addSourceProfile(SourceDriver::Mcp4725, 0x60, true), "DAC choice available to setup UI");
        String page; mappedInventoryForm(page);
        String form; mappedSwitchForm(form, 0); page += form;
        mappedSwitchForm(form, externalId(0)); page += form;
        check(std::string(page.c_str()).find("&lt;switch&gt;") != std::string::npos, "UI escapes names");
        check(std::string(page.c_str()).find("Missing") != std::string::npos, "UI retains selected offline source");
        mappedSwitchForm(form, externalId(1)); page += form;
        check(std::string(form.c_str()).find("value=\"2:32:5\" disabled") != std::string::npos, "Allocated option disabled");
        std::ofstream(".pio/mapped-ui-fragment.html") << page.c_str();
        //The legacy byte layout uses explicit native 32-bit integers/floats, not struct dumps.
        static_assert(sizeof(int) == 4 && sizeof(float) == 4 && sizeof(bool) == 1, "Legacy fixture widths");
        EEPROM.bytes.fill(0xff); EEPROM.bytes[0] = '*';
        int legacyCount = 2, legacyPort = 32227; EEPROM.put(4, legacyCount); EEPROM.put(8, legacyPort);
        char host[40] = "legacy-host", location[40] = "legacy-location";
        std::memcpy(EEPROM.bytes.data() + 12, host, 40); std::memcpy(EEPROM.bytes.data() + 52, location, 40);
        size_t at = 92;
        for (int id = 0; id < 2; ++id) {
            int type = id == 0 ? 0 : 2, pin = id == 0 ? -1 : 3; bool writable = true;
            float minimum = 0, maximum = id == 0 ? 1.0f : 1024.0f, step = 1, oldOnValue = 1;
            EEPROM.put(at, type); at += 4; EEPROM.put(at, pin); at += 4; EEPROM.put(at, writable); ++at;
            EEPROM.put(at, minimum); at += 4; EEPROM.put(at, maximum); at += 4;
            EEPROM.put(at, step); at += 4; EEPROM.put(at, oldOnValue); at += 4;
            char name[40] = "legacy-channel", description[40] = "legacy-description";
            std::memcpy(EEPROM.bytes.data() + at, name, 40); at += 40;
            std::memcpy(EEPROM.bytes.data() + at, description, 40); at += 40;
        }
        sourceBusStarted = false; setupFromEeprom();
        check(numSwitches == (int)sourceResidentCount + 2 && std::string(myHostname) == "legacy-host", "Legacy count and hostname migration");
        check(std::string(switchEntry[externalId(0)]->switchName) == "legacy-channel", "Legacy names migrated");
        check(switchEntry[externalId(0)]->binding.source.kind == Kind::None && sourceDeviceCount == 0,
              "Legacy invalid expander address never guessed");
        check(switchEntry[0]->binding.source.kind == Kind::NativePin && !switchEntry[0]->sample.valid,
              "Legacy permitted PWM pin migrated without replay");
        saveToEeprom(); check(sourceStorageValid, "Migrated schema saved with CRC");
        EEPROM.succeeds = false; saveToEeprom(); check(!sourceStorageValid, "Storage failure reported");
        EEPROM.succeeds = true;
        check(addSourceProfile(SourceDriver::Ads1115, 0x49, true), "Resident ADS1115 profile");
        check(allocateMappedSwitches(sourceResidentCount), "Zero configurable channels accepted");
        check(configuredSourceCount() == 0 && numSwitches == (int)nativeSourceCount + 4 &&
              switchEntry[nativeSourceCount]->binding.source.kind == Kind::I2cAdc,
              "All native and ADC residents survive external count zero");
        sourceDevices[0].state = State::Missing;
        check(switchEntry[nativeSourceCount]->binding.mode == Mode::AnalogInput && !switchEntry[nativeSourceCount]->writeable,
              "Missing ADC remains enumerable and read-only");
        sourceBusStarted = true; Wire.rawAdc = 0xfff1; fakeNow = 12000;
        for (int i = 0; i < 16; ++i) { fakeNow += 10; serviceMappedSources(); }
        check(readMappedValue(nativeSourceCount, value) == Result::Ok && fabs(value + 0.0028125) < 0.0000001,
              "ADS1115 uses signed 16-bit samples and correct LSB");
        switchEntry[nativeSourceCount]->min = 0; switchEntry[nativeSourceCount]->acquisitionStarted = false;
        for (int i = 0; i < 16; ++i) { fakeNow += 10; serviceMappedSources(); }
        check(readMappedValue(nativeSourceCount, value) == Result::OutOfRange && sourceDevices[0].state == State::Ready,
              "Out-of-range sensor value rejected without claiming the bus device disappeared");
        std::cout << checks << " mapping runtime checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << "\n"; return 1; }
}
