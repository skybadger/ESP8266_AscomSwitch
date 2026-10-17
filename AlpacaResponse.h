#ifndef SKYBADGER_ALPACA_RESPONSE_H
#define SKYBADGER_ALPACA_RESPONSE_H

// An understood operation returns Alpaca JSON even when ErrorNumber is nonzero.
// Empty/failed serialization is a transport failure, not a successful response.
/**
 * @brief Send a serialized Alpaca response.
 * @details Send JSON on successful serialization or an HTTP failure when the message is empty.
 * @param[in,out] server (Server&) HTTP server/request adapter used for argument lookup or delivery.
 * @param[in] message (const Message&) Response text buffer or assertion explanation.
 * @return void; effects are described above.
 */
template <class Server, class Message>
constexpr void sendAlpacaJson(Server &server, const Message &message)
{
    if (message.length() == 0 || message == "null")
        server.send(500, "text/plain", "Unable to serialize Alpaca response");
    else
        server.send(200, "application/json", message);
}

#endif
