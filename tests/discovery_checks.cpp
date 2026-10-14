#include "../../libraries/ASCOM_rest/AlpacaDiscovery.h"

constexpr uint8_t valid[] = "alpacadiscovery1";
constexpr uint8_t wrongLast[] = "alpacadiscoverx1";
constexpr uint8_t wrongVersion[] = "alpacadiscovery2";
constexpr uint8_t wrongCase[] = "Alpacadiscovery1";
constexpr uint8_t wrongFirst[] = "xlpacadiscovery1";
constexpr uint8_t reserved[64] = {'a','l','p','a','c','a','d','i','s','c','o','v','e','r','y','1',255};
static_assert(validAlpacaDiscoveryRequest(valid, 16), "Current version header");
static_assert(validAlpacaDiscoveryRequest(reserved, 64), "Reserved bytes ignored");
static_assert(validAlpacaDiscoveryRequest(valid, 17), "Optional trailing byte");
static_assert(!validAlpacaDiscoveryRequest(valid, 15), "Short header rejected");
static_assert(!validAlpacaDiscoveryRequest(valid, 0), "Empty request rejected");
static_assert(!validAlpacaDiscoveryRequest(nullptr, 16), "Null rejected");
static_assert(!validAlpacaDiscoveryRequest(reserved, 65), "Oversized rejected before access");
static_assert(!validAlpacaDiscoveryRequest(wrongLast, 16), "All signature bytes checked");
static_assert(!validAlpacaDiscoveryRequest(wrongVersion, 16), "Unsupported version rejected");
static_assert(!validAlpacaDiscoveryRequest(wrongCase, 16), "Signature is case sensitive");
static_assert(!validAlpacaDiscoveryRequest(wrongFirst, 16), "Wrong signature rejected");
