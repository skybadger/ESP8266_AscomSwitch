#include "alpaca_request_checks.cpp"
#include "../AlpacaConnection.h"

constexpr uint32_t TIMEOUT = 300000;
constexpr bool autoAcquire()
{
    AlpacaConnectionState s;
    return s.request(7, true, 100, TIMEOUT, false) == OWNERSHIP_OK
        && s.connected() && s.ownerClientID == 7 && s.timerArmed && s.lastActivityMs == 100;
}
constexpr bool zeroRejected(bool bypass)
{
    AlpacaConnectionState s;
    return s.request(0, true, 100, TIMEOUT, bypass) == OWNERSHIP_INVALID_CLIENT
        && !s.connected() && !s.timerArmed;
}
constexpr bool exclusiveOwner()
{
    AlpacaConnectionState s;
    s.request(1, true, 100, TIMEOUT, false);
    return s.request(2, true, 200, TIMEOUT, false) == OWNERSHIP_OTHER_CLIENT
        && s.ownerClientID == 1 && s.lastActivityMs == 100;
}
constexpr bool repeatedConnect()
{
    AlpacaConnectionState s;
    s.request(1, true, 100, TIMEOUT, false);
    return s.request(1, true, 200, TIMEOUT, false) == OWNERSHIP_OK
        && s.ownerClientID == 1 && s.lastActivityMs == 200 && s.timerArmed;
}
constexpr bool ownerDisconnect()
{
    AlpacaConnectionState s;
    s.request(1, true, 100, TIMEOUT, false);
    return s.disconnect(1, 200, TIMEOUT, false) == OWNERSHIP_OK
        && !s.connected() && s.ownerClientID == 0 && !s.timerArmed;
}
constexpr bool foreignDisconnect()
{
    AlpacaConnectionState s;
    s.request(1, true, 100, TIMEOUT, false);
    return s.disconnect(2, 200, TIMEOUT, false) == OWNERSHIP_OTHER_CLIENT
        && s.ownerClientID == 1 && s.lastActivityMs == 100 && s.timerArmed;
}
constexpr bool zeroDisconnect()
{
    AlpacaConnectionState s;
    s.request(1, true, 100, TIMEOUT, false);
    return s.disconnect(0, 200, TIMEOUT, true) == OWNERSHIP_INVALID_CLIENT
        && s.ownerClientID == 1 && s.timerArmed;
}
constexpr bool exactTimeout()
{
    AlpacaConnectionState s;
    s.request(1, true, 100, TIMEOUT, false);
    return !s.expire(300099, TIMEOUT) && s.connected()
        && s.expire(300100, TIMEOUT) && !s.connected() && !s.timerArmed;
}
constexpr bool ownerReadRenews()
{
    AlpacaConnectionState s;
    s.request(1, true, 0, TIMEOUT, false);
    s.request(1, false, 250000, TIMEOUT, false);
    return s.lastActivityMs == 250000 && !s.expire(549999, TIMEOUT) && s.expire(550000, TIMEOUT);
}
constexpr bool foreignReadDoesNotRenew()
{
    AlpacaConnectionState s;
    s.request(1, true, 0, TIMEOUT, false);
    return s.request(2, false, 250000, TIMEOUT, false) == OWNERSHIP_OK
        && s.lastActivityMs == 0 && s.expire(300000, TIMEOUT);
}
constexpr bool readDoesNotAcquire()
{
    AlpacaConnectionState s;
    return s.request(2, false, 100, TIMEOUT, false) == OWNERSHIP_OK && !s.connected() && !s.timerArmed;
}
constexpr bool readAfterTimeoutDoesNotAcquire()
{
    AlpacaConnectionState s;
    s.request(1, true, 0, TIMEOUT, false);
    return s.request(2, false, TIMEOUT, TIMEOUT, false) == OWNERSHIP_OK && !s.connected() && !s.timerArmed;
}
constexpr bool nextSettingAcquires()
{
    AlpacaConnectionState s;
    s.request(1, true, 0, TIMEOUT, false);
    return s.request(2, true, TIMEOUT, TIMEOUT, false) == OWNERSHIP_OK
        && s.ownerClientID == 2 && s.timerArmed && s.lastActivityMs == TIMEOUT;
}
constexpr bool idleTimerSuspended()
{
    AlpacaConnectionState s;
    s.request(1, true, 0, TIMEOUT, false);
    s.expire(TIMEOUT, TIMEOUT);
    return !s.expire(TIMEOUT * 2, TIMEOUT) && s.ownerClientID == 0 && s.lastActivityMs == 0 && !s.timerArmed;
}
constexpr bool rolloverTimeout()
{
    AlpacaConnectionState s;
    s.request(1, true, UINT32_MAX - 100000, TIMEOUT, false);
    return !s.expire(199998, TIMEOUT) && s.expire(199999, TIMEOUT) && !s.timerArmed && s.ownerClientID == 0;
}
constexpr bool bypassDoesNotStealOrRenew()
{
    AlpacaConnectionState s;
    s.request(1, true, 0, TIMEOUT, false);
    return s.request(2, true, 250000, TIMEOUT, true) == OWNERSHIP_OK
        && s.ownerClientID == 1 && s.lastActivityMs == 0 && s.expire(TIMEOUT, TIMEOUT);
}
constexpr bool bypassDisconnect()
{
    AlpacaConnectionState s;
    s.request(1, true, 0, TIMEOUT, false);
    return s.disconnect(2, 100, TIMEOUT, true) == OWNERSHIP_OK && !s.connected() && !s.timerArmed;
}
constexpr bool fullRangeOwner()
{
    AlpacaConnectionState s;
    return s.request(UINT32_MAX, true, 100, TIMEOUT, false) == OWNERSHIP_OK
        && s.ownerClientID == UINT32_MAX && s.connected();
}
constexpr bool customTimeout()
{
    AlpacaConnectionState s;
    s.request(1, true, 100, 2000, false);
    return !s.expire(2099, 2000) && s.expire(2100, 2000);
}
constexpr bool alreadyDisconnected()
{
    AlpacaConnectionState s;
    return s.disconnect(1, 100, TIMEOUT, false) == OWNERSHIP_OK
        && s.disconnect(2, 200, TIMEOUT, false) == OWNERSHIP_OK && !s.timerArmed;
}
static_assert(autoAcquire(), "Setting acquires a free device and arms timer");
static_assert(zeroRejected(false), "Zero is NOT_CONNECTED");
static_assert(zeroRejected(true), "Bypass cannot allow zero");
static_assert(exclusiveOwner(), "Foreign setting/connect is denied without renewing");
static_assert(repeatedConnect(), "Owner reconnect is idempotent and renews");
static_assert(ownerDisconnect(), "Owner release clears ownership and suspends timer");
static_assert(foreignDisconnect(), "Foreign client cannot release ownership");
static_assert(zeroDisconnect(), "Zero cannot release an owner even with bypass");
static_assert(exactTimeout(), "Expires at exactly 300 seconds");
static_assert(ownerReadRenews(), "Any owner API call refreshes deadline");
static_assert(foreignReadDoesNotRenew(), "Foreign reads cannot retain ownership");
static_assert(readDoesNotAcquire(), "Only setters or Connected=true acquire");
static_assert(readAfterTimeoutDoesNotAcquire(), "Read after expiry leaves timer suspended");
static_assert(nextSettingAcquires(), "Next setting acquires after timeout");
static_assert(idleTimerSuspended(), "Expired lease is not repeatedly timed");
static_assert(rolloverTimeout(), "millis rollover preserves deadline");
static_assert(bypassDoesNotStealOrRenew(), "Bypass preserves the owner's lease");
static_assert(bypassDisconnect(), "Bypass permits release by another positive ID");
static_assert(fullRangeOwner(), "All positive uint32 IDs supported");
static_assert(customTimeout(), "Global timeout can change");
static_assert(alreadyDisconnected(), "Connected=false is idempotent and never acquires");
