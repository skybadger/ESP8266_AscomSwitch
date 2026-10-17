#ifndef SKYBADGER_MQTT_SETUP_H
#define SKYBADGER_MQTT_SETUP_H
/**
 * @brief Render MQTT broker configuration controls.
 * @details Insert current escaped broker name and port into the setup POST form.
 * @param[in,out] html (String&) HTML buffer to append to or replace.
 * @return (String&) Generated text or the updated String reference.
 */
String &mqttSetupForm(String &html)
{
    html +=
        "<section><h2>MQTT broker</h2><p>Saving reconnects MQTT without rebooting.</p>"
        "<form method=\"POST\" action=\"/setup/mqtt\">"
        "<label>Server name or IPv4 address <input type=\"text\" name=\"mqttServer\" maxlength=\"63\" required value=\"";
    html += escapedMappingHtml(mqttServerName);
    html +=
        "\"></label><label>Port <input type=\"number\" name=\"mqttPort\" min=\"1\" max=\"65535\" required value=\"";
    html += String(mqttPort);
    html += "\"></label><input type=\"submit\" value=\"Save MQTT settings\"></form></section>";
    return html;
}

/**
 * @brief Apply MQTT settings submitted through setup.
 * @details Validate unique broker/port arguments, update live values, request reconnection and save EEPROM.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDeviceMqtt()
{
    if (!validateSettingHandlerRequest("ConfigureMqtt"))
        return;
    String host, portText, message, error;
    uint32_t port = 0;
    if (!mappingArg("mqttServer", host) || !mappingArg("mqttPort", portText) ||
        //Reject embedded NUL as well as invalid/duplicate arguments.
        host.length() != strlen(host.c_str()) || !validMqttServer(host.c_str()) ||
        !parseAlpacaUInt32(portText.c_str(), port) || !port || port > 65535)
    {
        error = "Enter a server name/IPv4 address (1-63 characters) and port 1-65535";
        sendDeviceSetup(400, message, error);
        return;
    }
    const bool changed = strcmp(mqttServerName, host.c_str()) || mqttPort != port;
    memcpy(mqttServerName, host.c_str(), host.length() + 1);
    mqttPort = (uint16_t)port;
    mqttEndpointChanged = mqttEndpointChanged || changed;
    saveToEeprom();
    if (!sourceStorageValid)
        error = sourceStorageMessage;
    sendDeviceSetup(error.length() ? 500 : 200, message, error);
}
#endif
