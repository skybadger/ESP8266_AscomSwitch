#ifndef SKYBADGER_ALPACA_CONNECTION_H
#define SKYBADGER_ALPACA_CONNECTION_H

#include <stdint.h>
enum AlpacaOwnershipResult
{
    OWNERSHIP_OK,
    OWNERSHIP_INVALID_CLIENT,
    OWNERSHIP_OTHER_CLIENT
};

struct AlpacaConnectionState
{
    uint32_t ownerClientID = 0; //0 is NOT_CONNECTED, never a valid owner.
    uint32_t lastActivityMs = 0;
    bool timerArmed = false;

    /**
     * @brief Report whether a client owns the device.
     * @details Test the stored positive owner identifier without changing the lease.
     * @par Arguments
     * None.
     * @return (bool) True when the described check/operation succeeds; otherwise false.
     */
    constexpr bool connected() const
    {
        return ownerClientID != 0;
    }

    /**
     * @brief Release the current client lease.
     * @details Clear the owner identifier and activity timestamp.
     * @par Arguments
     * None.
     * @return void; effects are described above.
     */
    constexpr void clear()
    {
        ownerClientID = 0;
        lastActivityMs = 0;
        timerArmed = false;
    }

    /**
     * @brief Expire an inactive client lease.
     * @details Compare unsigned elapsed milliseconds with the configured timeout and clear expired ownership.
     * @param[in] now (uint32_t) Current monotonic time in milliseconds.
     * @param[in] timeoutMs (uint32_t) Lease timeout in milliseconds.
     * @return (bool) True only when this call expires an active lease.
     */
    constexpr bool expire(uint32_t now, uint32_t timeoutMs)
    {
        if (timerArmed && (uint32_t)(now - lastActivityMs) >= timeoutMs)
        {
            clear();
            return true;
        }
        return false;
    }

    /**
     * @brief Validate or acquire the client lease.
     * @details Expire stale ownership, reject invalid or foreign setting clients, and renew owner activity.
     * @param[in] clientID (uint32_t) Positive caller identifier; zero is not connected.
     * @param[in] setting (bool) True for an operation that may acquire ownership.
     * @param[in] now (uint32_t) Current monotonic time in milliseconds.
     * @param[in] timeoutMs (uint32_t) Lease timeout in milliseconds.
     * @param[in] allowAnyClient (bool) Permit foreign clients to use an existing connection when true.
     * @return (AlpacaOwnershipResult) Ownership policy result; settings may acquire an unowned lease.
     */
    constexpr AlpacaOwnershipResult request(uint32_t clientID, bool setting, uint32_t now,
                                            uint32_t timeoutMs, bool allowAnyClient)
    {
        expire(now, timeoutMs);
        if (clientID == 0)
            return OWNERSHIP_INVALID_CLIENT;
        if (setting && connected() && ownerClientID != clientID && !allowAnyClient)
            return OWNERSHIP_OTHER_CLIENT;
        if (setting && !connected())
            ownerClientID = clientID;
        //Only the actual owner renews its lease, including on read-only API calls.
        if (ownerClientID == clientID)
        {
            lastActivityMs = now;
            timerArmed = true;
        }
        return OWNERSHIP_OK;
    }

    /**
     * @brief Release ownership for an authorized client.
     * @details Apply timeout and caller policy before clearing the lease.
     * @param[in] clientID (uint32_t) Positive caller identifier; zero is not connected.
     * @param[in] now (uint32_t) Current monotonic time in milliseconds.
     * @param[in] timeoutMs (uint32_t) Lease timeout in milliseconds.
     * @param[in] allowAnyClient (bool) Permit foreign clients to use an existing connection when true.
     * @return (AlpacaOwnershipResult) Ownership policy result; an authorized release clears the lease.
     */
    constexpr AlpacaOwnershipResult disconnect(uint32_t clientID, uint32_t now, uint32_t timeoutMs,
                                               bool allowAnyClient)
    {
        expire(now, timeoutMs);
        if (clientID == 0)
            return OWNERSHIP_INVALID_CLIENT;
        if (connected() && ownerClientID != clientID && !allowAnyClient)
            return OWNERSHIP_OTHER_CLIENT;
        clear();
        return OWNERSHIP_OK;
    }
};

#endif
