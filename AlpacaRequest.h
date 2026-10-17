#ifndef SKYBADGER_ALPACA_REQUEST_H
#define SKYBADGER_ALPACA_REQUEST_H

#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <math.h>

/**
 * @brief Recognize ASCII whitespace in an API argument.
 * @details Compare one character with the permitted whitespace characters.
 * @param[in] c (char) Character to inspect.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
constexpr bool alpacaSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

/**
 * @brief Normalize an ASCII letter for argument lookup.
 * @details Convert uppercase ASCII to lowercase and preserve other characters.
 * @param[in] c (char) Character to inspect.
 * @return (char) Computed value for the described operation.
 */
constexpr char alpacaLower(char c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

/**
 * @brief Compare argument names without ASCII case sensitivity.
 * @details Walk both null-terminated strings and compare normalized characters.
 * @param[in] left (const char*) First null-terminated string.
 * @param[in] right (const char*) Second null-terminated string.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
constexpr bool alpacaKeyEqual(const char *left, const char *right)
{
    while (*left && alpacaLower(*left) == alpacaLower(*right))
    {
        ++left;
        ++right;
    }
    return *left == *right;
}

/**
 * @brief Measure a null-terminated argument string.
 * @details Count characters up to the first NUL byte.
 * @param[in] text (const char*) Input text to validate or decode.
 * @return (unsigned int) Computed value for the described operation.
 */
constexpr unsigned int alpacaCStringLength(const char *text)
{
    unsigned int length = 0;
    while (text[length])
        ++length;
    return length;
}

enum AlpacaArgumentStatus
{
    ALPACA_ARG_MISSING,
    ALPACA_ARG_PRESENT,
    ALPACA_ARG_DUPLICATE,
    ALPACA_ARG_INVALID
};

/**
 * @brief Locate one unambiguous API argument.
 * @details Scan request names case-insensitively and reject duplicate names or embedded NUL bytes.
 * @param[in,out] server (Server&) HTTP server/request adapter used for argument lookup or delivery.
 * @param[in] name (const char*) Argument/control name or text to display.
 * @param[out] value (Text&) Unique request argument text, assigned when a matching field is found.
 * @return (AlpacaArgumentStatus) Missing, present, duplicate or invalid argument status.
 */
template <class Server, class Text>
constexpr AlpacaArgumentStatus findAlpacaArgument(Server &server, const char *name, Text &value)
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

/**
 * @brief Parse a strict unsigned 32-bit decimal integer.
 * @details Reject empty, signed, malformed or overflowing input before storing the result.
 * @param[in] text (const char*) Input text to validate or decode.
 * @param[out] value (uint32_t&) Parsed unsigned integer, assigned on success.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
constexpr bool parseAlpacaUInt32(const char *text, uint32_t &value)
{
    while (alpacaSpace(*text))
        ++text;
    if (*text < '0' || *text > '9')
        return false;
    uint32_t parsed = 0;
    while (*text >= '0' && *text <= '9')
    {
        const uint32_t digit = *text++ - '0';
        if (parsed > (UINT32_MAX - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }
    while (alpacaSpace(*text))
        ++text;
    if (*text)
        return false;
    value = parsed;
    return true;
}

/**
 * @brief Read an optional unsigned transaction or client identifier.
 * @details Use zero for an absent field and strictly parse a uniquely supplied value.
 * @param[in,out] server (Server&) HTTP server/request adapter used for argument lookup or delivery.
 * @param[in] name (const char*) Argument/control name or text to display.
 * @param[out] value (uint32_t&) Zero when absent, or the successfully parsed unsigned integer.
 * @return (bool) True for an absent or valid argument; false for invalid/duplicate input.
 */
template <class Server, class Text>
constexpr bool parseAlpacaOptionalUInt32(Server &server, const char *name, uint32_t &value)
{
    value = 0;
    Text text;
    const auto status = findAlpacaArgument(server, name, text);
    return status == ALPACA_ARG_MISSING ||
           (status == ALPACA_ARG_PRESENT && parseAlpacaUInt32(text.c_str(), value));
}

/**
 * @brief Match a complete Boolean keyword.
 * @details Compare the full token with the expected text without ASCII case sensitivity.
 * @param[in] text (const char*) Input text to validate or decode.
 * @param[in] token (const char*) Expected complete keyword.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
constexpr bool alpacaBooleanToken(const char *text, const char *token)
{
    while (alpacaSpace(*text))
        ++text;
    while (*token)
        if (alpacaLower(*text++) != *token++)
            return false;
    while (alpacaSpace(*text))
        ++text;
    return *text == '\0';
}

/**
 * @brief Parse a Boolean API value.
 * @details Accept complete true/false keywords and update the output only for valid input.
 * @param[in] text (const char*) Input text to validate or decode.
 * @param[out] value (bool&) Parsed true/false value, assigned on success.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
constexpr bool parseAlpacaBool(const char *text, bool &value)
{
    if (alpacaBooleanToken(text, "true"))
    {
        value = true;
        return true;
    }
    if (alpacaBooleanToken(text, "false"))
    {
        value = false;
        return true;
    }
    return false;
}

// Decimal grammar only: optional sign, digits/decimal point and optional exponent.
// Reject hex, locale commas, NaN, infinity, partial tokens and embedded spaces.
/**
 * @brief Validate the syntax of a decimal number.
 * @details Scan the sign, digits, decimal point and optional exponent without accepting trailing text.
 * @param[in] text (const char*) Input text to validate or decode.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
constexpr bool alpacaDecimalToken(const char *text)
{
    while (alpacaSpace(*text))
        ++text;
    if (*text == '+' || *text == '-')
        ++text;
    bool digits = false;
    while (*text >= '0' && *text <= '9')
    {
        digits = true;
        ++text;
    }
    if (*text == '.')
    {
        ++text;
        while (*text >= '0' && *text <= '9')
        {
            digits = true;
            ++text;
        }
    }
    if (!digits)
        return false;
    if (*text == 'e' || *text == 'E')
    {
        ++text;
        if (*text == '+' || *text == '-')
            ++text;
        if (*text < '0' || *text > '9')
            return false;
        while (*text >= '0' && *text <= '9')
            ++text;
    }
    while (alpacaSpace(*text))
        ++text;
    return *text == '\0';
}

/**
 * @brief Parse a finite decimal API value.
 * @details Validate decimal syntax, convert with strtod and reject range errors or nonfinite values.
 * @param[in] text (const char*) Input text to validate or decode.
 * @param[out] value (double&) Parsed finite floating-point number, assigned on success.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
inline bool parseAlpacaDouble(const char *text, double &value)
{
    if (!alpacaDecimalToken(text))
        return false;
    char *end = nullptr;
    errno = 0;
    const double parsed = strtod(text, &end);
    if (errno == ERANGE || !isfinite(parsed))
        return false;
    while (alpacaSpace(*end))
        ++end;
    if (*end)
        return false;
    value = parsed;
    return true;
}

#endif
