#ifndef SKYBADGER_MQTT_SETTINGS_H
#define SKYBADGER_MQTT_SETTINGS_H
//EEPROM space permits a hostname or IPv4 address of up to 63 characters.
char mqttServerName[64] = {};
uint16_t mqttPort = 1883;
bool mqttEndpointChanged = false;
/**
 * @brief Validate a bounded MQTT broker name.
 * @details Accept DNS-style labels or IPv4 text and reject empty, oversized or malformed names.
 * @param[in] host (const char*) Null-terminated broker hostname or IPv4 text.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
bool validMqttServer(const char *host)
{
    if (!host || !*host || strlen(host) >= sizeof(mqttServerName))
        return false;
    bool start = true;
    char previous = 0;
    for (const char *p = host; *p; ++p)
    {
        const char c = *p;
        const bool alnum =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (c == '.')
        {
            if (start || previous == '-')
                return false;
            start = true;
        }
        else
        {
            if (!alnum && c != '-')
                return false;
            if (start && c == '-')
                return false;
            start = false;
        }
        previous = c;
    }
    return !start && previous != '-';
}

/**
 * @brief Restore default MQTT endpoint settings.
 * @details Copy the compiled broker into permanent storage, select port 1883 and clear pending changes.
 * @param[in] defaultServer (const char*) Compiled default broker name.
 * @return void; effects are described above.
 */
void resetMqttSettings(const char *defaultServer)
{
    strncpy(mqttServerName, defaultServer, sizeof(mqttServerName) - 1);
    mqttServerName[sizeof(mqttServerName) - 1] = 0;
    mqttPort = 1883;
    mqttEndpointChanged = false;
}

/**
 * @brief Apply a pending MQTT endpoint change.
 * @details Disconnect the old session, use the permanent host buffer for setServer and clear the pending flag.
 * @param[in,out] mqtt (Client&) PubSubClient-compatible MQTT client.
 * @return (bool) True if a pending endpoint was applied; false when unchanged.
 */
template <class Client> bool applyMqttEndpoint(Client &mqtt)
{
    if (!mqttEndpointChanged)
        return false;
    mqtt.disconnect();
    //PubSubClient retains this pointer; it must reference permanent storage.
    mqtt.setServer(mqttServerName, mqttPort);
    mqttEndpointChanged = false;
    return true;
}
#endif
