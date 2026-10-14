// Compile with the actual ESP8266 toolchain; this is not a host-size test.
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static_assert(sizeof(long) == 4, "Expected ESP8266's 32-bit long");
static_assert(sizeof(long long) == 8, "ESP8266 toolchain supports 64-bit long long");
static_assert(sizeof(time_t) == 8, "This Arduino core uses 64-bit time_t");
static_assert((time_t)4294967296LL > (time_t)2147483647L,
              "time_t must retain seconds beyond the 32-bit range");

int checkTimestampFormat(char* output, size_t capacity, time_t seconds, int milliseconds)
{
    return snprintf(output, capacity, "%lld.%03i", (long long) seconds, milliseconds);
}
