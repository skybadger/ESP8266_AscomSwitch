#ifndef SKYBADGER_MAPPING_UI_H
#define SKYBADGER_MAPPING_UI_H
String escapedMappingHtml(const String& value) {
    String output; output.reserve(value.length());
    for (unsigned i = 0; i < value.length(); ++i) {
        switch (value[i]) {
          case '&': output += "&amp;"; break; case '<': output += "&lt;"; break;
          case '>': output += "&gt;"; break; case '"': output += "&quot;"; break;
          case '\'': output += "&#39;"; break; default: output += value[i];
        }
    }
    return output;
}
bool mappingArg(const char* name, String& value) {
    return findAlpacaArgument(server, name, value) == ALPACA_ARG_PRESENT;
}
void mappingNumberField(String& html, const char* name, const char* label, double value) {
    html += "<label><span>"; html += label;
    html += "</span></label><input type=\"number\" step=\"any\" required name=\"";
    html += name; html += "\" value=\""; html += String(value, 6); html += "\"><br>";
}
String sourceLabel(const SourceMapping::SourceRef& source) {
    if (source.kind == Kind::None) return "Unassigned";
    if (source.kind == Kind::NativePin) {
        for (size_t i = 0; i < nativeSourceCount; ++i) if (nativeSources[i].id == source.channel) return nativeSources[i].label;
        return "Unavailable native connector";
    }
    return String(source.kind == Kind::Pcf8574 ? "PCF8574" : source.kind == Kind::I2cAdc ? "I2C ADC" : "I2C DAC") +
        " bus 0 / 0x" + String(source.address, HEX) + " / channel " + source.channel;
}
void mappingSourceOption(String& html, int id, const SourceMapping::SourceRef& source) {
    bool occupied = false;
    for (int i = 0; i < numSwitches; ++i)
        if (i != id && SourceMapping::sameResource(source, switchEntry[i]->binding.source)) occupied = true;
    uint8_t caps = 31;
    double minimum = -6.144, maximum = 6.141, step = 0.003;
    if (source.kind == Kind::NativePin) {
        for (const auto& pin : nativeSources) if (pin.id == source.channel) caps = pin.capabilities;
        minimum = 0; maximum = 1; step = 1.0 / 1023;
    } else if (source.kind == Kind::Pcf8574) caps = 3;
    else if (source.kind == Kind::I2cAdc) {
        caps = 8;
        for (size_t slot = 0; slot < sourceDeviceCount; ++slot)
            if (sourceDevices[slot].address == source.address && sourceProfiles[slot].driver == SourceDriver::Ads1115) {
                maximum = 6.1438125; step = 0.0001875;
            }
    } else if (source.kind == Kind::I2cDac) caps = 16;
    html += "<option data-caps=\"" + String(caps) + "\" data-min=\"" + String(minimum, 8) +
        "\" data-max=\"" + String(maximum, 8) + "\" data-step=\"" + String(step, 9) + "\" value=\"" + sourceKey(source) + "\"";
    if (sourceKey(source) == sourceKey(switchEntry[id]->binding.source)) html += " selected";
    if (occupied) html += " disabled";
    html += ">" + escapedMappingHtml(sourceLabel(source));
    if (occupied) html += " (allocated)";
    for (size_t slot = 0; slot < sourceDeviceCount; ++slot)
        if (source.address && sourceDevices[slot].address == source.address) html += String(" — ") + sourceStateName(sourceDevices[slot].state);
    html += "</option>";
}
String& mappedSwitchForm(String& html, int id) {
    const auto& e = *switchEntry[id];
    html = "<div class=\"row\"><form data-mapped-source method=\"POST\" action=\"/api/v1/switch/0/switches\"><fieldset><legend>Switch ";
    html += id; html += "</legend><input type=\"hidden\" name=\"switchId\" value=\""; html += id;
    html += "\"><input type=\"hidden\" name=\"revision\" value=\""; html += sourceInventoryRevision; html += "\">";
    html += "<label><span>Name</span></label><input name=\"name\" maxlength=\"39\" required value=\"" + escapedMappingHtml(e.switchName) + "\"><br>";
    html += "<label><span>Description</span></label><input name=\"description\" maxlength=\"39\" value=\"" + escapedMappingHtml(e.description) + "\"><br>";
    html += "<label><span>Physical source</span></label><select name=\"source\" onchange=\"updateMappedForm(this.form,true)\">";
    if (id < (int)sourceResidentCount) mappingSourceOption(html, id, e.binding.source);
    else {
        mappingSourceOption(html, id, {});
        for (size_t slot = 0; slot < sourceDeviceCount; ++slot) {
            const auto& d = sourceDevices[slot];
            for (uint16_t channel = 0; channel < d.channelCount; ++channel)
                mappingSourceOption(html, id, {d.kind, d.bus, d.address, channel});
        }
    }
    html += "</select><br><label><span>Mode</span></label><select name=\"mode\" onchange=\"updateMappedForm(this.form,true)\">";
    const char* labels[] = {"Binary input", "Binary output", "PWM output", "ADC input (volts)", "DAC output"};
    for (int i = 0; i < 5; ++i) html += String("<option value=\"") + i + "\"" +
        ((int)e.binding.mode == i ? " selected" : "") + ">" + labels[i] + "</option>";
    html += "</select><br>";
    mappingNumberField(html, "min", "Minimum", e.min); mappingNumberField(html, "max", "Maximum", e.max);
    mappingNumberField(html, "step", "Step", e.step); mappingNumberField(html, "scale", "ADC scale", e.scale);
    mappingNumberField(html, "offset", "ADC offset", e.offset);
    html += String("<label><input type=\"checkbox\" name=\"writeable\" value=\"true\"") + (e.writeable ? " checked" : "") + ">Allow output writes (inputs are always read-only)</label><br>";
    html += String("<label><input type=\"checkbox\" name=\"activeLow\" value=\"true\"") + (e.binding.activeLow ? " checked" : "") + ">Active low</label><br>";
    html += "<label><input type=\"checkbox\" name=\"confirmChange\" value=\"true\">Approve hardware change. Reinitializing an expander resets all its outputs inactive; an absent old source cannot be controlled.</label><br>";
    html += "<input type=\"submit\" value=\"Save mapping\"></fieldset></form></div>"; return html;
}
String& mappedInventoryForm(String& html) {
    html += "<section><h2>Physical sources</h2><p>" + escapedMappingHtml(sourceStorageMessage) + "</p>";
    html += "<p>Native connectors come from BoardPinMap.h and occupy the first IDs. Confirmed ADC inputs follow; configurable external channels are last. Missing devices keep their assignments.</p>";
    html += "<p>Adding an ADC inserts its resident channels before configurable channels and changes those channel IDs. Address ACKs do not identify device type; confirm the hardware marking.</p><p>Detected addresses: ";
    for (unsigned address = 8; address <= 0x77; ++address) if (detectedAddresses[address]) html += String("0x") + String(address, HEX) + " ";
    html += "</p><form method=\"POST\" action=\"/setup/source-device\"><label>Driver <select name=\"driver\"><option value=\"1\">PCF8574 / PCF8574A</option><option value=\"2\">ADS1015</option><option value=\"3\">ADS1115</option><option value=\"4\">MCP4725 (0..4095 counts)</option></select></label><br>";
    html += "<label>7-bit address (decimal) <input type=\"number\" min=\"8\" max=\"119\" required name=\"address\"></label><br>";
    html += "<label>Whole-port inactive pattern (0..255 for PCF; enter 0 for converters) <input type=\"number\" min=\"0\" max=\"255\" required name=\"safeLevels\"></label><br>";
    html += "<label><input type=\"checkbox\" required name=\"confirmed\" value=\"true\">I confirm the physical chip and address</label><br><input type=\"submit\" value=\"Add device profile\"></form>";
    for (size_t slot = 0; slot < sourceDeviceCount; ++slot) {
        const auto& d = sourceDevices[slot];
        html += "<form method=\"POST\" action=\"/setup/source-recover\"><p>Device 0x" + String(d.address, HEX) + " — " + sourceStateName(d.state) + "</p>";
        html += "<input type=\"hidden\" name=\"address\" value=\"" + String(d.address) + "\">";
        html += "<label><input type=\"checkbox\" name=\"approved\" value=\"true\" required>Confirm device identity and initialize all outputs inactive</label><br><input type=\"submit\" value=\"Initialize device\"></form>";
    }
    html += "</section>"; return html;
}
bool mappedCheckbox(const char* key, bool& checked) {
    String text; const auto status = findAlpacaArgument(server, key, text);
    checked = false; return status == ALPACA_ARG_MISSING || (status == ALPACA_ARG_PRESENT && parseAlpacaBool(text.c_str(), checked));
}
void handlerSourceDevice() {
    if (!validateSettingHandlerRequest("ConfigureSourceDevice")) return;
    String addressText, driverText, safeText, message, error; uint32_t address = 0, driver = 0, safe = 0; bool confirmed = false;
    if (!mappingArg("address", addressText) || !mappingArg("driver", driverText) ||
        !parseAlpacaUInt32(addressText.c_str(), address) || !parseAlpacaUInt32(driverText.c_str(), driver) ||
        !mappingArg("safeLevels", safeText) || !parseAlpacaUInt32(safeText.c_str(), safe) || safe > 255 ||
        address > 0x77 || driver > 4 || !mappedCheckbox("confirmed", confirmed) || !confirmed ||
        !addSourceProfile((SourceDriver)driver, address, true, safe)) error = "Invalid/duplicate address or driver profile (maximum eight devices)";
    if (!error.length()) { saveToEeprom(); if (!sourceStorageValid) error = sourceStorageMessage; }
    sendDeviceSetup(error.length() ? 400 : 200, message, error);
}
void handlerSourceRecover() {
    if (!validateSettingHandlerRequest("RecoverSourceDevice")) return;
    String addressText, message, error; uint32_t address; bool approved = false;
    if (!mappingArg("address", addressText) || !parseAlpacaUInt32(addressText.c_str(), address) || address > 0x77 ||
        !mappedCheckbox("approved", approved) || !approved) error = "Positive confirmation and valid address required";
    else {
        size_t slot = 0; while (slot < sourceDeviceCount && sourceDevices[slot].address != address) ++slot;
        if (slot == sourceDeviceCount) error = "No configured device at this address";
        else {
            pendingAdcId = -1;
            Wire.beginTransmission((uint8_t)address); const uint8_t status = Wire.endTransmission();
            sourceDevices[slot].observeProbe(millis(), status == 0, status);
            if (!status) {
                sourceDevices[slot].state = State::Recovering;
                const Result result = recoverMappedDevice(slot, true);
                if (result != Result::Ok) error = sourceResultMessage(result);
            } else { invalidateDeviceSamples(slot); error = "Device did not acknowledge; mappings retained"; }
        }
    }
    sendDeviceSetup(error.length() ? 400 : 200, message, error);
}
void handlerMappedSetupSwitches() {
    String text, message, error; uint32_t id = UINT32_MAX, revision = 0, mode = 0;
    if (!mappingArg("switchId", text) || !parseAlpacaUInt32(text.c_str(), id) || id >= (uint32_t)numSwitches ||
        !mappingArg("revision", text) || !parseAlpacaUInt32(text.c_str(), revision) || revision != sourceInventoryRevision) {
        error = "Invalid switch ID or inventory changed. Reload setup and retry.";
        sendDriver0Setup(400, message, error); return;
    }
    SwitchEntry candidate = *switchEntry[id];
    String name, description; bool writable, activeLow, approved;
    if (!mappingArg("source", text) || !parseSourceKey(text, candidate.binding.source) ||
        !mappingArg("mode", text) || !parseAlpacaUInt32(text.c_str(), mode) || mode > 4 ||
        !mappingArg("name", name) || !mappingArg("description", description) || name.length() >= 40 || description.length() >= 40 ||
        !mappedCheckbox("writeable", writable) || !mappedCheckbox("activeLow", activeLow) || !mappedCheckbox("confirmChange", approved)) error = "Missing, duplicate or invalid configuration argument";
    if (!error.length()) {
        candidate.binding.mode = (Mode)mode; candidate.binding.activeLow = activeLow;
        candidate.writeable = SourceMapping::isOutput((Mode)mode) && writable;
        candidate.binding.userWritable = candidate.writeable;
        if (id < sourceResidentCount && sourceKey(candidate.binding.source) != sourceKey(switchEntry[id]->binding.source))
            error = "Resident physical sources cannot be rebound or removed";
        if (id >= nativeSourceCount && id < sourceResidentCount && mode != (unsigned)Mode::AnalogInput)
            error = "Resident ADC channels are read-only analogue inputs";
        double values[5]; const char* fields[] = {"min", "max", "step", "scale", "offset"};
        for (size_t i = 0; i < 5; ++i)
            if (!mappingArg(fields[i], text) || !parseAlpacaDouble(text.c_str(), values[i])) error = "Ranges and calibration must be finite numbers";
        if (!error.length()) {
            candidate.min = values[0]; candidate.max = values[1]; candidate.step = values[2]; candidate.scale = values[3]; candidate.offset = values[4];
            const bool binary = mode == 0 || mode == 1;
            if (!validMappedRange(candidate) || (binary && (candidate.min != 0 || candidate.max != 1 || candidate.step != 1)) ||
                ((mode == 2 || mode == 4) && (candidate.min < 0 || candidate.max > (mode == 2 ? 1024 : 4095) || candidate.min != round(candidate.min) || candidate.max != round(candidate.max) || candidate.step != round(candidate.step))))
                error = "Invalid range: binary 0..1; PWM integer 0..1024; DAC integer 0..4095; min < max and step > 0";
            else { const Result result = validateMappedBinding(id, candidate); if (result != Result::Ok) error = sourceResultMessage(result); }
        }
    }
    if (!error.length()) {
        const auto old = *switchEntry[id];
        bool hardwareChange = sourceKey(old.binding.source) != sourceKey(candidate.binding.source) || old.binding.mode != candidate.binding.mode ||
            old.binding.activeLow != candidate.binding.activeLow || old.min != candidate.min || old.max != candidate.max ||
            old.step != candidate.step || old.scale != candidate.scale || old.offset != candidate.offset;
        if (hardwareChange && !approved) error = "Approve the hardware change before applying this mapping";
        else {
            pendingAdcId = -1;
            if (hardwareChange) {
                if (quiesceMappedSource(id) != Result::Ok) error = "Could not quiesce the old output; retry after resolving its fault";
            }
            if (!error.length()) {
                if (hardwareChange) releaseMappedSource(*switchEntry[id]);
                candidate.switchName = switchEntry[id]->switchName; candidate.description = switchEntry[id]->description;
                candidate.type = mode == 2 ? SWITCH_PWM : mode == 4 ? SWITCH_ANALG_DAC : mode == 3 ? SWITCH_ADC : SWITCH_RELAY_NO;
                if (hardwareChange) { candidate.sample.valid = false; candidate.sampleError = Result::NoValidValue; candidate.acquisitionStarted = false; }
                *switchEntry[id] = candidate;
                copyConfigString(candidate.switchName, name.c_str()); copyConfigString(candidate.description, description.c_str());
                if (hardwareChange) {
                    initializeNativeSource(*switchEntry[id]);
                    for (size_t slot = 0; slot < sourceDeviceCount; ++slot)
                        if (candidate.binding.source.kind == Kind::Pcf8574 && candidate.binding.source.address == sourceDevices[slot].address) {
                            const uint8_t bit = 1u << candidate.binding.source.channel;
                            if (candidate.binding.mode == Mode::BinaryInput || candidate.binding.activeLow) sourceProfiles[slot].safeLevels |= bit;
                            else sourceProfiles[slot].safeLevels &= ~bit;
                        }
                    for (size_t slot = 0; slot < sourceDeviceCount; ++slot)
                        if (sourceDevices[slot].kind == Kind::Pcf8574 &&
                            (sourceDevices[slot].address == old.binding.source.address || sourceDevices[slot].address == candidate.binding.source.address)) {
                            sourceDevices[slot].state = State::Recovering; ++sourceDevices[slot].generation; invalidateDeviceSamples(slot);
                        }
                }
                ++sourceInventoryRevision; saveToEeprom(); if (!sourceStorageValid) error = sourceStorageMessage;
            }
        }
    }
    sendDriver0Setup(error.length() ? 400 : 200, message, error);
}
#endif
