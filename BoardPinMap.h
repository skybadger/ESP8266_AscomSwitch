#ifndef SKYBADGER_BOARD_PIN_MAP_H
#define SKYBADGER_BOARD_PIN_MAP_H
#include "SourceMapping.h"
#include <array>

//Assembly configuration: list ONLY connectors physically exposed on this build.
//Resource 0..16 means that ESP8266 GPIO. Resource 256 means dedicated ADC pad A0.
//A0 is ADC-only on ESP8266; it is not GPIO0 and cannot supply PWM/digital I/O.
//Optional final field: startupActiveLow for output-only connectors. Connectors
//with DigitalIn default to high-impedance input until the user configures output.
constexpr uint8_t boardSdaPin = 2;
constexpr uint8_t boardSclPin = 0;
constexpr uint8_t boardSerialTxPin = 1; //setup() uses UART TX-only.

#if defined ESP8266_01
constexpr std::array<SourceMapping::NativeResource, 1> nativeSources = {{
    {3, SourceMapping::DigitalIn | SourceMapping::DigitalOut | SourceMapping::Pwm, false, "RX header / GPIO3"},
    //ESP-01 does not bring A0 to its normal header. Do not add it unless wired.
}};
#elif defined ESP8266_12
constexpr std::array<SourceMapping::NativeResource, 4> nativeSources = {{
    {12, SourceMapping::DigitalIn | SourceMapping::DigitalOut | SourceMapping::Pwm, false, "GPIO12 connector"},
    {13, SourceMapping::DigitalIn | SourceMapping::DigitalOut | SourceMapping::Pwm, false, "GPIO13 connector"},
    {14, SourceMapping::DigitalIn | SourceMapping::DigitalOut | SourceMapping::Pwm, false, "GPIO14 connector"},
    {256, SourceMapping::Adc, false, "A0 ADC connector"},
}};
#else
#error Select and edit an ESP8266 assembly profile in BoardPinMap.h
#endif

constexpr size_t nativeSourceCount = nativeSources.size(); //Use array<...,0> if no native connectors are exposed.
constexpr bool validBoardPinMap() {
    if (boardSdaPin > 15 || boardSclPin > 15 || boardSerialTxPin != 1 ||
        (boardSdaPin >= 6 && boardSdaPin <= 11) || (boardSclPin >= 6 && boardSclPin <= 11) ||
        boardSdaPin == boardSclPin || boardSdaPin == boardSerialTxPin || boardSclPin == boardSerialTxPin) return false;
    for (size_t i = 0; i < nativeSourceCount; ++i) {
        const auto& p = nativeSources[i];
        if (p.reserved || !p.label || !p.capabilities) return false;
        if (p.id == 256) { if (p.capabilities != SourceMapping::Adc) return false; }
        else if (p.id > 16 || (p.id >= 6 && p.id <= 11) || p.id == boardSdaPin ||
            p.id == boardSclPin || p.id == boardSerialTxPin ||
            (p.capabilities & ~(SourceMapping::DigitalIn | SourceMapping::DigitalOut | SourceMapping::Pwm))) return false;
        for (size_t j = 0; j < i; ++j) if (nativeSources[j].id == p.id) return false;
    }
    return true;
}
static_assert(validBoardPinMap(), "Invalid exposed connector: capability, duplicate, I2C/UART/flash conflict or A0 used as GPIO");
constexpr uint32_t boardPinMapTag() {
    uint32_t hash = 2166136261u;
    hash = (hash ^ boardSdaPin) * 16777619u; hash = (hash ^ boardSclPin) * 16777619u;
    hash = (hash ^ boardSerialTxPin) * 16777619u;
    for (const auto& pin : nativeSources) {
        hash = (hash ^ pin.id) * 16777619u; hash = (hash ^ pin.capabilities) * 16777619u;
        hash = (hash ^ static_cast<uint32_t>(pin.startupActiveLow)) * 16777619u;
    }
    return hash;
}
#endif
