#pragma once
#include <array>
#include <cstring>
#include <stdexcept>
struct FakeEeprom {
    std::array<uint8_t, 4096> bytes{};
    bool succeeds = true;
    uint8_t read(size_t at) const { return bytes.at(at); }
    template<class T> void get(size_t at, T& value) const {
        if (at + sizeof(value) > bytes.size()) throw std::out_of_range("EEPROM get");
        std::memcpy(&value, bytes.data() + at, sizeof(value));
    }
    template<class T> void put(size_t at, const T& value) {
        if (at + sizeof(value) > bytes.size()) throw std::out_of_range("EEPROM put");
        std::memcpy(bytes.data() + at, &value, sizeof(value));
    }
    bool commit() { return succeeds; }
};
inline FakeEeprom EEPROM;
