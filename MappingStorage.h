#ifndef SKYBADGER_MAPPING_STORAGE_H
#define SKYBADGER_MAPPING_STORAGE_H
#include <EEPROM.h>
#include <new>
struct __attribute__((packed)) StoredSourceChannel {
    uint8_t kind, mode, bus, address, flags, type;
    uint16_t channel;
    float minimum, maximum, step, scale, offset;
    char name[40], description[40];
};
struct __attribute__((packed)) StoredSourceDevice { uint8_t driver, address, confirmed, safeLevels; };
struct __attribute__((packed)) StoredSourceConfig {
    uint32_t signature; uint16_t version, length; uint32_t crc, boardTag;
    uint8_t count, devices, autoRecover, reserved; uint16_t udp;
    char hostname[40], location[40];
    StoredSourceDevice profiles[MAX_SOURCE_DEVICES];
    StoredSourceChannel channels[MAXSWITCH];
};
static_assert(sizeof(StoredSourceChannel) == 108, "Stable channel schema");
static_assert(sizeof(StoredSourceConfig) < 4096, "Fits EEPROM sector");
constexpr uint32_t SOURCE_CONFIG_MAGIC = 0x334d5353;
StoredSourceConfig sourceConfigBuffer;
bool sourceStorageValid = false;
String sourceStorageMessage;
void nativeResidentDefaults(size_t index);
uint32_t sourceConfigCrc(const uint8_t* bytes, size_t length) {
    uint32_t crc = 0xffffffff;
    while (length--) { crc ^= *bytes++; for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u))); }
    return ~crc;
}
void copyConfigString(char* target, const char* source) {
    strncpy(target, source ? source : "", MAX_NAME_LENGTH - 1); target[MAX_NAME_LENGTH - 1] = 0;
}
SwitchEntry* createMappedSwitch() {
    auto* e = new (std::nothrow) SwitchEntry{}; if (!e) return nullptr;
    e->switchName = (char*)calloc(40, 1); e->description = (char*)calloc(40, 1);
    if (!e->switchName || !e->description) { free(e->switchName); free(e->description); delete e; return nullptr; }
    copyConfigString(e->switchName, "Unassigned switch"); copyConfigString(e->description, "Select a source in setup"); return e;
}
void destroyMappedSwitch(SwitchEntry* e) { if (e) { free(e->switchName); free(e->description); delete e; } }
bool allocateMappedSwitches(int count) {
    if (count < 0 || count > MAXSWITCH) return false;
    if (!switchEntry) switchEntry = (SwitchEntry**)calloc(MAXSWITCH, sizeof(SwitchEntry*));
    if (!switchEntry) return false;
    for (int i = numSwitches; i < count; ++i) {
        switchEntry[i] = createMappedSwitch();
        if (!switchEntry[i]) { for (int j = numSwitches; j < i; ++j) { destroyMappedSwitch(switchEntry[j]); switchEntry[j] = nullptr; } return false; }
    }
    for (int i = count; i < numSwitches; ++i) {
        if (sourceBusStarted && SourceMapping::isOutput(switchEntry[i]->binding.mode)) {
            if (quiesceMappedSource(i) != Result::Ok) return false;
        }
    }
    for (int i = count; i < numSwitches; ++i) { if (sourceBusStarted) releaseMappedSource(*switchEntry[i]); destroyMappedSwitch(switchEntry[i]); switchEntry[i] = nullptr; }
    numSwitches = count; return true;
}
void setDefaults() {
    if (!myHostname) myHostname = (char*)calloc(40, 1);
    if (!thisID) thisID = (char*)calloc(40, 1);
    if (!Location) Location = (char*)calloc(40, 1);
    if (!myHostname || !thisID || !Location) return;
    copyConfigString(myHostname, defaultHostname); copyConfigString(thisID, myHostname); copyConfigString(Location, "Default Location");
    sourceResidentCount = nativeSourceCount;
    udpPort = ALPACA_DISCOVERY_PORT; allocateMappedSwitches(sourceResidentCount + defaultNumSwitches);
    for (int i = 0; i < numSwitches; ++i) {
        switchEntry[i]->binding = {}; switchEntry[i]->sample = {}; switchEntry[i]->min = 0;
        switchEntry[i]->max = 1; switchEntry[i]->step = 1; switchEntry[i]->scale = 1; switchEntry[i]->offset = 0;
    }
    sourceDeviceCount = 0; sourceStorageValid = false; sourceAutoRecover = true;
    for (size_t i = 0; i < nativeSourceCount; ++i) nativeResidentDefaults(i);
}
void nativeResidentDefaults(size_t index) {
    auto& e = *switchEntry[index]; const auto& pin = nativeSources[index];
    const Mode mode = pin.capabilities & SourceMapping::Adc ? Mode::AnalogInput :
        pin.capabilities & SourceMapping::DigitalIn ? Mode::BinaryInput :
        pin.capabilities & SourceMapping::DigitalOut ? Mode::BinaryOutput : Mode::PwmOutput;
    e.binding = {{Kind::NativePin, 0, 0, pin.id}, mode, SourceMapping::isOutput(mode), pin.startupActiveLow};
    e.writeable = e.binding.userWritable; e.min = 0; e.max = mode == Mode::PwmOutput ? 1024 : 1;
    e.step = mode == Mode::AnalogInput ? 1.0f / 1023 : 1;
    e.scale = 1; e.offset = 0; e.sample = {};
    e.type = mode == Mode::AnalogInput ? SWITCH_ADC : mode == Mode::PwmOutput ? SWITCH_PWM : SWITCH_RELAY_NO;
    copyConfigString(e.switchName, pin.label); copyConfigString(e.description, "Assembly-defined persistent native connector");
}
void adcResidentDefaults(SwitchEntry& e, size_t slot, uint8_t channel) {
    const bool ads1015 = sourceProfiles[slot].driver == SourceDriver::Ads1015;
    e.binding = {{Kind::I2cAdc, 0, sourceDevices[slot].address, channel}, Mode::AnalogInput, false, false};
    e.writeable = false; e.type = SWITCH_ADC; e.scale = 1; e.offset = 0;
    e.min = -6.144f; e.max = ads1015 ? 6.141f : 6.1438125f;
    e.step = ads1015 ? 0.003f : 0.0001875f; e.sample = {};
    String name = String(ads1015 ? "ADS1015 0x" : "ADS1115 0x") + String(sourceDevices[slot].address, HEX) + " AIN" + channel;
    copyConfigString(e.switchName, name.c_str()); copyConfigString(e.description, "Resident external ADC input, volts");
}
bool insertAdcResidents(size_t slot) {
    if (numSwitches + 4 > MAXSWITCH) return false;
    SwitchEntry* fresh[4]{};
    for (int i = 0; i < 4; ++i) if (!(fresh[i] = createMappedSwitch())) {
        for (auto* e : fresh) destroyMappedSwitch(e); return false;
    }
    pendingAdcId = -1;
    for (int i = numSwitches - 1; i >= (int)sourceResidentCount; --i) switchEntry[i + 4] = switchEntry[i];
    for (int channel = 0; channel < 4; ++channel) {
        switchEntry[sourceResidentCount + channel] = fresh[channel]; adcResidentDefaults(*fresh[channel], slot, channel);
    }
    sourceResidentCount += 4; numSwitches += 4; return true;
}
void enforceResidentBindings() {
    for (size_t i = 0; i < nativeSourceCount; ++i) {
        if (switchEntry[i]->binding.source.kind != Kind::NativePin || switchEntry[i]->binding.source.channel != nativeSources[i].id)
            nativeResidentDefaults(i);
    }
    size_t id = nativeSourceCount;
    for (size_t slot = 0; slot < sourceDeviceCount; ++slot) if (sourceDevices[slot].kind == Kind::I2cAdc)
        for (uint8_t channel = 0; channel < 4; ++channel, ++id) {
            auto& e = *switchEntry[id];
            if (e.binding.source.kind != Kind::I2cAdc || e.binding.source.address != sourceDevices[slot].address || e.binding.source.channel != channel)
                adcResidentDefaults(e, slot, channel);
            e.binding.mode = Mode::AnalogInput; e.binding.userWritable = false; e.binding.activeLow = false;
            e.writeable = false; e.type = SWITCH_ADC;
        }
}
bool addSourceProfile(SourceDriver driver, uint8_t address, bool confirmed, uint8_t safeLevels = 0xff, bool materialize = true) {
    if (sourceDeviceCount >= MAX_SOURCE_DEVICES || address < 8 || address > 0x77 ||
        driver < SourceDriver::Pcf8574 || driver > SourceDriver::Mcp4725) return false;
    for (size_t i = 0; i < sourceDeviceCount; ++i) if (sourceDevices[i].address == address) return false;
    bool pcf = driver == SourceDriver::Pcf8574, dac = driver == SourceDriver::Mcp4725;
    if (pcf && !((address >= 0x20 && address <= 0x27) || (address >= 0x38 && address <= 0x3f))) return false;
    if (!pcf && !dac && (address < 0x48 || address > 0x4b)) return false;
    if (dac && (address < 0x60 || address > 0x67)) return false;
    sourceDevices[sourceDeviceCount] = {pcf ? Kind::Pcf8574 : dac ? Kind::I2cDac : Kind::I2cAdc, 0, address,
        (uint8_t)(pcf ? 8 : dac ? 1 : 4), (uint8_t)(pcf ? SourceMapping::DigitalIn | SourceMapping::DigitalOut : dac ? SourceMapping::Dac : SourceMapping::Adc)};
    sourceProfiles[sourceDeviceCount] = {driver, confirmed, safeLevels};
    if (!pcf && !dac && materialize && !insertAdcResidents(sourceDeviceCount)) return false;
    ++sourceDeviceCount; ++sourceInventoryRevision; return true;
}
void saveToEeprom() {
    auto& d = sourceConfigBuffer; memset(&d, 0, sizeof(d));
    d.signature = SOURCE_CONFIG_MAGIC; d.version = 3; d.length = sizeof(d); d.boardTag = boardPinMapTag(); d.count = numSwitches;
    d.devices = sourceDeviceCount; d.autoRecover = sourceAutoRecover; d.udp = udpPort;
    copyConfigString(d.hostname, myHostname); copyConfigString(d.location, Location);
    for (size_t i = 0; i < sourceDeviceCount; ++i) d.profiles[i] = {(uint8_t)sourceProfiles[i].driver, sourceDevices[i].address, (uint8_t)sourceProfiles[i].confirmed, sourceProfiles[i].safeLevels};
    for (int i = 0; i < numSwitches; ++i) {
        const auto& e = *switchEntry[i]; auto& s = d.channels[i];
        s.kind = (uint8_t)e.binding.source.kind; s.mode = (uint8_t)e.binding.mode; s.bus = e.binding.source.bus;
        s.address = e.binding.source.address; s.channel = e.binding.source.channel;
        s.flags = (e.writeable ? 1 : 0) | (e.binding.activeLow ? 2 : 0); s.type = e.type;
        s.minimum = e.min; s.maximum = e.max; s.step = e.step; s.scale = e.scale; s.offset = e.offset;
        copyConfigString(s.name, e.switchName); copyConfigString(s.description, e.description);
    }
    d.crc = sourceConfigCrc((uint8_t*)&d, sizeof(d)); EEPROM.put(0, d);
    sourceStorageValid = EEPROM.commit(); sourceStorageMessage = sourceStorageValid ? "Saved version 3 mappings" : "EEPROM commit failed; changes are active in RAM only";
}
Result changeMappedLegacyType(int id, SwitchType type) {
    if (id < 0 || id >= numSwitches || type > SWITCH_ANALG_DAC) return Result::InvalidSource;
    SwitchEntry candidate = *switchEntry[id];
    candidate.type = type;
    candidate.binding.mode = type == SWITCH_PWM ? Mode::PwmOutput : type == SWITCH_ANALG_DAC ? Mode::AnalogOutput : Mode::BinaryOutput;
    candidate.min = 0; candidate.max = type == SWITCH_PWM ? 1024 : type == SWITCH_ANALG_DAC ? 4095 : 1; candidate.step = 1;
    Result result = validateMappedBinding(id, candidate);
    if (result != Result::Ok) return result;
    const bool changed = candidate.binding.mode != switchEntry[id]->binding.mode || candidate.min != switchEntry[id]->min ||
        candidate.max != switchEntry[id]->max || candidate.step != switchEntry[id]->step;
    if (changed) {
        pendingAdcId = -1;
        result = quiesceMappedSource(id); if (result != Result::Ok) return result;
        releaseMappedSource(*switchEntry[id]);
        candidate.sample.valid = false; candidate.sampleError = Result::NoValidValue; candidate.acquisitionStarted = false;
    }
    *switchEntry[id] = candidate;
    if (changed) {
        initializeNativeSource(*switchEntry[id]);
        if (candidate.binding.source.kind == Kind::I2cDac) {
            size_t slot;
            if (resolveMapped(candidate, false, slot) == Result::Ok)
                switchEntry[id]->sample = {0, millis(), sourceDevices[slot].generation, true};
        }
    }
    ++sourceInventoryRevision; saveToEeprom();
    return sourceStorageValid ? Result::Ok : Result::StorageFailure;
}
void setupFromEeprom() {
    setDefaults(); auto& d = sourceConfigBuffer; EEPROM.get(0, d);
    for (size_t i = 0; i < nativeSourceCount; ++i) nativeResidentDefaults(i);
    if (d.signature == SOURCE_CONFIG_MAGIC && d.version == 3 && d.length == sizeof(d)) {
        uint32_t expected = d.crc; d.crc = 0;
        if (sourceConfigCrc((uint8_t*)&d, sizeof(d)) != expected || d.count > MAXSWITCH || d.devices > MAX_SOURCE_DEVICES ||
            d.udp < 1024 || d.autoRecover > 1 || d.hostname[39] || d.location[39] || d.boardTag != boardPinMapTag()) { sourceStorageMessage = "Corrupt configuration or changed connector map; safe defaults retained"; return; }
        for (size_t i = 0; i < d.devices; ++i) if (!addSourceProfile((SourceDriver)d.profiles[i].driver, d.profiles[i].address, d.profiles[i].confirmed == 1, d.profiles[i].safeLevels, false)) {
            sourceDeviceCount = 0; sourceStorageMessage = "Invalid driver profiles; outputs unassigned"; return;
        }
        for (size_t slot = 0; slot < sourceDeviceCount; ++slot) if (sourceDevices[slot].kind == Kind::I2cAdc) sourceResidentCount += 4;
        if (d.count < sourceResidentCount || d.count - sourceResidentCount > MAX_CONFIGURABLE_SWITCHES || !allocateMappedSwitches(d.count)) {
            sourceDeviceCount = 0; sourceResidentCount = nativeSourceCount; sourceStorageMessage = "Invalid saved channel count"; return;
        }
        copyConfigString(myHostname, d.hostname); copyConfigString(thisID, myHostname); copyConfigString(Location, d.location); udpPort = d.udp; sourceAutoRecover = d.autoRecover;
        for (int i = 0; i < numSwitches; ++i) {
            auto& e = *switchEntry[i]; const auto& s = d.channels[i];
            e.min = s.minimum; e.max = s.maximum; e.step = s.step; e.scale = s.scale; e.offset = s.offset; e.writeable = s.flags & 1;
            e.type = (SwitchType)s.type; e.binding = {{(Kind)s.kind, s.bus, s.address, s.channel}, (Mode)s.mode, e.writeable, (s.flags & 2) != 0};
            if (s.name[39] || s.description[39] || !validMappedRange(e) || validateMappedBinding(i, e) != Result::Ok) {
                e.binding = {}; e.min = 0; e.max = 1; e.step = 1; e.scale = 1; e.offset = 0;
            }
            copyConfigString(e.switchName, s.name); copyConfigString(e.description, s.description);
        }
        enforceResidentBindings();
        for (int i = sourceResidentCount; i < numSwitches; ++i)
            if (validateMappedBinding(i, *switchEntry[i]) != Result::Ok) switchEntry[i]->binding = {};
        sourceStorageValid = true; sourceStorageMessage = "Version 3 mappings loaded"; return;
    }
    if (EEPROM.read(0) != '*') { sourceStorageMessage = "No valid mapping configuration; outputs unassigned"; return; }
    int count, port; EEPROM.get(4, count); EEPROM.get(8, port);
    if (count < 0 || count > MAX_CONFIGURABLE_SWITCHES || port < 1024 || port > 65535 || !allocateMappedSwitches(count + sourceResidentCount)) { sourceStorageMessage = "Invalid legacy record"; return; }
    for (int j = 0; j < 40; ++j) { myHostname[j] = EEPROM.read(12 + j); Location[j] = EEPROM.read(52 + j); }
    myHostname[39] = 0; Location[39] = 0; copyConfigString(thisID, myHostname); udpPort = port;
    int at = 92;
    for (int i = 0; i < count; ++i) {
        auto& e = *switchEntry[i + sourceResidentCount]; int type, pin; uint8_t writableByte; float oldValue;
        EEPROM.get(at, type); at += 4; EEPROM.get(at, pin); at += 4; EEPROM.get(at, writableByte); at += 1;
        const bool writable = writableByte == 1;
        EEPROM.get(at, e.min); at += 4; EEPROM.get(at, e.max); at += 4; EEPROM.get(at, e.step); at += 4; EEPROM.get(at, oldValue); at += 4;
        for (int j = 0; j < 40; ++j) e.switchName[j] = EEPROM.read(at++);
        for (int j = 0; j < 40; ++j) e.description[j] = EEPROM.read(at++);
        e.switchName[39] = 0; e.description[39] = 0; e.type = type >= 0 && type <= 4 ? (SwitchType)type : SWITCH_NOT_SELECTED;
        e.writeable = writable; e.pin = pin; e.binding = {}; e.sample.valid = false;
        e.binding.mode = type == SWITCH_PWM ? Mode::PwmOutput : type == SWITCH_ANALG_DAC ? Mode::AnalogOutput : Mode::BinaryOutput;
        if (!validMappedRange(e)) { e.min = 0; e.max = 1; e.step = 1; }
        //Invalid historical decimal-160 address must be confirmed in setup, never guessed.
        if (e.type == SWITCH_PWM && pin >= 0) {
            e.binding = {{Kind::NativePin, 0, 0, (uint16_t)pin}, Mode::PwmOutput, writable, false};
            for (size_t resident = 0; resident < nativeSourceCount; ++resident)
                if (nativeSources[resident].id == (uint16_t)pin && (nativeSources[resident].capabilities & SourceMapping::Pwm)) {
                    auto& target = *switchEntry[resident];
                    target.binding = e.binding; target.binding.activeLow = nativeSources[resident].startupActiveLow;
                    target.min = e.min; target.max = e.max; target.step = e.step;
                    target.writeable = writable; target.type = SWITCH_PWM; target.sample = {};
                    copyConfigString(target.switchName, e.switchName); copyConfigString(target.description, e.description);
                }
            e.binding = {};
        }
    }
    sourceStorageMessage = "Legacy names and IDs retained. Confirm I2C profiles/assign sources; save to migrate.";
}
#endif
