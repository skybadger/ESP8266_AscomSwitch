#include "../AlpacaRequest.h"

constexpr bool uintValue(const char* text, uint32_t expected)
{
    uint32_t value = 17;
    return parseAlpacaUInt32(text, value) && value == expected;
}
constexpr bool badUInt(const char* text)
{
    uint32_t value = 17;
    return !parseAlpacaUInt32(text, value) && value == 17;
}
constexpr bool boolValue(const char* text, bool expected)
{
    bool value = !expected;
    return parseAlpacaBool(text, value) && value == expected;
}
constexpr bool badBool(const char* text)
{
    bool value = true;
    return !parseAlpacaBool(text, value) && value;
}

struct Text {
    const char* text = "";
    int reportedLength = -1;
    constexpr const char* c_str() const { return text; }
    constexpr unsigned int length() const {
        return reportedLength < 0 ? alpacaCStringLength(text) : (unsigned int) reportedLength;
    }
};
struct Request {
    int count;
    const char* keys[3];
    const char* values[3];
    int valueLengths[3] = {-1, -1, -1};
    int keyLengths[3] = {-1, -1, -1};
    constexpr int args() { return count; }
    constexpr Text argName(int i) { return Text{keys[i], keyLengths[i]}; }
    constexpr Text arg(int i) { return Text{values[i], valueLengths[i]}; }
};
constexpr bool keyLookup()
{
    Request request{3, {"clienttransactionid", "sTaTe", "VALUE"}, {"4294967295", "TRUE", "1.0"}};
    Text text;
    if (findAlpacaArgument(request, "ClientTransactionID", text) != ALPACA_ARG_PRESENT)
        return false;
    uint32_t transaction = 0;
    if (!parseAlpacaUInt32(text.c_str(), transaction) || transaction != UINT32_MAX)
        return false;
    if (findAlpacaArgument(request, "State", text) != ALPACA_ARG_PRESENT)
        return false;
    bool state = false;
    if (!parseAlpacaBool(text.c_str(), state) || !state) return false;
    return findAlpacaArgument(request, "Value", text) == ALPACA_ARG_PRESENT
        && alpacaDecimalToken(text.c_str());
}
constexpr bool duplicateKey()
{
    Request request{2, {"Id", "iD", ""}, {"0", "1", ""}};
    Text text;
    return findAlpacaArgument(request, "Id", text) == ALPACA_ARG_DUPLICATE;
}
constexpr bool missingKey()
{
    Request request{1, {"ClientID", "", ""}, {"0", "", ""}};
    Text text;
    return findAlpacaArgument(request, "Id", text) == ALPACA_ARG_MISSING;
}

constexpr bool embeddedNullValue()
{
    Request request{1, {"Value", "", ""}, {"1\0garbage", "", ""}};
    request.valueLengths[0] = 9;
    Text text;
    return findAlpacaArgument(request, "Value", text) == ALPACA_ARG_INVALID;
}

constexpr bool embeddedNullKey()
{
    Request request{1, {"Id\0junk", "", ""}, {"0", "", ""}};
    request.keyLengths[0] = 7;
    Text text;
    return findAlpacaArgument(request, "Id", text) == ALPACA_ARG_INVALID;
}

static_assert(uintValue("0", 0), "Zero is legal uint32");
static_assert(uintValue("4294967295", UINT32_MAX), "Full uint32 range");
static_assert(uintValue("0007", 7), "Leading zeroes");
static_assert(uintValue(" 42\t", 42), "Surrounding ASCII whitespace");
static_assert(badUInt(""), "Missing numeric text");
static_assert(badUInt("-1"), "Negative unsigned Id");
static_assert(badUInt("+1"), "Unsigned Id grammar has no sign");
static_assert(badUInt("1.0"), "Id cannot be decimal");
static_assert(badUInt("12abc"), "No partial integer parse");
static_assert(badUInt("0x10"), "No hex integer");
static_assert(badUInt("4294967296"), "Overflow boundary");
static_assert(badUInt("999999999999999999999999999"), "Long overflow");
static_assert(badUInt("1 2"), "No embedded whitespace");
static_assert(boolValue("TRUE", true), "Case-insensitive true");
static_assert(boolValue(" FaLsE\r\n", false), "Case-insensitive false with whitespace");
static_assert(badBool(""), "Empty boolean");
static_assert(badBool("tru"), "Incomplete boolean");
static_assert(badBool("truefalse"), "No boolean suffix");
static_assert(badBool("1"), "Numeric boolean rejected");
static_assert(badBool("anything"), "Garbage cannot become false");
static_assert(alpacaDecimalToken("0"), "Decimal zero");
static_assert(alpacaDecimalToken("-0.5"), "Negative decimal grammar");
static_assert(alpacaDecimalToken("+.5"), "Leading decimal point");
static_assert(alpacaDecimalToken("1."), "Trailing decimal point");
static_assert(alpacaDecimalToken(" 1.25e-2 "), "Exponent and surrounding whitespace");
static_assert(!alpacaDecimalToken(""), "No empty value");
static_assert(!alpacaDecimalToken("."), "At least one digit");
static_assert(!alpacaDecimalToken("12abc"), "No numeric suffix");
static_assert(!alpacaDecimalToken("1,25"), "Culture-neutral decimal point");
static_assert(!alpacaDecimalToken("0x1p0"), "No hexadecimal floating point");
static_assert(!alpacaDecimalToken("NaN"), "No non-finite text");
static_assert(!alpacaDecimalToken("Infinity"), "No infinity text");
static_assert(!alpacaDecimalToken("1e"), "Exponent requires digits");
static_assert(!alpacaDecimalToken("1e+"), "Exponent sign requires digits");
static_assert(!alpacaDecimalToken("1 2"), "No embedded whitespace");
static_assert(keyLookup(), "One resolver handles transaction, State and Value casing");
static_assert(duplicateKey(), "Mixed-case duplicate keys rejected");
static_assert(missingKey(), "Missing key distinguished from empty value");
static_assert(!alpacaKeyEqual("ClientID", "ClientTransactionID"), "No prefix matching");
static_assert(embeddedNullValue(), "No truncated parse of embedded-NUL value");
static_assert(embeddedNullKey(), "No aliasing key with an embedded NUL");

// Also compile the real libc conversion/range checks; execution needs a target.
bool compileDoubleConversion(const char* text, double& value)
{
    return parseAlpacaDouble(text, value);
}
