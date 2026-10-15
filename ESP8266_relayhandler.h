/*
ESP8266_relayhandler.h
This is a firmware application to implement the ASCOM ALPACA switch interface API.
Each device can manage more than one switch - up to numswitches which is user configured.
Each nominal switch can be 1 of 4 types - binary relays (no and nc) and digital pwm and DAC outputs.
Hence the setup allows specifying the number of switches per device and the host/device name.
This particular switch device assumes the use of port pins allocated from the device via a pcf8574 I2C port expansion device. 
Using an ESP8266-01 this leaves one pin free to be a digital device. 
Using an ESP8266-12 this leaves a number of pins free to be a digital device.
Use of a DAC requires (presumably) an i2C DAC or an onboard DAC tied to a pin.

In use of the ASCOM Api 
All URLs include an argument 'Id' which contains the number of the switch attached to this device instance
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
#include <errno.h>
#include <stdlib.h>

void sendSwitchResponse(JsonObject& root, String& message)
{
    message = "";
    ASCOM_JSON_SERIALIZE(root, message);
    sendAlpacaJson(server, message);
}

bool switchRequestError(JsonObject& root, String& message, const char* error)
{
    root["ErrorNumber"] = invalidValue;
    root["ErrorMessage"] = error;
    sendSwitchResponse(root, message);
    return false;
}

void checkAlpacaConnectionTimeout()
{
    alpacaConnection.expire(millis(), connectedTimeoutMs);
    connectionCtr = alpacaConnection.connected() ? 1 : 0;
}

int validateAlpacaRequestOwnership(uint32_t clientID, bool setting)
{
    const auto result = alpacaConnection.request(clientID, setting, millis(),
                                                connectedTimeoutMs, allowAnyConnectedClient);
    connectionCtr = alpacaConnection.connected() ? 1 : 0;
    if (result == OWNERSHIP_INVALID_CLIENT) return invalidValue;
    if (result == OWNERSHIP_OTHER_CLIENT) return notConnected;
    return Success;
}

const char* ownershipErrorMessage(int error)
{
    return error == invalidValue ? "ClientID must be supplied and greater than zero" :
                                   "Device is owned by another ClientID";
}

bool beginSwitchRequest(JsonObject& root, String& message, const char* method, bool forceSetting = false)
{
    uint32_t clientID = 0;
    uint32_t transID = 0;
    const bool clientValid = parseAlpacaOptionalUInt32<ESP8266WebServer, String>(server, "ClientID", clientID);
    const bool transValid = parseAlpacaOptionalUInt32<ESP8266WebServer, String>(server, "ClientTransactionID", transID);
    jsonResponseBuilder(root, clientID, transID, serverTransID++, method, Success, "");
    if (!clientValid || !transValid)
        return switchRequestError(root, message, "Invalid or duplicate ClientID/ClientTransactionID");
    //Connected PUT manages acquire/release explicitly after parsing its boolean.
    const bool connectionCommand = strcmp(method, "Connected") == 0 && server.method() == HTTP_PUT;
    const bool setting = !connectionCommand &&
        (forceSetting || server.method() == HTTP_PUT || server.method() == HTTP_POST);
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

bool validateSettingHandlerRequest(const char* method, bool forceSetting = true)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    return beginSwitchRequest(root, message, method, forceSetting);
}

void recordAlpacaReadActivity()
{
    uint32_t clientID = 0;
    if (parseAlpacaOptionalUInt32<ESP8266WebServer, String>(server, "ClientID", clientID) && clientID > 0)
        validateAlpacaRequestOwnership(clientID, false);
}

void handleConnected(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    if (!beginSwitchRequest(root, message, "Connected")) return;
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
        const auto result = requested ?
            alpacaConnection.request(clientID, true, millis(), connectedTimeoutMs, allowAnyConnectedClient) :
            alpacaConnection.disconnect(clientID, millis(), connectedTimeoutMs, allowAnyConnectedClient);
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

bool requireConnectedSwitch(JsonObject& root, String& message)
{
    if (alpacaConnection.connected()) return true;
    root["ErrorNumber"] = notConnected;
    root["ErrorMessage"] = "Switch device is not connected";
    sendSwitchResponse(root, message);
    return false;
}

bool readSwitchId(JsonObject& root, String& message, int& id)
{
    String text;
    uint32_t parsed = 0;
    if (findAlpacaArgument(server, "Id", text) != ALPACA_ARG_PRESENT ||
        !parseAlpacaUInt32(text.c_str(), parsed) || parsed > 32767 ||
        numSwitches <= 0 || parsed >= (uint32_t) numSwitches)
        return switchRequestError(root, message, "Missing, duplicate, malformed or out-of-range Id");
    id = (int) parsed;
    return true;
}

bool readSwitchBool(JsonObject& root, String& message, bool& state)
{
    String text;
    if (findAlpacaArgument(server, "State", text) != ALPACA_ARG_PRESENT ||
        !parseAlpacaBool(text.c_str(), state))
        return switchRequestError(root, message, "State must be a single true or false argument");
    return true;
}

bool readSwitchValue(JsonObject& root, String& message, double& value)
{
    String text;
    if (findAlpacaArgument(server, "Value", text) != ALPACA_ARG_PRESENT ||
        !parseAlpacaDouble(text.c_str(), value))
        return switchRequestError(root, message, "Value must be a single finite decimal number");
    return true;
}

bool requireWritableSwitch(int id, JsonObject& root, String& message)
{
    if (switchEntry[id]->writeable) return true;
    root["ErrorNumber"] = notImplemented;
    root["ErrorMessage"] = "This switch is read-only";
    sendSwitchResponse(root, message);
    return false;
}

bool setRelayOutput(unsigned int switchID, double value, JsonObject& root)
{
    RelayWriteResult result = writeRelayOutput(switchDevice, switchID, value,
                                               reverseRelayLogic, switchEntry[switchID]->value);
    switch (result)
    {
      case RELAY_WRITE_OK:
        return true;
      case RELAY_INVALID_VALUE:
        root["ErrorNumber"] = invalidValue;
        root["ErrorMessage"] = "Relay value must be 0 or 1";
        break;
      case RELAY_INVALID_PIN:
        root["ErrorNumber"] = invalidValue;
        root["ErrorMessage"] = "Relay Id exceeds the eight PCF8574 outputs";
        break;
      case RELAY_WRITE_FAILED:
        root["ErrorNumber"] = invalidOperation;
        root["ErrorMessage"] = "PCF8574 relay write failed";
        break;
    }
    return false;
}

struct ArduinoPwmOutput {
    void setRange(unsigned int range) { analogWriteRange(range); }
    void write(int pin, int value) { analogWrite(pin, value); }
};

bool setPwmOutput(unsigned int switchID, double value, JsonObject& root)
{
    ArduinoPwmOutput output;
    SwitchEntry& entry = *switchEntry[switchID];
    const PwmWriteResult result = writePwmOutput(output, entry.pin, MINPIN, MAXPIN,
                                                value, entry.min, entry.max, entry.value);
    if (result == PWM_WRITE_OK)
        return true;

    root["ErrorNumber"] = result == PWM_INVALID_CONFIG ? invalidOperation : invalidValue;
    switch (result)
    {
      case PWM_INVALID_CONFIG:
        root["ErrorMessage"] = "PWM range must have integer limits within 0..1024 and min < max";
        break;
      case PWM_INVALID_PIN:
        root["ErrorMessage"] = "PWM output pin is outside the board range";
        break;
      default:
        root["ErrorMessage"] = "PWM value must be an integer within the configured range";
        break;
    }
    return false;
}

//Function definitions
void copySwitch( SwitchEntry* sourceSe, SwitchEntry* targetSe );
void initSwitch( SwitchEntry* targetSe );
SwitchEntry** reSize( SwitchEntry** old, int newSize );
bool getUriField( char* inString, int searchIndex, String& outRef );

//Deprecated in favour of splitting up in to separate chunks. 
//String& setupFormBuilder( String& htmlForm, String& errMsg );

String& setupFormBuilderHeader( String& htmlForm );
String& setupFormBuilderDeviceHeader( String& htmlForm, String& errMsg ); //For device setup
String& setupFormBuilderDeviceStrings( String& htmlForm );
String& setupFormBuilderDriverHeader( String& htmlForm, String& errMsg ); //For per-driver setup when we have 1 or more drivers attached
String& setupFormBuilderDriver0Switches( String& htmlForm, int index );
String& setupFormBuilderFooter( String& htmlForm );

//Device settings
//Include a positive ClientID in every configuration form. With no query ID the
//field is blank, so the browser asks the operator to select a distinct ID.
void sendSetupContent(String& message)
{
    uint32_t clientID = 0;
    parseAlpacaOptionalUInt32<ESP8266WebServer, String>(server, "ClientID", clientID);
    String field = F("<label>Client ID <input type=\"number\" name=\"ClientID\" min=\"1\" max=\"4294967295\" required value=\"");
    if (clientID > 0) field += String(clientID);
    field += F("\"></label><br>");
    int offset = 0;
    int formStart;
    while ((formStart = message.indexOf("<form", offset)) >= 0)
    {
        const int end = message.indexOf('>', formStart);
        if (end < 0) break;
        message = message.substring(0, end + 1) + field + message.substring(end + 1);
        offset = end + 1 + field.length();
    }
    server.sendContent(message);
}

void sendDeviceSetup( int returnCode, String& message, String& err );
void handlerDeviceSetup(void);
void handlerDeviceHostname(void);
void handlerDeviceLocation(void);
void handlerDeviceUdpPort(void);

//Driver0 settings
void sendDriver0Setup( int returnCode, String& message, String& err );
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
void copySwitch( SwitchEntry* sourceSe, SwitchEntry* targetSe )
{
    strncpy( targetSe->description, sourceSe->description, MAX_NAME_LENGTH);
    strncpy( targetSe->switchName, sourceSe->switchName, MAX_NAME_LENGTH);
    targetSe->writeable   = sourceSe->writeable;
    targetSe->type        = SWITCH_RELAY_NO;
    targetSe->min         = sourceSe->min;
    targetSe->max         = sourceSe->max;
    targetSe->step        = sourceSe->step;
    targetSe->value       = sourceSe->value;
    targetSe->pin         = sourceSe->pin;
}

void initSwitch( SwitchEntry* targetSe )
{
    String output;
    output = "Default description";
    strncpy( targetSe->description, output.c_str(), MAX_NAME_LENGTH);

    output = "Switch Name";
    strncpy( targetSe->switchName, output.c_str(), MAX_NAME_LENGTH);

    targetSe->writeable   = true;
    targetSe->type        = SWITCH_RELAY_NO;
    targetSe->min         = 0.0F;
    targetSe->max         = 0.0F;
    targetSe->step        = 1.0F;
    targetSe->value       = 0.0F;
    targetSe->pin         = NULLPIN;
}
/*
 * This function re-sizes the existing switchEntry array by re-allocating memory and copying if required. 
 * returns pointer to array of switches
 */
SwitchEntry** reSize( SwitchEntry** old, int newSize )
{
  int i=0;
  SwitchEntry* newse;
  SwitchEntry** pse = (SwitchEntry** ) calloc( sizeof (SwitchEntry*),  newSize );
  
  DEBUGS1( "reSize called: " );DEBUGSL1( newSize );

  if ( newSize < numSwitches )
  {
    for ( i=0 ; i < newSize; i++ )
      pse[i] = old[i];      
    //Handle the remaining 
    for ( ; i < numSwitches; i++ )
    {
      if( old[i]->description != nullptr ) 
        free( old[i]->description );
      if( old[i]->switchName != nullptr ) free( old[i]->switchName );
        free( old[i] );
    }
  }
  else if ( newSize == numSwitches)
  {
    //do nothing
    free(pse);
    pse = old;
  }
  else //bigger than before
  {
    for ( i = 0; i < numSwitches; i++ )
       pse[i] = old[i];      
    for ( ; i < newSize; i++ )
    {
      newse = (SwitchEntry*) calloc( sizeof (SwitchEntry), 1 );
      newse->description = (char*)calloc( MAX_NAME_LENGTH, sizeof(char) );
      newse->switchName = (char*)calloc( MAX_NAME_LENGTH, sizeof(char) );
      pse[i] = newse;
      initSwitch( newse );
    }
  }
  numSwitches = newSize;
  DEBUGS1("reSize completed, numSwitches updated to");DEBUGSL1(newSize);
  return pse;
}

bool getUriField( char* inString, int searchIndex, String& outRef )
{
  char *p = inString;
  char *str;
  char delims1[] = {"//"};
  char delims2[] = {"/:"};
  int chunkCtr = 0;
  bool  status = false;    
  int localIndex = 0;
  
  localIndex = String( inString ).indexOf( delims1 );
  if( localIndex >= 0 )
  {
    while ((str = strtok_r(p, delims2, &p)) != NULL) // delimiter is the semicolon
    {
       if ( chunkCtr == searchIndex && !status )
       {
          outRef = String( str );
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
void handlerDriver0Maxswitch(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    if (!beginSwitchRequest(root, message, "MaxSwitch")) return;
    root["Value"] = numSwitches;
    sendSwitchResponse(root, message);
}

//GET ​/switch​/{device_number}​/canwrite
//Indicates whether the specified switch device can be written to
void handlerDriver0CanWrite(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "CanWrite") || !readSwitchId(root, message, id)) return;
    root["Value"] = switchEntry[id]->writeable;
    sendSwitchResponse(root, message);
}

//GET ​/switch​/{device_number}​/getswitch
//PUT ​/switch​/{device_number}​/setswitch
//Get/Set the state of switch device id as a boolean - treats as binary
void handlerDriver0SwitchState(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "SwitchState") || !readSwitchId(root, message, id)) return;

    SwitchEntry& entry = *switchEntry[id];
    if (server.method() == HTTP_GET)
    {
        if (entry.type == SWITCH_RELAY_NO || entry.type == SWITCH_RELAY_NC)
            root["Value"] = entry.value == 1.0F;
        else if (entry.type == SWITCH_PWM)
            root["Value"] = pwmSwitchState(entry.value, entry.min);
        else
        {
            root["ErrorNumber"] = invalidOperation;
            root["ErrorMessage"] = "Boolean state is unavailable for this switch type";
        }
    }
    else
    {
        if (!requireWritableSwitch(id, root, message)) return;
        bool state = false;
        if (!readSwitchBool(root, message, state)) return;
        if (entry.type == SWITCH_RELAY_NO || entry.type == SWITCH_RELAY_NC)
            setRelayOutput(id, state ? 1.0 : 0.0, root);
        else if (entry.type == SWITCH_PWM)
            setPwmOutput(id, pwmBooleanValue(state, entry.min, entry.max), root);
        else
        {
            root["ErrorNumber"] = notImplemented;
            root["ErrorMessage"] = "Output is not implemented for this switch type";
        }
    }
    sendSwitchResponse(root, message);
}

//GET ​/switch​/{device_number}​/getswitchdescription
//Gets the description of the specified switch device
void handlerDriver0SwitchDescription(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "SwitchDescription") || !readSwitchId(root, message, id)) return;
    root["Value"] = switchEntry[id]->description;
    sendSwitchResponse(root, message);
}

//GET ​/switch​/{device_number}​/getswitchname
//PUT ​/switch​/{device_number}​/setswitchname
//Get/set the name of the specified switch device
void handlerDriver0SwitchName(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "SwitchName") || !readSwitchId(root, message, id)) return;

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
            switchEntry[id]->switchName = (char*) calloc(MAX_NAME_LENGTH, sizeof(char));
            if (switchEntry[id]->switchName == nullptr)
            {
                root["ErrorNumber"] = invalidOperation;
                root["ErrorMessage"] = "Unable to allocate switch name";
                sendSwitchResponse(root, message);
                return;
            }
        }
        strcpy(switchEntry[id]->switchName, name.c_str());
    }
    sendSwitchResponse(root, message);
}

//Non-ascom function
//GET ​/switch​/{device_number}​/getswitchtype
//PUT ​/switch​/{device_number}​/setswitchtype
//Get/set the name of the specified switch device
void handlerDriver0SwitchType(void)
{
    if (!validateSettingHandlerRequest("SwitchType", false)) return;
    String message;
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    int returnCode = 200;
    int switchID;
    String newName =  "";
    String argToSearchFor[2] = {"Id", "Name"};
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, serverTransID++, "SwitchType", 0, "" );    
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
    {
      switchID = server.arg(argToSearchFor[0]).toInt();
    }  
    else
    {
       returnCode = 400;
       root["ErrorMessage"]= "Missing switchID argument";
       root["ErrorNumber"] = invalidValue ;     
       root.printTo(message);
       server.send(returnCode, "application/json", message);
       return;
    }
     
    if ( switchID >= 0 && switchID < numSwitches  )
    {
      if ( server.method() == HTTP_GET )
      {
          root["Value"] = switchEntry[switchID]->type;
      }
      else if( server.method() == HTTP_PUT && hasArgIC( argToSearchFor[1], server, false ) )
      {
          enum SwitchType newType = ( enum SwitchType ) server.arg(argToSearchFor[1]).toInt();          
          switch( newType )
          {
          case SWITCH_RELAY_NO:
          case SWITCH_RELAY_NC:
              switchEntry[switchID]->type = (enum SwitchType) newType;
              switchEntry[switchID]->min =  MINVAL;
              switchEntry[switchID]->max =  MAXBINARYVAL;
              switchEntry[switchID]->step = MAXBINARYVAL;
              break;
         
          case SWITCH_PWM:
              //Default frequency is 1000Hz
              switchEntry[switchID]->type = (enum SwitchType) newType;
              switchEntry[switchID]->min = (float) MINVAL;
              switchEntry[switchID]->max = (float) MAXDIGITALVAL; 
              switchEntry[switchID]->step = 1.0F;
              switchEntry[switchID]->value = switchEntry[switchID]->min;
              analogWriteRange ( 1024);
              analogWrite( switchEntry[switchID]->pin, switchEntry[switchID]->value );              
              returnCode = 200;
              break;
          case SWITCH_ANALG_DAC:                        
          default:
              root["ErrorMessage"]= "Invalid switch type not found or implemented";
              root["ErrorNumber"] = invalidValue ;
              returnCode = 400;
              break;
          }
      }
      else
      {
         returnCode = 400;
         root["ErrorMessage"]= "Invalid HTTP verb or arguments found";
         root["ErrorNumber"] = invalidOperation ;
      }
    }
    else
    {
       returnCode = 400;
       root["ErrorMessage"]= "Argument switchID out of range";
       root["ErrorNumber"] = invalidValue ;
    }
    root.printTo(message);
    server.send(returnCode, "application/json", message);
    return;
}

//GET ​/switch​/{device_number}​/getswitchvalue
//PUT ​/switch​/{device_number}​/setswitchvalue
//Get/Set the value of the specified switch device as a double - ie not a binary setting
void handlerDriver0SwitchValue(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "SwitchValue") || !readSwitchId(root, message, id)) return;

    SwitchEntry& entry = *switchEntry[id];
    if (server.method() == HTTP_GET)
    {
        if (entry.type == SWITCH_RELAY_NO || entry.type == SWITCH_RELAY_NC ||
            entry.type == SWITCH_PWM || entry.type == SWITCH_ANALG_DAC)
            root["Value"] = (double) entry.value;
        else
        {
            root["ErrorNumber"] = invalidOperation;
            root["ErrorMessage"] = "Value is unavailable for this switch type";
        }
    }
    else
    {
        if (!requireWritableSwitch(id, root, message)) return;
        double value = 0;
        if (!readSwitchValue(root, message, value)) return;
        if (entry.type == SWITCH_RELAY_NO || entry.type == SWITCH_RELAY_NC)
            setRelayOutput(id, value, root);
        else if (entry.type == SWITCH_PWM)
            setPwmOutput(id, value, root);
        else
        {
            root["ErrorNumber"] = notImplemented;
            root["ErrorMessage"] = "Output is not implemented for this switch type";
        }
    }
    sendSwitchResponse(root, message);
}
  
//GET ​/switch​/{device_number}​/minswitchvalue
//Gets the minimum value of the specified switch device as a double
void handlerDriver0MinSwitchValue(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "MinSwitchValue") || !readSwitchId(root, message, id)) return;
    root["Value"] = (double) switchEntry[id]->min;
    sendSwitchResponse(root, message);
}

//GET ​/switch​/{device_number}​/maxswitchvalue
//Gets the maximum value of the specified switch device as a double
void handlerDriver0MaxSwitchValue(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "MaxSwitchValue") || !readSwitchId(root, message, id)) return;
    root["Value"] = (double) switchEntry[id]->max;
    sendSwitchResponse(root, message);
}

//GET ​/switch​/{device_number}​/switchstep
//Returns the step size that this device supports (the difference between successive values of the device).
void handlerDriver0SwitchStep(void)
{
    String message;
    ASCOM_JSON_OBJECT(root, 256);
    int id = -1;
    if (!beginSwitchRequest(root, message, "SwitchStep") || !readSwitchId(root, message, id)) return;
    root["Value"] = (double) switchEntry[id]->step;
    sendSwitchResponse(root, message);
}

////////////////////////////////////////////////////////////////////////////////////
//Additional non-ASCOM custom setup calls

void handlerNotFound()
{
  server.send(400, "text/plain", "Unknown path or unsupported HTTP method: " + server.uri());
}

void handlerRestart()
{
  if (!validateSettingHandlerRequest("Restart")) return;
  String message;
  int responseCode = 200;
  server.sendHeader( WiFi.hostname().c_str(), String("/status"), true);
  server.send ( 302, F("text/html"), "<!Doctype html><html>Redirecting for restart</html>");
  DEBUGSL1(F("Reboot requested") );
  device.restart();
 }

void handlerNotImplemented()
{
  String message;
  uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
  uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();

  ASCOM_JSON_OBJECT(root, 250);
  jsonResponseBuilder( root, clientID, transID, serverTransID++, "HandlerNotFound", notImplemented  , "No REST handler implemented for argument - check ASCOM Dome v2 specification" );    
  root["Value"] = 0;
  sendSwitchResponse(root, message);
}

//GET ​/switch​/{device_number}​/status
//Get a descriptor of all the switches managed by this driver for discovery purposes
void handlerStatus(void)
{
    recordAlpacaReadActivity();
    String message, timeString;
    uint32_t clientID = 0;//(uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = 0;//(uint32_t)server.arg("ClientTransactionID").toInt();
    int i=0;
    int returnCode = 200;
    
    DynamicJsonBuffer jsonBuffer(1024); //Uplifted in size = 256 + numSwitches * ( sizeof ( switchentry ) + 3* MAX_NAME_LENGTH )
    JsonObject& root = jsonBuffer.createObject();
    JsonArray& entries = root.createNestedArray( "switches" );
    jsonResponseBuilder( root, clientID, transID, serverTransID++, "Status", 0, "" );    
    
    root["time"] = getTimeAsString( timeString );
    root["host"] = myHostname;
    root["build version"]         = String(__DATE__) + FPSTR(BuildVersionName);
    checkAlpacaConnectionTimeout();
    root["connected"] = alpacaConnection.connected();
    root["connectedId"] = alpacaConnection.ownerClientID;
    root["connectionTimeoutMs"] = connectedTimeoutMs;
    root["connectionTimerArmed"] = alpacaConnection.timerArmed;
    root["allowAnyConnectedClient"] = allowAnyConnectedClient;
    
    for( i = 0; i < numSwitches; i++ )
    {
      //Can I re-use a single object or do I need to create a new one each time? A: new one each time. 
      JsonObject& entry = jsonBuffer.createObject();

      entry["description"] = switchEntry[i]->description;
      entry["name"]        = switchEntry[i]->switchName;
      entry["type"]        = (int) switchEntry[i]->type;      
      entry["pin"]         = (int) switchEntry[i]->pin;      
      entry.set("writeable", switchEntry[i]->writeable );
      entry["min"]         = switchEntry[i]->min;
      entry["max"]         = switchEntry[i]->max;
      entry["step"]        = switchEntry[i]->step;
      if( switchEntry[i]->type == SWITCH_RELAY_NO || switchEntry[i]->type == SWITCH_RELAY_NC )
      {
        entry["state"]     = (switchEntry[i]->value == 1.0F ) ? true : false ;
      }
      else 
        entry["value"]     = switchEntry[i]->value; //Needs check limits to 1-1024, DAC and PWM limits. 
      entries.add( entry );
    }
    
    //Do something here about outputting the ADC records too
#if defined USE_ADC
      JsonArray& voltages = root.createNestedArray( "voltages" );
      for( i = 0; i < lastChannel; i++ )
      {
        JsonObject& entry = jsonBuffer.createObject();
        entry["adcReading"] = (float) adcReading[i];
        entry["adcScale"]   = (float) adcScaleFactor[i];
        entry["adcGain"]    = (float) adcGainFactor[ adcGainSettings[i] ];
        switch (i) 
        {
          case 0: entry["12v Voltage"] = (float) ( adcReading[i] * adcGainFactor[adcGainSettings[i]] * adcScaleFactor[i] ) ;
            break;
          case 1: entry["5v Voltage"] =  (float) ( adcReading[i] * adcGainFactor[adcGainSettings[i]] * adcScaleFactor[i] );
            break;
          case 2: entry["3v3 Voltage"] = (float) ( adcReading[i] * adcGainFactor[adcGainSettings[i]] * adcScaleFactor[i] );
            break;
          case 3: entry["Raw Voltage"] = (float) ( adcReading[i] * adcGainFactor[adcGainSettings[i]] * adcScaleFactor[i] ); 
            break;
          default:
            break;
        }
        voltages.add( entry );
      }

#endif 
    Serial.println( message);
    
    root.printTo(message);
    server.send(returnCode, F("application/json"), message);
    return;
}

//Helper function
void sendDeviceSetup( int returnCode, String& message, String& err )
 {
    //Send large pages in chunks
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    setupFormBuilderHeader( message );      
    server.send( returnCode, F("text/html"), message );
    message = "";

    setupFormBuilderDeviceHeader( message, err );      
    sendSetupContent(message);
    message = "";
    
    setupFormBuilderDeviceStrings( message );      
    sendSetupContent(message);
    message = "";    
    
    setupFormBuilderFooter( message );
    sendSetupContent(message);
 }

/*
 * Handlers to do custom setup that can't be done without a windows ascom driver setup form. 
 * Returns device page 
 */
 void handlerDeviceSetup( ) 
 {
    recordAlpacaReadActivity();
   String timeString, message, err;
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;
    
    int returnCode = 200;
    if ( server.method() == HTTP_GET )
    {
        sendDeviceSetup( returnCode, message, err );
    }
 }

  
 /*
  * Handler to update the hostname from the form.
  */
 void handlerDeviceHostname(void) 
 {
    if (!validateSettingHandlerRequest("SetHostname")) return;
    String message, timeString, err= "";
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;
    
    int returnCode = 400;
    String argToSearchFor[] = { "hostname" };
     
    if ( server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET)
    {
        if( hasArgIC( argToSearchFor[0], server, false )  )
        {
          String newHostname = server.arg(argToSearchFor[0]) ;
          //process form variables.
          if( newHostname.length() > 0 && newHostname.length() < MAX_NAME_LENGTH-1 )
          {
            //process new hostname
            strncpy( myHostname, newHostname.c_str(), MAX_NAME_LENGTH );
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
    
    sendDeviceSetup( returnCode, message, err );    

    if( returnCode == 200 ) 
    {              
      //Now reset to change the hostname and re-register DHCP
      device.reset();
    }
 return;
 }

/*
  * Handler to update the device Location from the form.
  */
 void handlerDeviceLocation(void) 
 {
    if (!validateSettingHandlerRequest("SetLocation")) return;
    String message, timeString, err= "";
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;
    
    int returnCode = 400;
    String argToSearchFor[] = { "location", };
     
    if ( server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET)
    {
        if( hasArgIC( argToSearchFor[0], server, false )  )
        {
          String newLocation = server.arg(argToSearchFor[0]) ;
          //process form variables.
          if( newLocation.length() > 0 && newLocation.length() < MAX_NAME_LENGTH-1 )
          {
            //process new hostname
            strncpy( Location, newLocation.c_str(), MAX_NAME_LENGTH );
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

    sendDeviceSetup( returnCode, message, err );
    return;    
 }

void handlerDeviceUdpPort(void) 
 {
    if (!validateSettingHandlerRequest("SetUdpPort")) return;
    String message, timeString, err = "";
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;    
   
    int returnCode = 400;
    String argToSearchFor[] = { "discoveryport"};
     
    if ( server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET)
    {
        if( hasArgIC( argToSearchFor[0], server, false )  )
        {
          int newPort = server.arg(argToSearchFor[0]).toInt() ;
          //process form variables.
          if( newPort > 1024 && newPort < 65536  )
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
      returnCode=400;
      err = "Bad HTTP request verb";
    }
    
    //Send large pages in chunks
    sendDeviceSetup( returnCode, message, err );
 }

//helper function
void sendDriver0Setup( int returnCode, String& message, String& err )
{
  //Send large pages in chunks
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  setupFormBuilderHeader( message );      
  server.send( returnCode, "text/html", message );
  message = "";
  
  setupFormBuilderDriverHeader( message, err );
  sendSetupContent(message);
  message = "";
  
  //List the switches
  for ( int i=0; i< numSwitches; i++ )
  {
    setupFormBuilderDriver0Switches( message, i ); 
    sendSetupContent(message);
    message = "";
  }

  setupFormBuilderFooter( message );
  sendSetupContent(message);
  message = "";
}

/*
 * Handlers to do custom setup that can't be done without a windows ascom driver setup form. 
 * Returns driver page for a specific Alpaca driver type, 
 * TODO might want to think about how to handle multiple drivers 
 */
 void handlerDriver0Setup(void)
 {
    recordAlpacaReadActivity();
   String message, timeString, err= "";
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;
    
    int returnCode = 200;
    if ( server.method() == HTTP_GET )
    {
        sendDriver0Setup( returnCode, message, err );
    }
 }

 /*
  * Handle update of the number of attached switches
  */
 void handlerDriver0SetupNumSwitches(void) 
 {
    if (!validateSettingHandlerRequest("SetNumSwitches")) return;
    String message, timeString, err= "";
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;
    
    int returnCode = 400;
    String argToSearchFor[] = { "numSwitches"};
     
    if ( server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET )
    {
        if( hasArgIC( argToSearchFor[0], server, false )  )
        {
          //process form variables.
          int newNumSwitches = server.arg(argToSearchFor[0]).toInt();
          if( newNumSwitches == 0 || newNumSwitches > MAXSWITCH )
          {
            err = "Switch size exceeds device range or is zero.";
          }
          else
          {
            //update the switches
            switchEntry = reSize( switchEntry, newNumSwitches );            
            numSwitches = newNumSwitches;
            saveToEeprom();
            returnCode = 200;              
          }
        }        
    }
    else
    {
      err = "Bad HTTP request verb";
    }

    sendDriver0Setup( returnCode, message, err );
    return;
 }

 /*
  * lengthy function to take the setup form output for each switch to manage its control parameters.
  * We don't attempt to set its value here. We just set the type, limits, features etc. The main API controls its use.
  */
 void handlerDriver0SetupSwitches(void) 
 {
    if (!validateSettingHandlerRequest("ConfigureSwitch")) return;
    String message, timeString, err= "";
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    int i;
    int returnCode = 400;
    String argToSearchFor[9] = { "switchId", "name", "description", "type", "writeable", "pin", "min", "max", "step" };
    
    if ( server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET )
    {
      //Expecting an array of variables in form submission
      //Need to parse and handle
      if( hasArgIC( argToSearchFor[0], server, false ) )
      {
        String arg; 
        String name = "default_name", description="default_description";
        double min = 0.0, max=0.0, step=0.0;
        int pin = NULLPIN;
        enum SwitchType type = SWITCH_RELAY_NO;
        bool  writeable = false;        
        bool allFound = true;
        int id = -1;
        
        //Get the id of the current switch to be updated 
        id = (int) server.arg( argToSearchFor[0] ).toInt();
        debugV( "found switchId %d\n", id );
        
        //Check for all args present 
        int i=1;
        int foundCount = 0;
        for ( i=1; i < 9; i++ ) 
        {
          arg = argToSearchFor[i] + id;
          debugV( "arg sought: %s\n", arg.c_str() );

          if( hasArgIC( arg, server, false )  )
          {
            debugV( "arg found: %s \n", arg.c_str() );
            allFound |= true;
            foundCount ++;
            debugW( "foundCount %d \n", foundCount);            
          }  
          else 
          {
            allFound &= false;          
          }
        }                    
  
        //4 is the number of variables expected for an analogue switch entry, 8 for a digital. 
        if( !allFound && foundCount == 4 ) 
        {
          enum SwitchType localType = (enum SwitchType) server.arg( argToSearchFor[3] + id ).toInt();
          debugV( "looking for type - found: %d\n", localType );
          
          if ( ( localType == SWITCH_RELAY_NC ) || ( localType == SWITCH_RELAY_NO ) )
          {
            debugW( "%s", "found analogue switch type AND expecting 4 variables");
            //We're all good - the disabled form variables are not passed when disabled.
            //That means we only see 4 rather than all 8 of them.                
            allFound = true;
            returnCode = 200;
            //Cache the existing values so they don't get overwritten with defaults. 
            min = switchEntry[id]->min;
            max = switchEntry[id]->max;
            step = switchEntry[id]->step;
            pin = switchEntry[id]->pin;
          }
          else
          {
             debugW( "%s\n", "type not found for analogue switch type - can't validate the 4 form variables found");
             err = "Incorrect switch type for number of variables supplied. Expecting id, name, description, type, writeable";
             returnCode = 0x402;
          }
        }
        else if ( !allFound && foundCount != 4 ) 
        {
          debugW( "not all args found - failed at %d", i );
          err = "Not all form variables supplied";
          returnCode = 0x402; //check
        }
        else
        {
          debugD( "%s\n", "All 8 args found - processing " );
          returnCode = 200;;//all found OK 
          err = "";
        }
  
        //Get the values and sense-check
        //Switch name
        debugV( "%s\n", "all args found - checking values ");

        arg = argToSearchFor[1] + id;
        if( hasArgIC( arg, server, false ) && returnCode == 200 )
        {
          name = server.arg( arg );
          if ( name.length() >= MAX_NAME_LENGTH  ) 
          {
            err = "Name longer than max allowed ";//Should be prevented by form controls.
            returnCode = 0x401;
            debugW( "%s\n", "switch name field not successfully parsed");            
          }
          else
            debugV( "Valid name field found : %s\n", name.c_str() );         
        }
          
        //Switch description        
        arg = argToSearchFor[2] + id;
        if( hasArgIC( arg, server, false ) && returnCode == 200 )
        {
          description = server.arg( arg );
          if ( description.length() >= MAX_NAME_LENGTH  ) 
          {
            err = "Description longer than max allowed ";//Should be prevented by form controls.
            returnCode = 0x403;
            debugW( "%s\n", "switch description field not successfully parsed");            
          }
          else
            debugV( "Valid description field found : %s\n", description.c_str() );                   
        }
  
        //Switch type
        arg = argToSearchFor[3] + id;
        if( hasArgIC( arg, server, false ) && returnCode == 200 )
        {
          bool found = false;
          type = SWITCH_NOT_SELECTED;

          int typeAsInt = server.arg( arg ).toInt();
          debugD( "switch type field found :%d\n", typeAsInt );
          
          if( typeAsInt >= 0 && typeAsInt < switchTypesLen )
          {
              found = true;              
              type = (enum SwitchType) typeAsInt;
              debugV( "switch type field matched :%d\n", type );
          }
          else 
          {
            debugW( "switch type field no valid value found (%d)\n", type );
            returnCode = 0x403;
            ;//
          }
        }
        
        //Switch writeable
        arg = argToSearchFor[4] + id;
        //If writeable is not checked, chrome doesn't send it as a form variable - hence its missing. 
        //So we have to assume its false if missing. 
        //Solved by replacing a checkbox with a pre-checked radio  
        writeable = false;
        if( hasArgIC( arg, server, false ) && returnCode == 200 )
        {
          writeable = (bool) server.arg( arg ).equalsIgnoreCase( "on" );
          debugW( "%s", "handlerSwitchSetup - writeable value found:");DEBUGSL1( writeable );
        }
        
        if( foundCount > 4 && allFound ) 
        {
          //Switch pin
          arg = argToSearchFor[5] + id;
          if( hasArgIC( arg, server, false ) && returnCode == 200 )
          {
            pin = (int) server.arg( arg ).toInt();
            if( type == SWITCH_PWM || type == SWITCH_ANALG_DAC )
            {
              //? in valid pin range ? 
              if ( ( pin != NULLPIN ) && ( pin >= MINPIN ) && ( pin <= MAXPIN ) )
              {
                int pinValid = false;
                int index = 0;
                
                debugD( "switch pin field found :%d\n", pin );

                //? already in use - means previous users need to be set to NULLPIN first. 
                while ( !pinValid && pinMap[index] != NULLPIN )
                {
                  pinValid = ( pin == pinMap[index] );
                  index++;
                }
                if ( !pinValid ) 
                {
                  pin = NULLPIN;
                  err = "PWM Pin value in range but already in use";//Not prevented by form controls.
                  returnCode = 0x403;
                  debugW( "%s\n", "switch pin value not in valid range");
                }
              }
              else 
              {
                err = "Digital Pin value not in range";//Should be prevented by form controls.
                returnCode = 0x403;
                debugW( "%s\n", "switch pin value not in valid range");
              }
            }
            else //other switch types - Should be prevented by form controls.
            {
              if ( pin != NULLPIN ) 
              {
                err = "Binary switches don't have non-null pin numbers";
                returnCode = 0x403;
                debugW( "%s", "switch pin field should be NULLPIN");              
              }
            }
          }
    
          //Switch min
          arg = argToSearchFor[6] + id;
          if( hasArgIC( arg, server, false ) && returnCode == 200  )
          {
            min = server.arg( arg ).toFloat();            
            if( type == SWITCH_PWM || type == SWITCH_ANALG_DAC )
            {
              if ( ( min < MINVAL ) || ( min > MAXDIGITALVAL ) ) 
              {
                err = "Min value out of digital range";//Should be prevented by form controls.
                returnCode = 0x400;
                debugW( "%s", "switch max field not successfully parsed OOR for type");
              }
            }
            else //Binary switches  - Should be prevented by form controls
            {
              if( ( min < MINVAL ) || ( min > MAXBINARYVAL ) ) 
              {
                  err = "Min value out of binary/analogue range";
                  returnCode = 0x400;
                  debugW( "%s", "switch min field not in range");                
              }
            }
          }
    
          //Switch max - Should be prevented by form controls.
          arg = argToSearchFor[7] + id;
          if( hasArgIC( arg, server, false ) && returnCode == 200 )
          {
            max = (float) server.arg( arg ).toFloat();
            if( type == SWITCH_PWM || type == SWITCH_ANALG_DAC )
            {
              if ( ( max < MINVAL ) || ( max > MAXDIGITALVAL ) ) 
              {
                err = "Max value out of digital range";
                returnCode = 0x400;
                debugW( "%s", "switch max field not in digital range");              
              }
            }
            else 
            {
              if( ( max < MINVAL) || ( max > MAXBINARYVAL ) ) 
              {
                  err = "Max value out of binary/analogue range";//Should be prevented by form controls.
                  returnCode = 0x400;
                  debugW( "%s\n", "switch max field not in analogue range");                            
              }
            }
          }
    
          //Switch step
          arg = argToSearchFor[8] + id;
          if( hasArgIC( arg, server, false ) && returnCode == 200 )
          {
            step = (float) server.arg( arg ).toFloat();
            if( type == SWITCH_PWM || type == SWITCH_ANALG_DAC )
            {
              if ( step < MINVAL || step > MAXDIGITALVAL ) 
              {
                err = "Max step value out of digital range";//Should be prevented by form controls.
                returnCode = 0x400;
                debugW( "%s\n", "switch step field not in digital range");                            
              }
            }
            else 
            {
              if( ( step < MINVAL ) || ( step > MAXBINARYVAL ) ) 
              {
                  err = "Step value out of binary/analogue range";//Should be prevented by form controls.
                  returnCode = 0x400;
                  debugW( "%s\n", "handlerSwitchSetup - switch step field not in analogue range");                              
              }
            }            
          }
        }//end of exclusion for disabled fields based on switch types. 
        else
            debugW( "%s\n", "switch step not enough fields > 4 but less than 7 ");                                      

      /*
      Apply some logic to the combinations of values to check for errors
      Not checking for :  duplicates (names/description/pins)
                          step values are reasonable.
      */
        if ( returnCode == 200 )
        {
          if ( ( min > max ) )
          {
            returnCode = 0x402;
            err = "Bad values : check min <= max ";              
            debugW("%s", "switch min/max/step field not in range");              
          }
          else //Save the values found - they look OK otherwise
          {
            DEBUGSL1("handlerSwitchSetup - saving new values");
            strcpy( switchEntry[id]->switchName, name.c_str() );
            strcpy( switchEntry[id]->description, description.c_str() );
            switchEntry[id]->max = max;
            switchEntry[id]->min = min;
            switchEntry[id]->step = step;
            switchEntry[id]->pin = pin;
            switchEntry[id]->writeable = writeable;
            //Update current value to fall within new range 
            switchEntry[id]->value = switchEntry[id]->min;
            
            //Has the switch type changed for this switch ? 
            if( switchEntry[id]->type != SWITCH_PWM && type == SWITCH_PWM ) 
            {
              switchEntry[id]->type = (enum SwitchType ) type; 
              analogWrite( pin, min );
            }
            //Are we re-setting the existing PWM mode pin
            else if ( switchEntry[id]->type == SWITCH_PWM && type == SWITCH_PWM )
            {
              analogWrite( pin, 0 );
            }
            //Or something else 
            else 
              analogWrite( pin, 0 );
            
            //Save the new setup
            saveToEeprom();
            returnCode = 200;
          }         
        }
      }
      else
      {
        err = "Switch Id ('switchId') missing ";
        returnCode = 400; //check    
      }                         
    }//End of GET/POST/PUT
    else //Not an acceptable HTML verb
    { 
      err = "Not a supported HTML verb (PUT/GET/POST)";
      returnCode = 403; //check    
    }

    sendDriver0Setup( returnCode, message, err );
    return;  
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

String& setupFormBuilderHeader( String& htmlForm )
{
  String hostname = WiFi.hostname();

  htmlForm = "<!DocType html><html lang=en ><head><meta charset=\"utf-8\">";
  htmlForm += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  htmlForm += FPSTR(setupPageStyle);

  //Used to enable/disable the input fields for binary relays vs digital PWM and DAC outputs. 
  htmlForm += "<script>function setTypes( a ) { \
  var searchFor = \"type\"+a;\
  var x = document.getElementById(searchFor).value;\
  if( x.indexOf( \"2\" ) >= 0 || x.indexOf( \"3\" ) >= 0 )\
  {\
      document.getElementById(\"pin\"+a).disabled = false;\
        document.getElementById(\"pin\"+a).min=";
  htmlForm += MINPIN;
  htmlForm += "; document.getElementById(\"pin\"+a).max=";
  htmlForm += MAXPIN;
  htmlForm += "; document.getElementById(\"min\"+a).disabled=false;";
  htmlForm += "document.getElementById(\"min\"+a).min=";
  htmlForm += MINVAL;
  htmlForm += "; document.getElementById(\"min\"+a).max=";
  htmlForm += MAXDIGITALVAL;
  htmlForm += "; document.getElementById(\"max\"+a).disabled=false;\
        document.getElementById(\"max\"+a).min=";
  htmlForm += MINVAL;
  htmlForm += "; document.getElementById(\"max\"+a).max=";
  htmlForm += MAXDIGITALVAL;
  htmlForm += "; document.getElementById(\"step\"+a).disabled=false;\
        document.getElementById(\"step\"+a).min=";
  htmlForm += MINVAL;
  htmlForm += "; document.getElementById(\"step\"+a).max = ";
  htmlForm += MAXDIGITALVAL;
  htmlForm += "}\n\
  else\n\
  {\
      document.getElementById(\"pin\"+a).disabled = true;\      
        document.getElementById(\"pin\"+a).value=";
        htmlForm += NULLPIN;
        htmlForm += "; ";
        htmlForm += "document.getElementById(\"pin\"+a).min=0;\
          document.getElementById(\"pin\"+a).max=";
        htmlForm += NULLPIN;
        htmlForm += "; ";
      htmlForm +=" document.getElementById(\"min\"+a).disabled = true;\
        document.getElementById(\"min\"+a).value=";
        htmlForm += MINVAL;
        htmlForm += "; ";
        htmlForm += "document.getElementById(\"min\"+a).min=";
        htmlForm += MINVAL;
        htmlForm += "; ";
        htmlForm += "document.getElementById(\"min\"+a).max=";
        htmlForm += MAXBINARYVAL;
        htmlForm += "; ";
      htmlForm += "document.getElementById(\"max\"+a).disabled = true;\
          document.getElementById(\"max\"+a).value=";  
        htmlForm += MAXBINARYVAL;
        htmlForm += "; ";
        htmlForm += "document.getElementById(\"max\"+a).min=";
        htmlForm += MINVAL;
        htmlForm += "; ";
        htmlForm += "document.getElementById(\"max\"+a).max=";
        htmlForm += MAXBINARYVAL;
        htmlForm += "; ";
      htmlForm += "; document.getElementById(\"step\"+a).disabled = true;\  
        document.getElementById(\"step\"+a).value = 0.0;\  
        document.getElementById(\"step\"+a).min = 0.0;\  
        document.getElementById(\"step\"+a).max = 1.0;\  
  }\
}</script>";
  htmlForm += "</head>";
  
  return htmlForm;
}

String& setupFormBuilderDeviceHeader( String& htmlForm, String& errMsg )
{
  String hostname = WiFi.hostname();

  htmlForm = F("<body><div class=\"container\">");
  htmlForm += F("<div class=\"row\" id=\"topbar\" bgcolor='A02222'>");
  htmlForm += F("<p> This is the setup page for the <a href=\"http://www.skybadger.net\">Skybadger</a> <a href=\"https://www.ascom-standards.org\">ASCOM</a> device '");
  htmlForm += myHostname;
  htmlForm += F("' device which implements the <a href=\"https://www.ascom-standards.org/api\">ALPACA</a> v1.0 API</b> for the devices listed below:");
  htmlForm += F("<ul><li> <a href=\"/api/v1/switch/0/setup\"> ALPACA Switch 0</a> </li>");
  htmlForm += F("<li> <end of drivers implemented>. </li>");
  htmlForm += F("</ul></div>");

  if( errMsg != NULL && errMsg.length() > 0 ) 
  {
    htmlForm += F("<div class=\"row\" id=\"errorbar\" bgcolor='lightred'>");
    htmlForm += F("<b>Error Message</b>");
    htmlForm += F("</div><hr>");
  }
 
  return htmlForm;
}

String& setupFormBuilderDeviceStrings( String& htmlForm )
{
  String hostname = WiFi.hostname();
 
  //UDP Discovery port 
  htmlForm  = F( "<div class=\"row float-left\" id=\"topbar-device\" bgcolor='A02222'> ");
  htmlForm += F("<div class=\"col-sm-12\">" );
  htmlForm += F("<p> This device supports the <a href=\"placeholder\">ALPACA UDP discovery API</a> on port: ");
  htmlForm += udpPort;
  htmlForm += F("</p></div></div>");
  
  //Device settings hostname 
  htmlForm += F("<div class=\"row float-left\">");
  htmlForm += F("<div class=\"col-sm-12\"><h2> Enter new hostname for device</h2><br/>");
  htmlForm += F("<p>Changing the hostname will cause the device to reboot and may change the IP address!</p>");
  htmlForm += F("<form method=\"POST\" id=\"hostname\" action=\"http://");
  htmlForm.concat( myHostname );
  htmlForm += F("/setup/hostname\">");
  htmlForm += F("<input type=\"text\" name=\"hostname\" maxlength=\"");
  htmlForm += String(MAX_NAME_LENGTH).c_str( );
  htmlForm += F("\" value=\"");
  htmlForm.concat( myHostname );
  htmlForm += F("\"/>");
  htmlForm += F("<label for=\"hostname\" > Hostname </label>");
  htmlForm += F("<input type=\"submit\" value=\"Set hostname\" />");
  htmlForm += F("</form></div></div>");

  //Device settings location
  htmlForm += "<div class=\"row float-left\">";
  htmlForm += "<div class=\"col-sm-12\"><h2> Enter location to be reported by Management API for device</h2><br/>";
  htmlForm += "<form method=\"POST\" id=\"location\" action=\"http://";
  htmlForm.concat( myHostname );
  htmlForm += "/setup/location\">";
  htmlForm += "<input type=\"text\" name=\"location\" maxlength=\"";
  htmlForm += String(MAX_NAME_LENGTH).c_str( );
  htmlForm += "\" value=\"";
  htmlForm.concat( Location );
  htmlForm += "\"/>";
  htmlForm += "<label for=\"location\" > Location </label>";
  htmlForm += "<input type=\"submit\" value=\"Set location\" />";
  htmlForm += "</form></div></div>";  

 //UDP Port
  htmlForm += "<div class=\"row float-left\" id=\"discovery-port\" >";
  htmlForm += "<div class=\"col-sm-12\"><h2> Enter new Discovery port number for device</h2>";
  htmlForm += "<form method=\"POST\" action=\"http://";
  htmlForm.concat( myHostname );
  htmlForm += "/setup/udpport\">";
  htmlForm += "<label for=\"udpport\" id=\"udpport\"> Port number to use for Management API discovery </label>";
  htmlForm += "<input type=\"number\" name=\"udpport\" min=\"1024\" max=\"65535\" ";
  htmlForm += "value=\"";
  htmlForm.concat( udpPort );
  htmlForm += "\"/>";
  htmlForm += "<input type=\"submit\" value=\"Set port\" />";
  htmlForm += "</form></div></div>"; 

 return htmlForm;
}

String& setupFormBuilderDriverHeader( String& htmlForm, String& errMsg )
{
  String hostname = WiFi.hostname();
  
  debugI("%s", "called driver0 header ");
  htmlForm  = F("<body> <div class=\"container-fluid\">");
  htmlForm += F("<div class=\"row float-left\" id=\"topbar\" bgcolor=\"A02222\">");
  htmlForm += F("<div class=\"col-sm-12\">");
  htmlForm += F("<p> This is the driver setup page for the <a href=\"http://www.skybadger.net\">Skybadger</a> <a href=\"https://www.ascom-standards.org\">ASCOM</a> Switch device ");
  htmlForm += myHostname;
  htmlForm += F(" which uses the <a href=\"https://www.ascom-standards.org/api\">ALPACA</a> v1.0 API</b> for Switches");
  htmlForm += F("<p> Click <a href=\"/setup\"> Device Management <a/> to manage the device.  </p>");
  htmlForm += F("</div></div>");

  if( errMsg != NULL && errMsg.length() > 0 ) 
  {
    htmlForm += F("<div class=\"row\" id=\"errorbar\" bgcolor='lightred'><div class=col-sm-12>");
    htmlForm += F("<div class=\"col-sm-12\"><b>");
    htmlForm.concat ( errMsg );
    htmlForm += F("</b></div></div><hr>");
  }
  
  //Setup the number of switches - should be in a separate function but lumped in here.. Separate out when have multiple drivers for this device
  htmlForm += "<div class=\"row float-left\"> ";
  htmlForm += "<div class=\"col-sm-12\"><h2>Configure switches</h2><br/>";
  htmlForm += "<p>Editing this to add switch components ('upscaling') will copy the existing setup to the new setup but you will need to edit the added switches. </p>";
  htmlForm += "<p>Editing this to reduce the number of switch components ('downscaling') will delete the configuration for the switches dropped but retain those lower number switch configurations for further editing</p><br>";
  htmlForm += "</div></div>";

  htmlForm += "<div class=\"row\" id=\"numSwitches\">";
  htmlForm += "<div class=\"col-sm-12\">";
  htmlForm += "<form action=\"/api/v1/switch/0/numswitches\" method=\"POST\" id=\"switchcount\" >";
  htmlForm += "<label for=\"numSwitches\" >Number of switch components</label>";
  htmlForm += "<input type=\"number\" name=\"numSwitches\" min=\"1\" max=\"";
  htmlForm += String( MAXSWITCH ).c_str();
  htmlForm += "\" value=\"";
  htmlForm += numSwitches;
  htmlForm += "\">";
  htmlForm += "<input type=\"submit\" value=\"Update\"> </form> </div></div>";
 
  htmlForm += "<div class=\"row float-left\">";
  htmlForm += "<div class=\"col-sm-12\" id=\"switchConfig\"> <h2>Switch configuration </h2>";
  htmlForm += "<br><p>To configure the switch types and limits, select the switch you need below.</p></div></div>";
  return htmlForm;
}

String& setupFormBuilderDriver0Switches( String& htmlForm, int index )
{
  String hostname = WiFi.hostname();

  htmlForm  = F("<div class=\"row float-left\" id=\"switch" );
  htmlForm += index;
  htmlForm += F("\"><div class=\"col-sm-6\">" );
  htmlForm += F("<form action=\"/api/v1/switch/0/switches\" method=\"POST\">" );
  htmlForm += F("<fieldset class=\"border p2\">");
  htmlForm += F("<input type=\"hidden\" value=\"");
  htmlForm += index;
  htmlForm += F("\" name=\"switchId\" />");

  htmlForm += F("<legend class=\"w-auto\">Settings Switch ");
  htmlForm += index;
  htmlForm += F("</legend>");

  //Name
  htmlForm += F("<label for=\"name");
  htmlForm += index;
  htmlForm += F("\"><span>Switch Name</span></label>");
  htmlForm += F("<input type=\"text\" id=\"fname");
  htmlForm += index;
  htmlForm += F("\" name=\"name");
  htmlForm += index;
  htmlForm += F("\" value=\"");
  htmlForm += switchEntry[index]->switchName;
  htmlForm += F("\" maxlength=\"");
  htmlForm += String(MAX_NAME_LENGTH).c_str( );
  htmlForm += F("\"><br>");
     
  //Description
  htmlForm += "<label for=\"description";
  htmlForm += index;
  htmlForm += "\"><span>Description</span></label>";
  htmlForm += "<input type=\"text\" id=\"lname";
  htmlForm += index;
  htmlForm += "\" name=\"description";
  htmlForm += index;
  htmlForm += "\" value=\"";
  htmlForm += switchEntry[index]->description;
  htmlForm += "\" maxlength=\"";
  htmlForm += String(MAX_NAME_LENGTH).c_str( );
  htmlForm += "\"><br>";
  
  //Type - Hardware implementation detail exposed for configuration
  htmlForm += "<label for=\"type";
  htmlForm += index;
  htmlForm += "\"><span>Switch type</span></label>";
  htmlForm += "<select id=\"type";
  htmlForm += index;
  htmlForm += "\" name=\"type";
  htmlForm += index;
  htmlForm += "\" onChange=\"setTypes( ";
  htmlForm += index;
  htmlForm += " )\">";
  for( int k=0; k < 4; k++ ) 
  {
    htmlForm += "<option value=\"";
    htmlForm += k;
    htmlForm += "\" ";
    if ( ( (int) switchEntry[index]->type ) == k )
      htmlForm += " selected ";
    htmlForm += ">";
    htmlForm += switchTypes[k].c_str();
    htmlForm += "</option>";
  }
  htmlForm += "</select> <br>";

  //Pin - Hardware implementation detail exposed for configuration
  htmlForm += "<label for=\"pin";
  htmlForm += index;
  htmlForm += "\"><span>Hardware pin</span></label>";
  
  /*As a number field 
  htmlForm += "<input ";
  if( switchEntry[index]->type == SWITCH_RELAY_NC || 
      switchEntry[index]->type == SWITCH_RELAY_NO || 
      sizeof( pinMap ) == 0 ) 
  {
    htmlForm += "disabled" ;
  }
  htmlForm += " type=\"number\" id=\"pin";
  htmlForm += " id=\"pin";
  htmlForm += index;
  htmlForm += "\" name=\"pin";
  htmlForm += index;
  htmlform += "\" default=\"";
  htmlForm += switchEntry[index]->pin;
  htmlForm += "\" min=\"";
  htmlForm += MINPIN;  
  htmlForm += "\" max=\"";
  htmlForm += MAXPIN;
  htmlForm += "\" >
  
  */
  //As a select with options 
  //<select> <option value="audi">Audi</option> </select>
  htmlForm += "<select ";
  if( switchEntry[index]->type == SWITCH_RELAY_NC || 
      switchEntry[index]->type == SWITCH_RELAY_NO || 
      sizeof( pinMap ) == 0 ) 
  {
    htmlForm += "disabled" ;
  } 
  
  htmlForm += " id=\"pin";
  htmlForm += index;
  htmlForm += "\" name=\"pin";
  htmlForm += index;
  htmlForm += "\">";
  
  int i=0;
  while ( pinMap[i] != NULLPIN )
  {
    htmlForm += "<option value=\"";
    htmlForm += pinMap[i];
    htmlForm += "\">";
    htmlForm += String( pinMap[i] );
    htmlForm += "</option>";
    i++;
  };
  htmlForm += "</select><br>";  
  
  //Min value for switch 
  htmlForm += "<label for=\"min";
  htmlForm += index;
  htmlForm += "\"><span>Switch min value</span></label>";
  htmlForm += "<input type=\"number\" id=\"min";
  htmlForm += index;
  htmlForm += "\" name=\"min";
  htmlForm += index;
  htmlForm += "\" value=\"";
  htmlForm += switchEntry[index]->min;
  htmlForm += "\" min=\"";
  if( switchEntry[index]->type == SWITCH_RELAY_NC || switchEntry[index]->type == SWITCH_RELAY_NO ) 
  {
    htmlForm += MINVAL;  
    htmlForm += "\" max=\"";
    htmlForm += MAXBINARYVAL;
    htmlForm += "\" disabled><br>";  
  }
  else
  {
    htmlForm += MINVAL;  
    htmlForm += "\" max=\"";
    htmlForm += MAXDIGITALVAL;
    htmlForm += "\"><br>";  
  }

  //Max value for switch
  htmlForm += "<label for=\"max";
  htmlForm += index;
  htmlForm += "\"><span>Max value</span></label>";
  htmlForm += "<input type=\"number\" id=\"max";
  htmlForm += index;
  htmlForm += "\" name=\"max";
  htmlForm += index;
  htmlForm += "\" value=\"";
  htmlForm += switchEntry[index]->max;
  htmlForm += "\" min=\"";
  if( switchEntry[index]->type == SWITCH_RELAY_NC || switchEntry[index]->type == SWITCH_RELAY_NO ) 
  {
    htmlForm += MINVAL;  
    htmlForm += "\" max=\"";
    htmlForm += MAXBINARYVAL;
    htmlForm += "\" disabled><br>";  
  }
  else
  {
    htmlForm += MINVAL;  
    htmlForm += "\" max=\"";
    htmlForm += MAXDIGITALVAL;
    htmlForm += "\" ><br>";  
  }
  
  //Step - number of steps in range for switch
  htmlForm += "<label for=\"step";
  htmlForm += index;
  htmlForm += "\"><span>Steps in range</span></label>";
  htmlForm += "<input type=\"number\" id=\"step";
  htmlForm += index;
  htmlForm += "\" name=\"step";
  htmlForm += index;
  htmlForm += "\" value=\"";
  htmlForm += switchEntry[index]->step;
  htmlForm += "\" min=\"";
  if( switchEntry[index]->type == SWITCH_RELAY_NC || switchEntry[index]->type == SWITCH_RELAY_NO ) 
  {
    htmlForm += MINVAL;  
    htmlForm += "\" max=\"";
    htmlForm += MAXBINARYVAL;
    htmlForm += "\" disabled ><br>";  
  }
  else
  {
    htmlForm += MINVAL;  
    htmlForm += "\" max=\"";
    htmlForm += MAXDIGITALVAL;
    htmlForm += "\" ><br>";  
  }

  //Writeable
  htmlForm += "<label for=\"writeable";
  htmlForm += index;
  htmlForm += "\"><span>Writeable</span></label>";
  htmlForm += " &nbsp; <input type=\"radio\" value=\"on\" id=\"writeable";
  htmlForm += index;
  htmlForm += "\" name=\"writeable";
  htmlForm += index;
  htmlForm += "\""; 
  if( switchEntry[index]->writeable )
    htmlForm += " checked";
  htmlForm += ">";
  htmlForm += "<label for=\"writeableB";
  htmlForm += index;
  htmlForm += "\"><span>ReadOnly</span></label>";
  htmlForm += " &nbsp; <input type=\"radio\" value=\"off\" id=\"writeableB";
  htmlForm += index;
  htmlForm += "\" name=\"writeable";
  htmlForm += index;
  htmlForm += "\""; 
  if( !switchEntry[index]->writeable )
    htmlForm += " checked";
  htmlForm += "> <br>";
 
  //Form submit button. 
  htmlForm += "<input type=\"submit\" value=\"Update\">";
  htmlForm += "</fieldset> "; 
  htmlForm += "</form></div></div>";

  return htmlForm;
}

//footer
String& setupFormBuilderFooter( String& htmlForm )
{
  //restart button
  htmlForm =  F("<div class=\"row float-left\" id=\"restartField\" >");
  htmlForm += F("<div class=\"col-sm-12\"><h2> Restart device</h2>");
  htmlForm += F("<form class=\"form-inline\" method=\"POST\" action=\"http://");
  htmlForm.concat( myHostname );
  htmlForm += F("/restart\">");
  htmlForm += F("<input type=\"submit\" class=\"btn btn-default\" value=\"Restart device\" />");
  htmlForm += F("</form>");
  htmlForm += F("</div></div>");
  
  //Update button
  htmlForm += F("<div class=\"row float-left\" id=\"updateField\" >");
  htmlForm += F("<div class=\"col-sm-12\"><h2> Update firmware</h2>");
  htmlForm += F("<form class=\"form-inline\" method=\"GET\"  action=\"http://");
  htmlForm.concat( myHostname );
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
#endif
