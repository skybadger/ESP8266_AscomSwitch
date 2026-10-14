#ifndef SKYBADGER_ALPACA_REQUEST_H
#define SKYBADGER_ALPACA_REQUEST_H

#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <math.h>

constexpr bool alpacaSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

constexpr char alpacaLower(char c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

constexpr bool alpacaKeyEqual(const char* left, const char* right)
{
    while (*left && alpacaLower(*left) == alpacaLower(*right)) { ++left; ++right; }
    return *left == *right;
}

constexpr unsigned int alpacaCStringLength(const char* text)
{
    unsigned int length = 0;
    while (text[length]) ++length;
    return length;
}

enum AlpacaArgumentStatus {
    ALPACA_ARG_MISSING, ALPACA_ARG_PRESENT, ALPACA_ARG_DUPLICATE, ALPACA_ARG_INVALID
};

template <class Server, class Text>
constexpr AlpacaArgumentStatus findAlpacaArgument(Server& server, const char* name, Text& value)
{
    bool found = false;
    for (int i = 0; i < server.args(); ++i)
    {
        const auto key = server.argName(i);
        if (key.length() != alpacaCStringLength(key.c_str()))
            return ALPACA_ARG_INVALID;
        if (!alpacaKeyEqual(key.c_str(), name))
            continue;
        if (found)
            return ALPACA_ARG_DUPLICATE;
        value = server.arg(i);
        if (value.length() != alpacaCStringLength(value.c_str()))
            return ALPACA_ARG_INVALID;
        found = true;
    }
    return found ? ALPACA_ARG_PRESENT : ALPACA_ARG_MISSING;
}

constexpr bool parseAlpacaUInt32(const char* text, uint32_t& value)
{
    while (alpacaSpace(*text)) ++text;
    if (*text < '0' || *text > '9') return false;
    uint32_t parsed = 0;
    while (*text >= '0' && *text <= '9')
    {
        const uint32_t digit = *text++ - '0';
        if (parsed > (UINT32_MAX - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    while (alpacaSpace(*text)) ++text;
    if (*text) return false;
    value = parsed;
    return true;
}

template <class Server, class Text>
constexpr bool parseAlpacaOptionalUInt32(Server& server, const char* name, uint32_t& value)
{
    value = 0;
    Text text;
    const auto status = findAlpacaArgument(server, name, text);
    return status == ALPACA_ARG_MISSING ||
        (status == ALPACA_ARG_PRESENT && parseAlpacaUInt32(text.c_str(), value));
}

constexpr bool alpacaBooleanToken(const char* text, const char* token)
{
    while (alpacaSpace(*text)) ++text;
    while (*token)
        if (alpacaLower(*text++) != *token++) return false;
    while (alpacaSpace(*text)) ++text;
    return *text == '\0';
}

constexpr bool parseAlpacaBool(const char* text, bool& value)
{
    if (alpacaBooleanToken(text, "true")) { value = true; return true; }
    if (alpacaBooleanToken(text, "false")) { value = false; return true; }
    return false;
}

// Decimal grammar only: optional sign, digits/decimal point and optional exponent.
// Reject hex, locale commas, NaN, infinity, partial tokens and embedded spaces.
constexpr bool alpacaDecimalToken(const char* text)
{
    while (alpacaSpace(*text)) ++text;
    if (*text == '+' || *text == '-') ++text;
    bool digits = false;
    while (*text >= '0' && *text <= '9') { digits = true; ++text; }
    if (*text == '.')
    {
        ++text;
        while (*text >= '0' && *text <= '9') { digits = true; ++text; }
    }
    if (!digits) return false;
    if (*text == 'e' || *text == 'E')
    {
        ++text;
        if (*text == '+' || *text == '-') ++text;
        if (*text < '0' || *text > '9') return false;
        while (*text >= '0' && *text <= '9') ++text;
    }
    while (alpacaSpace(*text)) ++text;
    return *text == '\0';
}

inline bool parseAlpacaDouble(const char* text, double& value)
{
    if (!alpacaDecimalToken(text)) return false;
    char* end = nullptr;
    errno = 0;
    const double parsed = strtod(text, &end);
    if (errno == ERANGE || !isfinite(parsed)) return false;
    while (alpacaSpace(*end)) ++end;
    if (*end) return false;
    value = parsed;
    return true;
}

#endif
