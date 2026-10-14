#ifndef SKYBADGER_ALPACA_RESPONSE_H
#define SKYBADGER_ALPACA_RESPONSE_H

// An understood operation returns Alpaca JSON even when ErrorNumber is nonzero.
// Empty/failed serialization is a transport failure, not a successful response.
template <class Server, class Message>
constexpr void sendAlpacaJson(Server& server, const Message& message)
{
    if (message.length() == 0 || message == "null")
        server.send(500, "text/plain", "Unable to serialize Alpaca response");
    else
        server.send(200, "application/json", message);
}

#endif
