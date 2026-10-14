#include "../AlpacaResponse.h"

constexpr bool equalText(const char* left, const char* right)
{
    while (*left && *left == *right) { ++left; ++right; }
    return *left == *right;
}

struct TestMessage {
    const char* text;
    constexpr unsigned int length() const {
        unsigned int result = 0;
        while (text[result]) ++result;
        return result;
    }
    constexpr bool operator==(const char* other) const { return equalText(text, other); }
};

struct TestServer {
    int code = 0;
    const char* type = "";
    const char* body = "";
    constexpr void send(int status, const char* contentType, const char* payload) {
        code = status; type = contentType; body = payload;
    }
    constexpr void send(int status, const char* contentType, const TestMessage& payload) {
        send(status, contentType, payload.text);
    }
};

constexpr bool checkJson(const char* payload)
{
    TestServer server;
    sendAlpacaJson(server, TestMessage{payload});
    return server.code == 200 && equalText(server.type, "application/json")
        && equalText(server.body, payload);
}

constexpr bool checkSerializationFailure(const char* payload)
{
    TestServer server;
    sendAlpacaJson(server, TestMessage{payload});
    return server.code == 500 && equalText(server.type, "text/plain")
        && equalText(server.body, "Unable to serialize Alpaca response");
}

static_assert(checkJson("{\"Value\":true,\"ErrorNumber\":0}"), "Successful operation is JSON/200");
static_assert(checkJson("{\"ErrorNumber\":1025,\"ErrorMessage\":\"Invalid Id\"}"),
              "Invalid argument is JSON/200 with its error body preserved");
static_assert(checkJson("{\"ErrorNumber\":1024,\"ErrorMessage\":\"Read-only\"}"),
              "Unsupported write is JSON/200 with its error body preserved");
static_assert(checkSerializationFailure(""), "Empty body must not be sent as HTTP 200");
static_assert(checkSerializationFailure("null"), "Failed JSON allocation is HTTP 500/plain text");
