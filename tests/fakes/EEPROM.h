#pragma once
#include <array>
#include <cstring>
#include <stdexcept>
struct FakeEeprom
{
    std::array<uint8_t, 4096> bytes{};
    bool succeeds = true;
    /**
     * @brief Provide the test adapter operation for read.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] at (size_t) Value used by the test adapter.
     * @return (uint8_t) Adapter value or regression result used by the calling test.
     */
    uint8_t read(size_t at) const
    {
        return bytes.at(at);
    }
    /**
     * @brief Provide the test adapter operation for get.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] at (size_t) Value used by the test adapter.
     * @param[in,out] value (T&) Input value or parsed/output value as indicated by reference and operation.
     * @return void; effects are described above.
     */
    template <class T> void get(size_t at, T &value) const
    {
        if (at + sizeof(value) > bytes.size())
            throw std::out_of_range("EEPROM get");
        std::memcpy(&value, bytes.data() + at, sizeof(value));
    }
    /**
     * @brief Provide the test adapter operation for put.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @param[in] at (size_t) Value used by the test adapter.
     * @param[in] value (const T&) Input value or parsed/output value as indicated by reference and operation.
     * @return void; effects are described above.
     */
    template <class T> void put(size_t at, const T &value)
    {
        if (at + sizeof(value) > bytes.size())
            throw std::out_of_range("EEPROM put");
        std::memcpy(bytes.data() + at, &value, sizeof(value));
    }
    /**
     * @brief Provide the test adapter operation for commit.
     * @details Use deterministic in-memory state instead of physical hardware or network I/O.
     * @par Arguments
     * None.
     * @return (bool) True when the described check/operation succeeds; otherwise false.
     */
    bool commit()
    {
        return succeeds;
    }
};
inline FakeEeprom EEPROM;
