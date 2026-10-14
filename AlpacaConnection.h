#ifndef SKYBADGER_ALPACA_CONNECTION_H
#define SKYBADGER_ALPACA_CONNECTION_H

#include <stdint.h>
enum AlpacaOwnershipResult { OWNERSHIP_OK, OWNERSHIP_INVALID_CLIENT, OWNERSHIP_OTHER_CLIENT };

struct AlpacaConnectionState {
    uint32_t ownerClientID = 0; //0 is NOT_CONNECTED, never a valid owner.
    uint32_t lastActivityMs = 0;
    bool timerArmed = false;

    constexpr bool connected() const { return ownerClientID != 0; }

    constexpr void clear()
    {
        ownerClientID = 0;
        lastActivityMs = 0;
        timerArmed = false;
    }

    constexpr bool expire(uint32_t now, uint32_t timeoutMs)
    {
        if (timerArmed && (uint32_t)(now - lastActivityMs) >= timeoutMs)
        {
            clear();
            return true;
        }
        return false;
    }

    constexpr AlpacaOwnershipResult request(uint32_t clientID, bool setting, uint32_t now,
                                           uint32_t timeoutMs, bool allowAnyClient)
    {
        expire(now, timeoutMs);
        if (clientID == 0) return OWNERSHIP_INVALID_CLIENT;
        if (setting && connected() && ownerClientID != clientID && !allowAnyClient)
            return OWNERSHIP_OTHER_CLIENT;
        if (setting && !connected()) ownerClientID = clientID;
        //Only the actual owner renews its lease, including on read-only API calls.
        if (ownerClientID == clientID)
        {
            lastActivityMs = now;
            timerArmed = true;
        }
        return OWNERSHIP_OK;
    }

    constexpr AlpacaOwnershipResult disconnect(uint32_t clientID, uint32_t now,
                                              uint32_t timeoutMs, bool allowAnyClient)
    {
        expire(now, timeoutMs);
        if (clientID == 0) return OWNERSHIP_INVALID_CLIENT;
        if (connected() && ownerClientID != clientID && !allowAnyClient)
            return OWNERSHIP_OTHER_CLIENT;
        clear();
        return OWNERSHIP_OK;
    }
};

#endif
