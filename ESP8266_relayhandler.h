/*
ESP8266_relayhandler.h
This is a firmware application to implement the ASCOM ALPACA switch interface API.
Each CPU is attached to a number of built-in devices which is based on the the chip in use and the number of free pins brought out for connections 
when the base board or pcb is built. 
On top of that other i2c devices can be added a sin put or outputs or both dependning on function. 
I thought it would be useful to asusme use of PCF8574 as an i2c 8-bit port expander supporting binary in/out, MCP4725 analogue DAC and ADS1105/ADS115 as 4-channel 
ADC to measure both internal or external voltages. 
The number of logical switch ports  is specified in numswitches which is user configured to a max of 16 which is to manage the memory required. 
Each nominal switch can be 1 of 4 types 
- binary relays (no and nc)
- binary logica levels in and out
- digital pwm
- ADC - analogue in 
- i2c -based functions such as the above but also ADC, DAC, magnetic , other stuff. 
Hence the setup allows specifying the number of switches per device and the host/device name.

This particular switch device assumes the use of port pins allocated from the device via a pcf8574 I2C port expansion device. 
Using an ESP8266-01 this leaves one pin free to be a digital device. 
Using an ESP8266-12 this leaves a number of pins free to be a digital device.
Use of a DAC requires (presumably) an i2C DAC or an onboard DAC tied to a pin.

In use of the ASCOM API, all URLs include an argument 'Id' which contains the number of the switch attached to this device instance
The switch device number is in the path itself. Hence the getUriField function
Internally this code keeps the state of the switch in the switchEntry structure and uses (value != 1.0F) to mean false. 

 To do:
 Debug, trial
 
 Layout: 
 (ESP8266-12)
 GPIO 4,2 to SDA
 GPIO 5,0 to SCL 
 (ESP8266-01)
 GPIO 0 - SDA
 GPIO 1 - Rx - re-use as PWM output for testing purposes
 GPIO 2 - SCL
 GPIO 3 - Tx
 All 3.3v logic. 
  
*/

#ifndef _ESP8266_RELAYHANDLER_H_
#define _ESP8266_RELAYHANDLER_H_

#include "Webrelay_eeprom.h"
#include <Wire.h>
#include "AlpacaErrorConsts.h"
#include "ASCOMAPISwitch_rest.h"
#include "RelayOutput.h"
#include "PwmOutput.h"
#include "AlpacaResponse.h"
#include "AlpacaRequest.h"
#include "SetupPageStyle.h"
#include "MappingPageScript.h"
String escapedMappingHtml(const String &);
void handlerMappedSetupSwitches();
String &mappedSwitchForm(String &, int);
String &mappedInventoryForm(String &);
#include <errno.h>
#include <stdlib.h>

/**
 * @brief Serialize and send a Switch API response.
 * @details Stamp the server transaction identifier and delegate JSON serialization/HTTP delivery.
 * @param[in,out] root (JsonObject&) Response JSON object updated by the operation.
 * @param[in,out] message (String&) Response text buffer or assertion explanation.
 * @return void; effects are described above.
 */
void sendSwitchResponse(JsonObject &root, String &message)
{
    message = "";
    ASCOM_JSON_SERIALIZE(root, message);
    sendAlpacaJson(server, message);
}

/**
 * @brief Return an invalid-argument Switch response.
 * @details Set error fields and send the response through the common sender.
 * @param[in,out] root (JsonObject&) Response JSON object updated by the operation.
 * @param[in,out] message (String&) Response text buffer or assertion explanation.
 * @param[in] error (const char*) Failure code or human-readable failure explanation.
 * @return (bool) Always false, after sending the error response.
 */
bool switchRequestError(JsonObject &root, String &message, const char *error)
{
    root["ErrorNumber"] = invalidValue;
    root["ErrorMessage"] = error;
    sendSwitchResponse(root, message);
    return false;
}

/**
 * @brief Keep connection ownership current without HTTP traffic.
 * @details Expire the client lease from elapsed time and synchronize the legacy connection indicator.
 * @par Arguments
 * None.
 * @return void; effects are described above.
 */
void checkAlpacaConnectionTimeout()
{
    alpacaConnection.expire(millis(), connectedTimeoutMs);
    connectionCtr = alpacaConnection.connected() ? 1 : 0;
}

/**
 * @brief Enforce the REST client ownership policy.
 * @details Delegate acquisition/renewal to the lease and translate ownership results into Alpaca errors.
 * @param[in] clientID (uint32_t) Positive caller identifier; zero is not connected.
 * @param[in] setting (bool) True for an operation that may acquire ownership.
 * @return (int) Zero on success or an Alpaca error code.
 */
int validateAlpacaRequestOwnership(uint32_t clientID, bool setting)
{
    const auto result = alpacaConnection.request(clientID, setting, millis(), connectedTimeoutMs,
                                                 allowAnyConnectedClient);
    connectionCtr = alpacaConnection.connected() ? 1 : 0;
    if (result == OWNERSHIP_INVALID_CLIENT)
        return invalidValue;
    if (result == OWNERSHIP_OTHER_CLIENT)
        return notConnected;
    return Success;
}

/**
 * @brief Describe an ownership validation failure.
 * @details Choose the invalid-client or foreign-owner explanation for an Alpaca error code.
 * @param[in] error (int) Failure code or human-readable failure explanation.
 * @return (const char*) Borrowed constant explanation string.
 */
const char *ownershipErrorMessage(int error)
{
    return error == invalidValue ? "ClientID must be supplied and greater than zero"
                                 : "Device is owned by another ClientID";
}

/**
 * @brief Validate a Switch REST request before handling it.
 * @details Parse transaction/client fields, initialize response metadata and enforce the appropriate lease policy.
 * @param[in,out] root (JsonObject&) Response JSON object updated by the operation.
 * @param[in,out] message (String&) Response text buffer or assertion explanation.
 * @param[in] method (const char*) API operation name for response metadata.
 * @param[in] forceSetting (bool) Treat the operation as a setting when true.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
bool beginSwitchRequest(JsonObject &root, String &message, const char *method,
                        bool forceSetting = false)
{
    uint32_t clientID = 0;
    uint32_t transID = 0;
    const bool clientValid =
        parseAlpacaOptionalUInt32<ESP8266WebServer, String>(server, "ClientID", clientID);
    const bool transValid =
        parseAlpacaOptionalUInt32<ESP8266WebServer, String>(server, "ClientTransactionID", transID);
    jsonResponseBuilder(root, clientID, transID, serverTransID++, method, Success, "");
    if (!clientValid || !transValid)
        return switchRequestError(root, message,
                                  "Invalid or duplicate ClientID/ClientTransactionID");
    //Connected PUT manages acquire/release explicitly after parsing its boolean.
    const bool connectionCommand = strcmp(method, "Connected") == 0 && server.method() == HTTP_PUT;
    const bool setting = !connectionCommand && (forceSetting || server.method() == HTTP_PUT ||
                                                server.method() == HTTP_POST);
    const int ownershipError = validateAlpacaRequestOwnership(clientID, setting);
    if (ownershipError != Success)
    {
        root["ErrorNumber"] = ownershipError;
        root["ErrorMessage"] = ownershipErrorMessage(ownershipError);
        sendSwitchResponse(root, message);
        return false;
    }
    return true;
}

/**
 * @brief Validate a shared REST or setup setting request.
 * @details Bypass the lease for setup/restart paths; direct API calls use the common request validation.
 * @param[in] method (const char*) API operation name for response metadata.
 * @param[in] forceSetting (bool) Treat the operation as a setting when true.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
bool validateSettingHandlerRequest(const char *method, bool forceSetting = true)
{
    //Setup is independent of the Alpaca client lease. Shared API handlers still
    //validate ownership when reached through their /api/... routes.
    if (server.uri().startsWith("/setup/") || server.uri() == "/restart")
    {
        return true;
    }
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    return beginSwitchRequest(root, message, method, forceSetting);
}

/**
 * @brief Renew activity for a supplied owner identifier.
 * @details Parse an optional positive ClientID and validate it as a read without acquiring ownership.
 * @par Arguments
 * None.
 * @return void; effects are described above.
 */
void recordAlpacaReadActivity()
{
    uint32_t clientID = 0;
    if (parseAlpacaOptionalUInt32<ESP8266WebServer, String>(server, "ClientID", clientID) &&
        clientID > 0)
        validateAlpacaRequestOwnership(clientID, false);
}

/**
 * @brief Serve the Alpaca Connected property.
 * @details Read Boolean ownership state or validate a requested connect/disconnect transition and send JSON.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handleConnected(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    if (!beginSwitchRequest(root, message, "Connected"))
        return;
    if (server.method() == HTTP_PUT)
    {
        String text;
        const uint32_t clientID = root["ClientID"];
        bool requested = false;
        if (findAlpacaArgument(server, "Connected", text) != ALPACA_ARG_PRESENT ||
            !parseAlpacaBool(text.c_str(), requested))
        {
            switchRequestError(root, message, "Connected must be a single true or false argument");
            return;
        }
        const auto result =
            requested ? alpacaConnection.request(clientID, true, millis(), connectedTimeoutMs,
                                                 allowAnyConnectedClient)
                      : alpacaConnection.disconnect(clientID, millis(), connectedTimeoutMs,
                                                    allowAnyConnectedClient);
        connectionCtr = alpacaConnection.connected() ? 1 : 0;
        if (result != OWNERSHIP_OK)
        {
            const int error = result == OWNERSHIP_INVALID_CLIENT ? invalidValue : notConnected;
            root["ErrorNumber"] = error;
            root["ErrorMessage"] = ownershipErrorMessage(error);
            sendSwitchResponse(root, message);
            return;
        }
    }
    root["Value"] = alpacaConnection.connected();
    sendSwitchResponse(root, message);
}

/**
 * @brief Require an active lease for a Switch operation.
 * @details Send a NotConnected response when the current owner is absent.
 * @param[in,out] root (JsonObject&) Response JSON object updated by the operation.
 * @param[in,out] message (String&) Response text buffer or assertion explanation.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
bool requireConnectedSwitch(JsonObject &root, String &message)
{
    if (alpacaConnection.connected())
        return true;
    root["ErrorNumber"] = notConnected;
    root["ErrorMessage"] = "Switch device is not connected";
    sendSwitchResponse(root, message);
    return false;
}

/**
 * @brief Validate and resolve a logical Switch Id.
 * @details Read one unsigned Id and reject values outside the advertised channel table.
 * @param[in,out] root (JsonObject&) Response JSON object updated by the operation.
 * @param[in,out] message (String&) Response text buffer or assertion explanation.
 * @param[out] id (int&) Validated zero-based logical Id, assigned on success.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
bool readSwitchId(JsonObject &root, String &message, int &id)
{
    String text;
    uint32_t parsed = 0;
    if (findAlpacaArgument(server, "Id", text) != ALPACA_ARG_PRESENT ||
        !parseAlpacaUInt32(text.c_str(), parsed) || parsed > 32767 || numSwitches <= 0 ||
        parsed >= (uint32_t)numSwitches)
        return switchRequestError(root, message,
                                  "Missing, duplicate, malformed or out-of-range Id");
    id = (int)parsed;
    return true;
}

/**
 * @brief Read a strict Switch State argument.
 * @details Require one complete Boolean token before allowing output dispatch.
 * @param[in,out] root (JsonObject&) Response JSON object updated by the operation.
 * @param[in,out] message (String&) Response text buffer or assertion explanation.
 * @param[out] state (bool&) Parsed requested Boolean state, assigned on success.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
bool readSwitchBool(JsonObject &root, String &message, bool &state)
{
    String text;
    if (findAlpacaArgument(server, "State", text) != ALPACA_ARG_PRESENT ||
        !parseAlpacaBool(text.c_str(), state))
        return switchRequestError(root, message, "State must be a single true or false argument");
    return true;
}

/**
 * @brief Read a strict numeric Switch Value argument.
 * @details Require one finite decimal token before allowing output dispatch.
 * @param[in,out] root (JsonObject&) Response JSON object updated by the operation.
 * @param[in,out] message (String&) Response text buffer or assertion explanation.
 * @param[out] value (double&) Parsed requested numeric value, assigned on success.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
bool readSwitchValue(JsonObject &root, String &message, double &value)
{
    String text;
    if (findAlpacaArgument(server, "Value", text) != ALPACA_ARG_PRESENT ||
        !parseAlpacaDouble(text.c_str(), value))
        return switchRequestError(root, message, "Value must be a single finite decimal number");
    return true;
}

/**
 * @brief Enforce source capability and user write policy.
 * @details Reject unbound/read-only channels before parsing or performing an output operation.
 * @param[in] id (int) Zero-based logical channel identifier.
 * @param[in,out] root (JsonObject&) Response JSON object updated by the operation.
 * @param[in,out] message (String&) Response text buffer or assertion explanation.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
bool requireWritableSwitch(int id, JsonObject &root, String &message)
{
    if (switchEntry[id]->binding.source.kind != Kind::None && switchEntry[id]->writeable &&
        SourceMapping::isOutput(switchEntry[id]->binding.mode))
        return true;
    root["ErrorNumber"] = notImplemented;
    root["ErrorMessage"] = "This switch is read-only";
    sendSwitchResponse(root, message);
    return false;
}

/**
 * @brief Translate mapped hardware results into Alpaca fields.
 * @details Keep successful results or attach the appropriate error and omit invalid Value data.
 * @param[in] result (Result) Operation status or returned sample value.
 * @param[in,out] root (JsonObject&) Response JSON object updated by the operation.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
bool mappedResponseResult(Result result, JsonObject &root)
{
    if (result == Result::Ok)
        return true;
    root.remove("Value");
    root["ErrorNumber"] = result == Result::ReadOnly ? notImplemented
                          : result == Result::InvalidSource || result == Result::InvalidSwitchId
                              ? invalidValue
                              : 0x500;
    root["ErrorMessage"] = sourceResultMessage(result);
    return false;
}

/**
 * @brief Apply a logical binary channel command.
 * @details Dispatch the logical Id through mapped hardware and translate the resulting status.
 * @param[in] switchID (unsigned int) Zero-based logical channel identifier.
 * @param[in] value (double) Input value or parsed/output value as indicated by reference and operation.
 * @param[in,out] root (JsonObject&) Response JSON object updated by the operation.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
bool setRelayOutput(unsigned int switchID, double value, JsonObject &root)
{
    return mappedResponseResult(writeMappedValue(switchID, value), root);
}

/**
 * @brief Apply a logical PWM channel command.
 * @details Dispatch the validated value through mapped hardware and translate its status.
 * @param[in] switchID (unsigned int) Zero-based logical channel identifier.
 * @param[in] value (double) Input value or parsed/output value as indicated by reference and operation.
 * @param[in,out] root (JsonObject&) Response JSON object updated by the operation.
 * @return (bool) True when the described check/operation succeeds; otherwise false.
 */
bool setPwmOutput(unsigned int switchID, double value, JsonObject &root)
{
    return mappedResponseResult(writeMappedValue(switchID, value), root);
}

//Function definitions
void copySwitch(SwitchEntry *sourceSe, SwitchEntry *targetSe);
void initSwitch(SwitchEntry *targetSe);
SwitchEntry **reSize(SwitchEntry **old, int newSize);
bool getUriField(char *inString, int searchIndex, String &outRef);

//Deprecated in favour of splitting up in to separate chunks.
//String& setupFormBuilder( String& htmlForm, String& errMsg );

String &setupFormBuilderHeader(String &htmlForm);
String &setupFormBuilderDeviceHeader(String &htmlForm, String &errMsg); //For device setup
String &setupFormBuilderDeviceStrings(String &htmlForm);
String &setupFormBuilderDriverHeader(
    String &htmlForm,
    String &errMsg); //For per-driver setup when we have 1 or more drivers attached
String &setupFormBuilderDriver0Switches(String &htmlForm, int index);
String &setupFormBuilderFooter(String &htmlForm);

//Device settings
void sendDeviceSetup(int returnCode, String &message, String &err);
void handlerDeviceSetup(void);
void handlerDeviceHostname(void);
void handlerDeviceMqtt(void);
String &mqttSetupForm(String &html);
void handlerDeviceLocation(void);
void handlerDeviceUdpPort(void);

//Driver0 settings
void sendDriver0Setup(int returnCode, String &message, String &err);
void handlerDriver0Setup(void);
void handlerDriver0SetupNumSwitches(void);
void handlerDriver0Maxswitch(void);
void handlerDriver0CanWrite(void);
void handlerDriver0SwitchState(void);
void handlerDriver0SwitchDescription(void);
void handlerDriver0SwitchName(void);
void handlerDriver0SwitchType(void);
void handlerDriver0SwitchValue(void);
void handlerDriver0MinSwitchValue(void);
void handlerDriver0MaxSwitchValue(void);
void handlerDriver0SwitchStep(void);
void handlerDriver0SetupSwitches(void);

void handlerNotFound();
void handlerRestart();
void handlerNotImplemented();
void handlerStatus(void);

/*
 * This function will write a copy of the provided deviceEntry structure into the internal memory array. 
 * switch Entry may be a pointer to a single item or an array of items. ID must be in range;
 */
/**
 * @brief Copy a channel configuration into an allocated entry.
 * @details Copy scalar/binding state and bounded strings without sharing name storage.
 * @param[in] sourceSe (SwitchEntry*) Source entry with separately owned string buffers.
 * @param[in,out] targetSe (SwitchEntry*) Allocated destination channel entry.
 * @return void; effects are described above.
 */
void copySwitch(SwitchEntry *sourceSe, SwitchEntry *targetSe)
{
    char *name = targetSe->switchName;
    char *description = targetSe->description;
    *targetSe = *sourceSe;
    targetSe->switchName = name;
    targetSe->description = description;
    copyConfigString(name, sourceSe->switchName);
    copyConfigString(description, sourceSe->description);
    targetSe->sample.valid = false;
}

/**
 * @brief Initialize an allocated channel entry.
 * @details Reset binding, cached sample, calibration and numeric limits; retain allocated text buffers.
 * @param[in,out] targetSe (SwitchEntry*) Allocated destination channel entry.
 * @return void; effects are described above.
 */
void initSwitch(SwitchEntry *targetSe)
{
    targetSe->binding = {};
    targetSe->sample = {};
    targetSe->scale = 1;
    targetSe->offset = 0;
    targetSe->min = 0;
    targetSe->max = 1;
    targetSe->step = 1;
    targetSe->value = 0;
}
/*
 * This function re-sizes the existing switchEntry array by re-allocating memory and copying if required. 
 * returns pointer to array of switches
 */
/**
 * @brief Resize the bounded channel table.
 * @details Delegate allocation/removal to the mapped storage allocator and return the resulting table.
 * @param[in] old (SwitchEntry**) Previous pointer table, retained for the compatibility signature.
 * @param[in] newSize (int) Requested total channel count.
 * @return (SwitchEntry**) Current global channel table; allocation failure is not separately reported by this legacy wrapper.
 */
SwitchEntry **reSize(SwitchEntry **old, int newSize)
{
    (void)old;
    pendingAdcId = -1;
    allocateMappedSwitches(newSize);
    ++sourceInventoryRevision;
    return switchEntry;
}

/**
 * @brief Extract a numbered URI component.
 * @details Walk separators in the supplied URI and copy the requested component to the output String.
 * @param[in,out] inString (char*) Mutable null-terminated URI to inspect.
 * @param[in] searchIndex (int) Zero-based URI component number.
 * @param[out] outRef (String&) Extracted URI component, assigned when the requested index exists.
 * @return (bool) True when the requested URI field exists.
 */
bool getUriField(char *inString, int searchIndex, String &outRef)
{
    char *p = inString;
    char *str;
    char delims1[] = {"//"};
    char delims2[] = {"/:"};
    int chunkCtr = 0;
    bool status = false;
    int localIndex = 0;

    localIndex = String(inString).indexOf(delims1);
    if (localIndex >= 0)
    {
        while ((str = strtok_r(p, delims2, &p)) != NULL) // delimiter is the semicolon
        {
            if (chunkCtr == searchIndex && !status)
            {
                outRef = String(str);
                status = true;
            }
            chunkCtr++;
        }
    }
    else
        status = false;

    return status;
}

//GET ​/switch​/{device_number}​/maxswitch
//The number of switch devices managed by this driver
/**
 * @brief Serve the maximum advertised Switch count.
 * @details Validate request metadata and return the total logical channel count.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDriver0Maxswitch(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    if (!beginSwitchRequest(root, message, "MaxSwitch"))
        return;
    root["Value"] = numSwitches;
    sendSwitchResponse(root, message);
}

//GET ​/switch​/{device_number}​/canwrite
//Indicates whether the specified switch device can be written to
/**
 * @brief Serve the write capability of one channel.
 * @details Resolve its Id and combine binding capability with the user write policy.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDriver0CanWrite(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "CanWrite") || !readSwitchId(root, message, id))
        return;
    root["Value"] = switchEntry[id]->binding.source.kind != Kind::None &&
                    switchEntry[id]->writeable &&
                    SourceMapping::isOutput(switchEntry[id]->binding.mode);
    sendSwitchResponse(root, message);
}

//GET ​/switch​/{device_number}​/getswitch
//PUT ​/switch​/{device_number}​/setswitch
//Get/Set the state of switch device id as a boolean - treats as binary
/**
 * @brief Read or set a channel as a Boolean.
 * @details Validate the request/Id and dispatch mapped reads or Boolean-to-range output writes.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDriver0SwitchState(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "SwitchState") || !readSwitchId(root, message, id) ||
        !requireConnectedSwitch(root, message))
        return;
    SwitchEntry &entry = *switchEntry[id];
    if (server.method() == HTTP_GET)
    {
        double value = 0;
        if (mappedResponseResult(readMappedValue(id, value), root))
            root["Value"] = value != entry.min;
    }
    else
    {
        if (!requireWritableSwitch(id, root, message))
            return;
        bool state = false;
        if (!readSwitchBool(root, message, state))
            return;
        const double value = state ? entry.max : entry.min;
        if (entry.binding.mode == Mode::PwmOutput)
            setPwmOutput(id, value, root);
        else
            setRelayOutput(id, value, root);
    }
    sendSwitchResponse(root, message);
}

//GET ​/switch​/{device_number}​/getswitchdescription
//Gets the description of the specified switch device
/**
 * @brief Serve a channel description.
 * @details Validate request and Id, then send the configured description string.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDriver0SwitchDescription(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "SwitchDescription") || !readSwitchId(root, message, id))
        return;
    root["Value"] = switchEntry[id]->description;
    sendSwitchResponse(root, message);
}

//GET ​/switch​/{device_number}​/getswitchname
//PUT ​/switch​/{device_number}​/setswitchname
//Get/set the name of the specified switch device
/**
 * @brief Read or update a channel name.
 * @details Validate the request and bounded text, persist accepted changes and send the API result.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDriver0SwitchName(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "SwitchName") || !readSwitchId(root, message, id))
        return;

    if (server.method() == HTTP_GET)
        root["Value"] = switchEntry[id]->switchName;
    else
    {
        String name;
        if (findAlpacaArgument(server, "Name", name) != ALPACA_ARG_PRESENT)
        {
            switchRequestError(root, message, "Name must be supplied exactly once");
            return;
        }
        if (name.length() >= MAX_NAME_LENGTH)
        {
            switchRequestError(root, message, "Switch name too long");
            return;
        }
        if (switchEntry[id]->switchName == nullptr)
        {
            switchEntry[id]->switchName = (char *)calloc(MAX_NAME_LENGTH, sizeof(char));
            if (switchEntry[id]->switchName == nullptr)
            {
                root["ErrorNumber"] = invalidOperation;
                root["ErrorMessage"] = "Unable to allocate switch name";
                sendSwitchResponse(root, message);
                return;
            }
        }
        strcpy(switchEntry[id]->switchName, name.c_str());
        saveToEeprom();
        if (!sourceStorageValid)
        {
            root["ErrorNumber"] = 0x500;
            root["ErrorMessage"] = sourceStorageMessage;
        }
    }
    sendSwitchResponse(root, message);
}

//Non-ascom function
//GET ​/switch​/{device_number}​/getswitchtype
//PUT ​/switch​/{device_number}​/setswitchtype
//Get/set the name of the specified switch device
/**
 * @brief Serve the additional channel-type endpoint.
 * @details Read the current type or validate, quiesce and persist an allowed type change.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDriver0SwitchType(void)
{
    if (!validateSettingHandlerRequest("SwitchType", false))
        return;
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "SwitchType") || !readSwitchId(root, message, id))
        return;
    if (server.method() == HTTP_GET)
        root["Value"] = (int)switchEntry[id]->type;
    else
    {
        String text;
        uint32_t type;
        if (findAlpacaArgument(server, "Name", text) != ALPACA_ARG_PRESENT ||
            !parseAlpacaUInt32(text.c_str(), type) || type > SWITCH_ANALG_DAC)
        {
            switchRequestError(root, message, "Name must specify a supported legacy switch type");
            return;
        }
        else
            mappedResponseResult(changeMappedLegacyType(id, (SwitchType)type), root);
    }
    sendSwitchResponse(root, message);
}

//GET ​/switch​/{device_number}​/getswitchvalue
//PUT ​/switch​/{device_number}​/setswitchvalue
//Get/Set the value of the specified switch device as a double - ie not a binary setting
/**
 * @brief Read or set a numeric channel value.
 * @details Validate ownership, Id, write policy and number before dispatching the mapped operation.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDriver0SwitchValue(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "SwitchValue") || !readSwitchId(root, message, id) ||
        !requireConnectedSwitch(root, message))
        return;
    SwitchEntry &entry = *switchEntry[id];
    if (server.method() == HTTP_GET)
    {
        double value = 0;
        if (mappedResponseResult(readMappedValue(id, value), root))
            root["Value"] = value;
    }
    else
    {
        if (!requireWritableSwitch(id, root, message))
            return;
        double value = 0;
        if (!readSwitchValue(root, message, value))
            return;
        if (entry.binding.mode == Mode::PwmOutput)
            setPwmOutput(id, value, root);
        else
            setRelayOutput(id, value, root);
    }
    sendSwitchResponse(root, message);
}

//GET ​/switch​/{device_number}​/minswitchvalue
//Gets the minimum value of the specified switch device as a double
/**
 * @brief Serve the minimum channel value.
 * @details Validate the request/Id and return the configured minimum.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDriver0MinSwitchValue(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "MinSwitchValue") || !readSwitchId(root, message, id))
        return;
    root["Value"] = (double)switchEntry[id]->min;
    sendSwitchResponse(root, message);
}

//GET ​/switch​/{device_number}​/maxswitchvalue
//Gets the maximum value of the specified switch device as a double
/**
 * @brief Serve the maximum channel value.
 * @details Validate the request/Id and return the configured maximum.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDriver0MaxSwitchValue(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "MaxSwitchValue") || !readSwitchId(root, message, id))
        return;
    root["Value"] = (double)switchEntry[id]->max;
    sendSwitchResponse(root, message);
}

//GET ​/switch​/{device_number}​/switchstep
//Returns the step size that this device supports (the difference between successive values of the device).
/**
 * @brief Serve the channel value increment.
 * @details Validate the request/Id and return the configured step.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDriver0SwitchStep(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "SwitchStep") || !readSwitchId(root, message, id))
        return;
    root["Value"] = (double)switchEntry[id]->step;
    sendSwitchResponse(root, message);
}

////////////////////////////////////////////////////////////////////////////////////
//Additional non-ASCOM custom setup calls

/**
 * @brief Respond to an unregistered HTTP route.
 * @details Send the application not-found response without treating it as a device operation.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerNotFound()
{
    server.send(400, "text/plain", "Unknown path or unsupported HTTP method: " + server.uri());
}

/**
 * @brief Restart the controller from its setup action.
 * @details Validate the route policy, send a redirect response and request a hardware restart.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerRestart()
{
    if (!validateSettingHandlerRequest("Restart"))
        return;
    String message;
    int responseCode = 200;
    server.sendHeader(WiFi.hostname().c_str(), String("/status"), true);
    server.send(302, F("text/html"), "<!Doctype html><html>Redirecting for restart</html>");
    DEBUGSL1(F("Reboot requested"));
    device.restart();
}

/**
 * @brief Respond to an unsupported operation.
 * @details Construct the application error response using the current request context.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerNotImplemented()
{
    String message;
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();

    ASCOM_JSON_OBJECT(root, 250);
    jsonResponseBuilder(
        root, clientID, transID, serverTransID++, "HandlerNotFound", notImplemented,
        "No REST handler implemented for argument - check ASCOM Dome v2 specification");
    root["Value"] = 0;
    sendSwitchResponse(root, message);
}

//GET ​/switch​/{device_number}​/status
//Get a descriptor of all the switches managed by this driver for discovery purposes
/**
 * @brief Report controller, channel and hardware status.
 * @details Build JSON from connection state, mapped sources, cached-value quality and inventory diagnostics.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerStatus(void)
{
    recordAlpacaReadActivity();
    String message, timeString;
    uint32_t clientID = 0; //(uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = 0;  //(uint32_t)server.arg("ClientTransactionID").toInt();
    int i = 0;
    int returnCode = 200;

    ASCOM_JSON_OBJECT(root, 1024);
    ASCOM_JSON_NESTED_ARRAY(entries, root, "switches");
    jsonResponseBuilder(root, clientID, transID, serverTransID++, "Status", 0, "");

    root["time"] = getTimeAsString(timeString);
    root["host"] = myHostname;
    root["build version"] = String(__DATE__) + FPSTR(BuildVersionName);
    checkAlpacaConnectionTimeout();
    root["connected"] = alpacaConnection.connected();
    root["connectedId"] = alpacaConnection.ownerClientID;
    root["connectionTimeoutMs"] = connectedTimeoutMs;
    root["connectionTimerArmed"] = alpacaConnection.timerArmed;
    root["allowAnyConnectedClient"] = allowAnyConnectedClient;

    for (i = 0; i < numSwitches; i++)
    {
        //Can I re-use a single object or do I need to create a new one each time? A: new one each time.
        ASCOM_JSON_OBJECT(entry, 256);

        entry["description"] = switchEntry[i]->description;
        entry["name"] = switchEntry[i]->switchName;
        entry["type"] = (int)switchEntry[i]->type;
        entry["pin"] = (int)switchEntry[i]->pin;
        entry["writeable"] = switchEntry[i]->writeable;
        entry["min"] = switchEntry[i]->min;
        entry["max"] = switchEntry[i]->max;
        entry["step"] = switchEntry[i]->step;
        entry["source"] = sourceKey(switchEntry[i]->binding.source);
        double value = 0;
        const Result result = readMappedValue(i, value);
        entry["valid"] = result == Result::Ok;
        if (result == Result::Ok)
            entry["value"] = value;
        else
            entry["error"] = sourceResultMessage(result);
        entries.add(entry);
    }

    ASCOM_JSON_NESTED_ARRAY(devices, root, "sourceDevices");
    for (size_t slot = 0; slot < sourceDeviceCount; ++slot)
    {
        ASCOM_JSON_OBJECT(item, 256);
        const auto &device = sourceDevices[slot];
        item["address"] = device.address;
        item["state"] = sourceStateName(device.state);
        item["error"] = device.lastError;
        item["generation"] = device.generation;
        item["lastSuccessMs"] = device.lastSuccessMs;
        devices.add(item);
    }
    root["inventoryRevision"] = sourceInventoryRevision;
    root["storage"] = sourceStorageMessage;
    Serial.println(message);

    ASCOM_JSON_SERIALIZE(root, message);
    server.send(returnCode, F("application/json"), message);
    return;
}

//Setup pages do not participate in the Alpaca ClientID lease.
/**
 * @brief Stream a prepared setup-page fragment.
 * @details Send HTML unchanged without inserting REST ClientID controls.
 * @param[in,out] message (String&) Response text buffer or assertion explanation.
 * @return void; effects are described above.
 */
void sendSetupContent(String &message)
{
    server.sendContent(message);
}

//Helper function
/**
 * @brief Send the device setup page.
 * @details Stream the header, settings, inventory and footer with the requested HTTP status and error.
 * @param[in] returnCode (int) HTTP response status.
 * @param[in,out] message (String&) Response text buffer or assertion explanation.
 * @param[in,out] err (String&) Setup error text; empty for success.
 * @return void; effects are described above.
 */
void sendDeviceSetup(int returnCode, String &message, String &err)
{
    //Send large pages in chunks
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    setupFormBuilderHeader(message);
    server.send(returnCode, F("text/html"), message);
    message = "";

    setupFormBuilderDeviceHeader(message, err);
    sendSetupContent(message);
    message = "";

    setupFormBuilderDeviceStrings(message);
    mappedInventoryForm(message);
    sendSetupContent(message);
    message = "";

    setupFormBuilderFooter(message);
    sendSetupContent(message);
}

/*
 * Handlers to do custom setup that can't be done without a windows ascom driver setup form. 
 * Returns device page 
 */
/**
 * @brief Open the device setup UI.
 * @details Record eligible read activity and render the current device settings.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDeviceSetup()
{
    recordAlpacaReadActivity();
    String timeString, message, err;
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;

    int returnCode = 200;
    if (server.method() == HTTP_GET)
    {
        sendDeviceSetup(returnCode, message, err);
    }
}

/*
  * Handler to update the hostname from the form.
  */
/**
 * @brief Update the configured device hostname.
 * @details Validate a bounded hostname, save it and return the setup page with operation status.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDeviceHostname(void)
{
    if (!validateSettingHandlerRequest("SetHostname"))
        return;
    String message, timeString, err = "";
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;

    int returnCode = 400;
    String argToSearchFor[] = {"hostname"};

    if (server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET)
    {
        if (hasArgIC(argToSearchFor[0], server, false))
        {
            String newHostname = server.arg(argToSearchFor[0]);
            //process form variables.
            if (newHostname.length() > 0 && newHostname.length() < MAX_NAME_LENGTH - 1)
            {
                //process new hostname
                strncpy(myHostname, newHostname.c_str(), MAX_NAME_LENGTH);
                saveToEeprom();
                returnCode = 200;
            }
            else
            {
                err = "New name too long";
            }
        }
    }
    else
    {
        err = "Bad HTTP request verb";
    }

    sendDeviceSetup(returnCode, message, err);

    if (returnCode == 200)
    {
        //Now reset to change the hostname and re-register DHCP
        device.reset();
    }
    return;
}

/*
  * Handler to update the device Location from the form.
  */
/**
 * @brief Update the management location string.
 * @details Validate bounded form text, save it and return the setup page.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDeviceLocation(void)
{
    if (!validateSettingHandlerRequest("SetLocation"))
        return;
    String message, timeString, err = "";
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;

    int returnCode = 400;
    String argToSearchFor[] = {
        "location",
    };

    if (server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET)
    {
        if (hasArgIC(argToSearchFor[0], server, false))
        {
            String newLocation = server.arg(argToSearchFor[0]);
            //process form variables.
            if (newLocation.length() > 0 && newLocation.length() < MAX_NAME_LENGTH - 1)
            {
                //process new hostname
                strncpy(Location, newLocation.c_str(), MAX_NAME_LENGTH);
                saveToEeprom();
                returnCode = 200;
            }
            else
            {
                err = "Location details too long";
            }
        }
    }
    else
    {
        err = "Bad HTTP request verb";
    }

    sendDeviceSetup(returnCode, message, err);
    return;
}

/**
 * @brief Update the Alpaca discovery port setting.
 * @details Validate the submitted port, save the setting and return the device setup page.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDeviceUdpPort(void)
{
    if (!validateSettingHandlerRequest("SetUdpPort"))
        return;
    String message, timeString, err = "";
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;

    int returnCode = 400;
    String argToSearchFor[] = {"discoveryport"};

    if (server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET)
    {
        if (hasArgIC(argToSearchFor[0], server, false))
        {
            int newPort = server.arg(argToSearchFor[0]).toInt();
            //process form variables.
            if (newPort > 1024 && newPort < 65536)
            {
                //process new hostname
                udpPort = newPort;
                saveToEeprom();
                returnCode = 200;
            }
            else
            {
                err = "New Discovery port value out of range ";
                returnCode = 401;
            }
        }
    }
    else
    {
        returnCode = 400;
        err = "Bad HTTP request verb";
    }

    //Send large pages in chunks
    sendDeviceSetup(returnCode, message, err);
}

//helper function
/**
 * @brief Send the switch-driver setup page.
 * @details Stream driver settings, individual mapped channel forms and footer with status/error information.
 * @param[in] returnCode (int) HTTP response status.
 * @param[in,out] message (String&) Response text buffer or assertion explanation.
 * @param[in,out] err (String&) Setup error text; empty for success.
 * @return void; effects are described above.
 */
void sendDriver0Setup(int returnCode, String &message, String &err)
{
    //Send large pages in chunks
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    setupFormBuilderHeader(message);
    server.send(returnCode, "text/html", message);
    message = "";

    setupFormBuilderDriverHeader(message, err);
    sendSetupContent(message);
    message = "";

    //List the switches
    for (int i = 0; i < numSwitches; i++)
    {
        setupFormBuilderDriver0Switches(message, i);
        sendSetupContent(message);
        message = "";
    }

    setupFormBuilderFooter(message);
    sendSetupContent(message);
    message = "";
}

/*
 * Handlers to do custom setup that can't be done without a windows ascom driver setup form. 
 * Returns driver page for a specific Alpaca driver type, 
 * TODO might want to think about how to handle multiple drivers 
 */
/**
 * @brief Open the switch-driver setup UI.
 * @details Render current channel count and mapping forms from the stored configuration.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDriver0Setup(void)
{
    recordAlpacaReadActivity();
    String message, timeString, err = "";
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;

    int returnCode = 200;
    if (server.method() == HTTP_GET)
    {
        sendDriver0Setup(returnCode, message, err);
    }
}

/*
  * Handle update of the number of attached switches
  */
/**
 * @brief Change the configurable external-channel count.
 * @details Validate the count, preserve residents, safely resize the table and persist accepted configuration.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDriver0SetupNumSwitches(void)
{
    if (!validateSettingHandlerRequest("SetNumSwitches"))
        return;
    String message, error, text;
    uint32_t count = 0;
    if (findAlpacaArgument(server, "numSwitches", text) != ALPACA_ARG_PRESENT ||
        !parseAlpacaUInt32(text.c_str(), count) || count > (uint32_t)maximumConfigurableSources())
        error = "Invalid external channel count; resident native/ADC channels cannot be removed";
    else
    {
        pendingAdcId = -1;
        if (!allocateMappedSwitches(sourceResidentCount + count))
            error = "Allocation or hardware quiesce failed; configuration retained";
        else
        {
            ++sourceInventoryRevision;
            saveToEeprom();
            if (!sourceStorageValid)
                error = sourceStorageMessage;
        }
    }
    sendDriver0Setup(error.length() ? 400 : 200, message, error);
}

/*
  * lengthy function to take the setup form output for each switch to manage its control parameters.
  * We don't attempt to set its value here. We just set the type, limits, features etc. The main API controls its use.
  */
/**
 * @brief Process one channel setup submission.
 * @details Apply route-specific ownership policy, then delegate mapping validation and reconfiguration.
 * @par Arguments
 * None.
 * @return void; sends an HTTP response and applies the described side effects.
 */
void handlerDriver0SetupSwitches(void)
{
    if (!validateSettingHandlerRequest("ConfigureSwitch"))
        return;
    handlerMappedSetupSwitches();
}
/*
 Handler for setup dialog - issue call to <hostname>/setup and receive a webpage
 Fill in the form and submit and handler for each form button will store the variables and return the same page.
 optimise to something like this:
 https://circuits4you.com/2018/02/04/esp8266-ajax-update-part-of-web-page-without-refreshing/
 Bear in mind HTML standard doesn't support use of PUT in forms and changes it to GET so arguments get sent in plain sight as 
 part of the URL.
 */
/*   https://github.com/esp8266/Arduino/issues/3205
  *   server.setContentLength(CONTENT_LENGTH_UNKNOWN);
      server.send ( 200, "text/html", first_part.c_str());
      while (...) 
      {
          resp = "...";
          resp += "...";
          server.sendContent(resp);
      }
  */

/**
 * @brief Build shared setup-page head content.
 * @details Append viewport metadata, local responsive CSS and mapping behavior script.
 * @param[in,out] htmlForm (String&) HTML fragment buffer to append to or replace.
 * @return (String&) Generated text or the updated String reference.
 */
String &setupFormBuilderHeader(String &htmlForm)
{
    htmlForm = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">";
    htmlForm += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    htmlForm += FPSTR(setupPageStyle);
    htmlForm += FPSTR(mappingPageScript);
    htmlForm += "</head>";
    return htmlForm;
}

/**
 * @brief Build the device setup heading.
 * @details Insert current device identity and an escaped operation error when present.
 * @param[in,out] htmlForm (String&) HTML fragment buffer to append to or replace.
 * @param[in,out] errMsg (String&) Setup error text; empty for success.
 * @return (String&) Generated text or the updated String reference.
 */
String &setupFormBuilderDeviceHeader(String &htmlForm, String &errMsg)
{
    String hostname = WiFi.hostname();

    htmlForm = F("<body><div class=\"container\">");
    htmlForm += F("<div class=\"row\" id=\"topbar\" bgcolor='A02222'>");
    htmlForm += F(
        "<p> This is the setup page for the <a href=\"http://www.skybadger.net\">Skybadger</a> <a href=\"https://www.ascom-standards.org\">ASCOM</a> device '");
    htmlForm += myHostname;
    htmlForm += F(
        "' device which implements the <a href=\"https://www.ascom-standards.org/api\">ALPACA</a> v1.0 API</b> for the devices listed below:");
    htmlForm += F("<ul><li> <a href=\"/setup/v1/switch/0/setup\"> ALPACA Switch 0</a> </li>");
    htmlForm += F("<li> <end of drivers implemented>. </li>");
    htmlForm += F("</ul></div>");

    if (errMsg != NULL && errMsg.length() > 0)
    {
        htmlForm += F("<div class=\"row\" id=\"errorbar\" bgcolor='lightred'>");
        htmlForm += F("<b>Error Message</b>");
        htmlForm += escapedMappingHtml(errMsg);
        htmlForm += F("</div><hr>");
    }

    return htmlForm;
}

/**
 * @brief Build device configuration forms.
 * @details Render hostname, location, discovery-port and MQTT settings from live variables.
 * @param[in,out] htmlForm (String&) HTML fragment buffer to append to or replace.
 * @return (String&) Generated text or the updated String reference.
 */
String &setupFormBuilderDeviceStrings(String &htmlForm)
{
    String hostname = WiFi.hostname();

    //UDP Discovery port
    htmlForm = F("<div class=\"row float-left\" id=\"topbar-device\" bgcolor='A02222'> ");
    htmlForm += F("<div class=\"col-sm-12\">");
    htmlForm += F(
        "<p> This device supports the <a href=\"placeholder\">ALPACA UDP discovery API</a> on port: ");
    htmlForm += udpPort;
    htmlForm += F("</p></div></div>");

    //Device settings hostname
    htmlForm += F("<div class=\"row float-left\">");
    htmlForm += F("<div class=\"col-sm-12\"><h2> Enter new hostname for device</h2><br/>");
    htmlForm += F(
        "<p>Changing the hostname will cause the device to reboot and may change the IP address!</p>");
    htmlForm += F("<form method=\"POST\" id=\"hostname\" action=\"http://");
    htmlForm.concat(myHostname);
    htmlForm += F("/setup/hostname\">");
    htmlForm += F("<input type=\"text\" name=\"hostname\" maxlength=\"");
    htmlForm += String(MAX_NAME_LENGTH).c_str();
    htmlForm += F("\" value=\"");
    htmlForm.concat(myHostname);
    htmlForm += F("\"/>");
    htmlForm += F("<label for=\"hostname\" > Hostname </label>");
    htmlForm += F("<input type=\"submit\" value=\"Set hostname\" />");
    htmlForm += F("</form></div></div>");

    //Device settings location
    htmlForm += "<div class=\"row float-left\">";
    htmlForm +=
        "<div class=\"col-sm-12\"><h2> Enter location to be reported by Management API for device</h2><br/>";
    htmlForm += "<form method=\"POST\" id=\"location\" action=\"http://";
    htmlForm.concat(myHostname);
    htmlForm += "/setup/location\">";
    htmlForm += "<input type=\"text\" name=\"location\" maxlength=\"";
    htmlForm += String(MAX_NAME_LENGTH).c_str();
    htmlForm += "\" value=\"";
    htmlForm.concat(Location);
    htmlForm += "\"/>";
    htmlForm += "<label for=\"location\" > Location </label>";
    htmlForm += "<input type=\"submit\" value=\"Set location\" />";
    htmlForm += "</form></div></div>";

    //UDP Port
    htmlForm += "<div class=\"row float-left\" id=\"discovery-port\" >";
    htmlForm += "<div class=\"col-sm-12\"><h2> Enter new Discovery port number for device</h2>";
    htmlForm += "<form method=\"POST\" action=\"http://";
    htmlForm.concat(myHostname);
    htmlForm += "/setup/udpport\">";
    htmlForm +=
        "<label for=\"udpport\" id=\"udpport\"> Port number to use for Management API discovery </label>";
    htmlForm += "<input type=\"number\" name=\"udpport\" min=\"1024\" max=\"65535\" ";
    htmlForm += "value=\"";
    htmlForm.concat(udpPort);
    htmlForm += "\"/>";
    htmlForm += "<input type=\"submit\" value=\"Set port\" />";
    htmlForm += "</form></div></div>";

    mqttSetupForm(htmlForm);
    return htmlForm;
}

/**
 * @brief Build the driver setup heading and count form.
 * @details Render current configuration limits and the configurable external-channel count.
 * @param[in,out] htmlForm (String&) HTML fragment buffer to append to or replace.
 * @param[in,out] errMsg (String&) Setup error text; empty for success.
 * @return (String&) Generated text or the updated String reference.
 */
String &setupFormBuilderDriverHeader(String &htmlForm, String &errMsg)
{
    String hostname = WiFi.hostname();

    debugI("%s", "called driver0 header ");
    htmlForm = F("<body> <div class=\"container-fluid\">");
    htmlForm += F("<div class=\"row float-left\" id=\"topbar\" bgcolor=\"A02222\">");
    htmlForm += F("<div class=\"col-sm-12\">");
    htmlForm += F(
        "<p> This is the driver setup page for the <a href=\"http://www.skybadger.net\">Skybadger</a> <a href=\"https://www.ascom-standards.org\">ASCOM</a> Switch device ");
    htmlForm += myHostname;
    htmlForm += F(
        " which uses the <a href=\"https://www.ascom-standards.org/api\">ALPACA</a> v1.0 API</b> for Switches");
    htmlForm +=
        F("<p> Click <a href=\"/setup\"> Device Management <a/> to manage the device.  </p>");
    htmlForm += F("</div></div>");

    if (errMsg != NULL && errMsg.length() > 0)
    {
        htmlForm +=
            F("<div class=\"row\" id=\"errorbar\" bgcolor='lightred'><div class=col-sm-12>");
        htmlForm += F("<div class=\"col-sm-12\"><b>");
        htmlForm += escapedMappingHtml(errMsg);
        htmlForm += F("</b></div></div><hr>");
    }

    //Setup the number of switches - should be in a separate function but lumped in here.. Separate out when have multiple drivers for this device
    htmlForm += "<div class=\"row float-left\"> ";
    htmlForm += "<div class=\"col-sm-12\"><h2>Configure channels</h2><br/>";
    htmlForm +=
        "<p>Native connectors are first, then resident ADC inputs, then configurable external channels. The count below changes only the final group.</p>";
    htmlForm +=
        "<p>Editing this to add switch components ('upscaling') will copy the existing setup to the new setup but you will need to edit the added switches. </p>";
    htmlForm +=
        "<p>Editing this to reduce the number of switch components ('downscaling') will delete the configuration for the switches dropped but retain those lower number switch configurations for further editing</p><br>";
    htmlForm += "</div></div>";

    htmlForm += "<div class=\"row\" id=\"numSwitches\">";
    htmlForm += "<div class=\"col-sm-12\">";
    htmlForm += "<form action=\"/setup/numswitches\" method=\"POST\" id=\"switchcount\" >";
    htmlForm += "<label for=\"numSwitches\" >Number of configurable external channels</label>";
    htmlForm += "<input type=\"number\" name=\"numSwitches\" min=\"0\" max=\"";
    htmlForm += maximumConfigurableSources();
    htmlForm += "\" value=\"";
    htmlForm += configuredSourceCount();
    htmlForm += "\">";
    htmlForm += "<input type=\"submit\" value=\"Update\"> </form> </div></div>";

    htmlForm += "<div class=\"row float-left\">";
    htmlForm += "<div class=\"col-sm-12\" id=\"switchConfig\"> <h2>Switch configuration </h2>";
    htmlForm +=
        "<br><p>To configure the switch types and limits, select the switch you need below.</p></div></div>";
    return htmlForm;
}

/**
 * @brief Build one mapped channel setup form.
 * @details Delegate rendering to the source-aware form builder for the requested logical Id.
 * @param[in,out] htmlForm (String&) HTML fragment buffer to append to or replace.
 * @param[in] index (int) Zero-based entry index.
 * @return (String&) Generated text or the updated String reference.
 */
String &setupFormBuilderDriver0Switches(String &htmlForm, int index)
{
    return mappedSwitchForm(htmlForm, index);
}

//footer
/**
 * @brief Build setup-page footer actions.
 * @details Append restart/navigation controls and closing page markup.
 * @param[in,out] htmlForm (String&) HTML fragment buffer to append to or replace.
 * @return (String&) Generated text or the updated String reference.
 */
String &setupFormBuilderFooter(String &htmlForm)
{
    //restart button
    htmlForm = F("<div class=\"row float-left\" id=\"restartField\" >");
    htmlForm += F("<div class=\"col-sm-12\"><h2> Restart device</h2>");
    htmlForm += F("<form class=\"form-inline\" method=\"POST\" action=\"http://");
    htmlForm.concat(myHostname);
    htmlForm += F("/restart\">");
    htmlForm += F("<input type=\"submit\" class=\"btn btn-default\" value=\"Restart device\" />");
    htmlForm += F("</form>");
    htmlForm += F("</div></div>");

    //Update button
    htmlForm += F("<div class=\"row float-left\" id=\"updateField\" >");
    htmlForm += F("<div class=\"col-sm-12\"><h2> Update firmware</h2>");
    htmlForm += F("<form class=\"form-inline\" method=\"GET\"  action=\"http://");
    htmlForm.concat(myHostname);
    htmlForm += F("/update\">");
    htmlForm += F("<input type=\"submit\" class=\"btn btn-default\" value=\"Update firmware\" />");
    htmlForm += F("</form>");
    htmlForm += F("</div></div>");

    //Close page
    htmlForm += F("<div class=\"row float-left\" id=\"positions\">");
    htmlForm += F("<div class=\"col-sm-12\"><br> </div></div>");
    htmlForm += F("</div></body></html>");
    return htmlForm;
}
#include "MappingUi.h"
#include "MqttSetup.h"
#endif
