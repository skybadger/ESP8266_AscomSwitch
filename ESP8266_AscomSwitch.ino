/*
 Program to implement ASCOM ALPACA compliant switch interface for remote relays used in power or signalling devices. 
 Typically implemented dusing wireless to talk to the device, and the device uses a PCF8574 I2C serial expander to control an 8 or 16 bit wide data bus
 You can get simple relay boards off the web for ten GBP or so and create a cable to connect the two together.
 Supports web interface on port 80 returning json string managed through ALPACA api and supporting Alpaca discovery and management api
 Note that this implementation expect to use the PCF8574 for relay board driving. You could of course use it for both binary input and output or not use i2c at all and use the loal pins for 
 really small switch implementations. 

Notes: 
 This design now requires a larger memory size than 1MB for OTA operation and to return the html pages and debug output correctly. 
 Step field is interpreted as number of digital steps in full range from min to max. ie a 10-bit DAC will allow a value of 1024 
 The i2c pin connections need to be reversed between ASW01 and ASW 02. ASW01 is the normal way around. 
 
 To do:
 Complete Setup page - in progress
 Complete support for device-based PWM
 Complete support for DAC output
  
 Done: 
 Complete EEPROM calls - done.
 PCF8574 library added to support switches - needs physical integration testing.
 Add suport for PWM hardware chip(s).
 Added support for measuring the voltages we are switching - unreg source input, reg 5V and 3.3v - ASW01 h/w only has the first two channels implemented.
 8574 DIP16 pinout is: 
 1 A0       15 Vdd
 2 A1       15 SDA
 3 A2       14 SCL
 4 P0       13 /INT
 5 P1       12 P7
 6 P2       11 P6
 7 P3       10 P5
 8 Vss      09 P4

 Layout:
 Pin 13 to PWM output
 GPIO 4,2 to SDA
 GPIO 5,0 to SCL 
 All 3.3v logic. 
 
Dependencies
Remote Debug telnet service https://github.com/JoaoLopesF/RemoteDebug
Pubsub Client https://pubsubclient.knolleary.net/api.html
I2C PCF8584 expander library 

Test:
curl -X PUT http://espASW01/api/v1/switch/0/Connected -d "ClientID=0&ClientTransactionID=0&Connected=true" (note cases)
http://espASW01/api/v1/switch/0/status
telnet espASW01 32272 (UDP)

Change Log
19/10/2021 Updatees to normalise and fill out dependencies 
07/01/2022 Switch settings handler not working - failing to update due to dodgy switch type handling - expecting int and receiving string. Fixed. 
17/01/2022 Added pin type re-setting function to allow PWM pins to be changed. 
           Need to document that PWM types used in the middle of relay ranges will remove the relay from operations. PWM should be mapped as switch entries only after relays are all assigned.           

If we want to link a switch output to a heater - is this a step too far? 
 * Needs a dewpoint temperature. 
 * Needs a local temp sensor 
 * Needs a PWM managed switched current driver at 5v or 12v
 * Needs to relate PWM output to dewpoint - local. 
 * Options: 
 * 1 - let the user do it and leave this as a dumb switch 
 * 2 - build intelligence into this dumb switch . 
 * 3 - build it into node-red and let node-red manage the switch based on sensor input and central dewpoint. 
 * 4 - something else. 
 * So - node red it is. 
*/

//define the processor in use  - could also be ESP8266_12 and later the ESP32
#define ESP8266_01
//turns on debug output via debugStrings macros - either to serial or telnet if telnet is enabled.
#define DEBUG_ESP_MH
//Use for client performance testing
#define DEBUG_ESP_HTTP_CLIENT
//Manage the remote debug interface, it takes 6K of memory with all the strings
//#define DEBUG_DISABLED
//#define DEBUG_DISABLE_AUTO_FUNC true
#define WEBSOCKET_DISABLED true //No impact to memory requirement
#define MAX_TIME_INACTIVE 0     //to turn off the de-activation of a telnet session

//Turn on or off use of the ADC for voltage monitoring
#define USE_ADC

#include "RemoteDebug.h" //https://github.com/JoaoLopesF/RemoteDebug
#include "DebugSerial.h"
#include "SkybadgerStrings.h"
#include "Webrelay_common.h"
#include "AlpacaErrorConsts.h"
#include <esp8266_peri.h> //register map and access
#include <ESP8266WiFi.h>
#include <PubSubClient.h> //https://pubsubclient.knolleary.net/api.html
#include <EEPROM.h>
#include "EEPROMAnything.h"
#include <Wire.h> //https://playground.arduino.cc/Main/WireLibraryDetailedReference
#include <time.h> //Look at https://github.com/PaulStoffregen/Time for a more useful internal timebase library
#include <WiFiUdp.h> //WiFi UDP discovery responder
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ArduinoJson.h>
#include <ArduinoJsonCompat.h>
#include "AlpacaRequest.h"

// Opt this application into strict optional IDs and its device connection handler.
#define ASCOM_STRICT_OPTIONAL_IDS 1
#define ASCOM_CUSTOM_CONNECTED_HANDLER 1
int validateAlpacaRequestOwnership(uint32_t clientID, bool setting);
#define ASCOM_VALIDATE_REQUEST_OWNERSHIP(clientID, setting)                                        \
    validateAlpacaRequestOwnership(clientID, setting)

//Create a remote debug object
#if !defined DEBUG_DISABLED
RemoteDebug Debug;
#endif

extern "C"
{
//Ntp dependencies - available from v2.4
#include <time.h>
#include <sys/time.h>
    //#include <coredecls.h>

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <String.h>
#include <user_interface.h>
}
time_t now; //use as 'gmtime(&now);'

//Program constants
#if !defined DEBUG_DISABLED
const char *BuildVersionName PROGMEM = "LWIPv2 lo memory, RDebug enabled \n";
#else
const char *BuildVersionName PROGMEM = "LWIPv2 lo memory, RDebug disabled \n";
#endif

WiFiClient espClient;
PubSubClient client(espClient);
volatile bool callbackFlag = false;

// Create an instance of the server - used for Device and driver UI
// specify the port to listen on as an argument
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;

//UDP Port can be edited in setup page - used for Alpaca discovery
WiFiUDP Udp;

//Hardware device system functions - reset/restart etc
EspClass device;
ETSTimer timer, timeoutTimer;
volatile bool newDataFlag = false;
volatile bool timeoutFlag = false;
volatile bool timerSet = false;

void onTimer(void);
void onTimeoutTimer(void);
void setupWifi(void);
void setPins(void);
void publishHealth(void);
#if defined USE_ADC
void publishADC(void);
#endif

//Make these variables rather than constants to allow the custom setup to change them and store them to EEPROM
int numSwitches = 0;
SwitchEntry **switchEntry;

const int eepromSize = 4096;

//Order sensitive
#include "Skybadger_common_funcs.h"
#include "JSONHelperFunctions.h"
#include "ASCOMAPICommon_rest.h" //From library/ASCOM_REST - ASCOM common driver descriptors and handlers. Override as required.
#include "MappedSources.h"
#include "Webrelay_eeprom.h"
#include "ESP8266_relayhandler.h"
#include "AlpacaManagement.h"

/**
 * @brief Initialize the ESP8266 switch controller.
 * @details Load settings, initialize inactive sources, connect networking and register HTTP, MQTT and timer services.
 * @par Arguments
 * None.
 * @return void; effects are described above.
 */
void setup()
{
    Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
    Serial.println(F("ESP starting."));

    //Start NTP client
    configTime(TZ_SEC, DST_SEC, timeServer1, timeServer2, timeServer3);
    delay(5000);

    //Setup default data structures
    DEBUGSL1("Setup EEprom variables");
    EEPROM.begin(eepromSize);
    //setDefaults();
    setupFromEeprom();
    DEBUGSL1("Setup eeprom variables complete.");

    Wire.begin(boardSdaPin, boardSclPin);
    Wire.setClock(100000);
    Wire.setClockStretchLimit(25000);
    sourceBusStarted = true;
    for (int i = 0; i < numSwitches; ++i)
        initializeNativeSource(*switchEntry[i]);

    // Connect to wifi
    setupWifi();

    //Open a connection to MQTT
    DEBUGSL1("Setting up MQTT.");
    client.setServer(mqttServerName, mqttPort);
    String lastWillTopic = outHealthTopic;
    lastWillTopic.concat(myHostname);
    client.setBufferSize(512); //rather than using library defines.
    client.connect(thisID, pubsubUserID, pubsubUserPwd, lastWillTopic.c_str(), 1, true,
                   "disconnected");
    //Create a timer-based callback that causes this device to read the local i2C bus devices for data to publish.
    client.setCallback(callback);
    client.subscribe(inTopic);
    publishHealth();

#if !defined DEBUG_DISABLED
    //Debugging over telnet setup
    // Initialize the server (telnet or web socket) of RemoteDebug
    //Debug.begin(HOST_NAME, startingDebugLevel );
    Debug.begin(WiFi.hostname().c_str(), Debug.VERBOSE);
    Debug.setSerialEnabled(true); //until set false
    // Options
    // Debug.setResetCmdEnabled(true); // Enable the reset command
    // Debug.showProfiler(true); // To show profiler - time between messages of Debug
    debugI("Remote debugger enabled and operating");
#endif //remote debug

    //for use in debugging reset - may need to move
    debugI("Device reset reason: %s", device.getResetReason().c_str());
    debugD("Device reset info: %s", device.getResetInfo().c_str());

    ////////////////////////////////////////////////////////////////////////////////////////

    String outbuf = scanI2CBus();
    Serial.println(outbuf.c_str());
    debugV("I2C scan output: %s", outbuf.c_str());

    ////////////////////////////////////////////////////////////////////////////////////////

    //Setup webserver handler functions
    server.on("/", handlerStatus);
    server.onNotFound(handlerNotFound);

    //Common ASCOM handlers

    //Generic ASCOM descriptor functions
    server.on("/api/v1/switch/0/action", HTTP_PUT, handleAction);
    server.on("/api/v1/switch/0/commandblind", HTTP_PUT, handleCommandBlind);
    server.on("/api/v1/switch/0/commandbool", HTTP_PUT, handleCommandBool);
    server.on("/api/v1/switch/0/commandstring", HTTP_PUT, handleCommandString);
    server.on("/api/v1/switch/0/connected", HTTP_GET, handleConnected);
    server.on("/api/v1/switch/0/connected", HTTP_PUT, handleConnected);
    server.on("/api/v1/switch/0/description", HTTP_GET, handleDescriptionGet);
    server.on("/api/v1/switch/0/driverinfo", HTTP_GET, handleDriverInfoGet);
    server.on("/api/v1/switch/0/driverversion", HTTP_GET, handleDriverVersionGet);
    server.on("/api/v1/switch/0/interfaceversion", HTTP_GET, handleInterfaceVersionGet);
    server.on("/api/v1/switch/0/name", HTTP_GET, handleNameGet);
    server.on("/api/v1/switch/0/supportedactions", HTTP_GET, handleSupportedActionsGet);

    //Switch-specific functions - things are named --0-- so you can additional devices later or override the request handler to function against the correct switch hardware set using generic handler set.
    server.on("/api/v1/switch/0/maxswitch", HTTP_GET, handlerDriver0Maxswitch);
    server.on("/api/v1/switch/0/canwrite", HTTP_GET, handlerDriver0CanWrite);
    server.on("/api/v1/switch/0/getswitchdescription", HTTP_GET, handlerDriver0SwitchDescription);
    server.on("/api/v1/switch/0/getswitch", HTTP_GET, handlerDriver0SwitchState);
    server.on("/api/v1/switch/0/setswitch", HTTP_PUT, handlerDriver0SwitchState);
    server.on("/api/v1/switch/0/getswitchname", HTTP_GET, handlerDriver0SwitchName);
    server.on("/api/v1/switch/0/setswitchname", HTTP_PUT, handlerDriver0SwitchName);
    server.on("/api/v1/switch/0/getswitchvalue", HTTP_GET, handlerDriver0SwitchValue);
    server.on("/api/v1/switch/0/setswitchvalue", HTTP_PUT, handlerDriver0SwitchValue);
    server.on("/api/v1/switch/0/minswitchvalue", HTTP_GET, handlerDriver0MinSwitchValue);
    server.on("/api/v1/switch/0/maxswitchvalue", HTTP_GET, handlerDriver0MaxSwitchValue);
    server.on("/api/v1/switch/0/switchstep", HTTP_GET, handlerDriver0SwitchStep);

    //Additional ASCOM ALPACA Management setup calls
    //Per device
    server.on("/setup", HTTP_GET, handlerDeviceSetup);
    server.on("/setup/hostname", HTTP_ANY, handlerDeviceHostname);
    server.on("/setup/udpport", HTTP_ANY, handlerDeviceUdpPort);
    server.on("/setup/source-device", HTTP_POST, handlerSourceDevice);
    server.on("/setup/source-recover", HTTP_POST, handlerSourceRecover);
    server.on("/setup/location", HTTP_ANY, handlerDeviceLocation);
    server.on("/setup/mqtt", HTTP_POST, handlerDeviceMqtt);
    server.on("/setup/numswitches", HTTP_POST, handlerDriver0SetupNumSwitches);
    server.on("/setup/switches", HTTP_POST, handlerDriver0SetupSwitches);

    //Management API
    server.on("/management/apiversions", HTTP_GET,
              /**
               * @brief Serve the supported Alpaca API versions.
               * @details Record eligible owner activity, then delegate the management response.
               * @par Arguments
               * None; request and response use the global HTTP server.
               * @return void; sends the management JSON response.
               */
              []()
              {
                  recordAlpacaReadActivity();
                  handleMgmtVersions();
              });
    server.on("/management/v1/description", HTTP_GET,
              /**
               * @brief Serve controller management identification.
               * @details Record eligible owner activity, then delegate the description response.
               * @par Arguments
               * None; request and response use the global HTTP server.
               * @return void; sends the management JSON response.
               */
              []()
              {
                  recordAlpacaReadActivity();
                  handleMgmtDescription();
              });
    server.on("/management/v1/configureddevices", HTTP_GET,
              /**
               * @brief Serve the configured Alpaca device list.
               * @details Record eligible owner activity, then delegate the configured-device response.
               * @par Arguments
               * None; request and response use the global HTTP server.
               * @return void; sends the management JSON response.
               */
              []()
              {
                  recordAlpacaReadActivity();
                  handleMgmtConfiguredDevices();
              });

    server.on("/setup/v1/switch/0/setup", HTTP_GET,
              handlerDriver0Setup); //Standard Alpaca device setup URL
    server.on("/api/v1/switch/0/setup", HTTP_GET,
              handlerDriver0Setup); //Retain legacy client setup alias.
    server.on("/setup/api/v1/switch/0/setup", HTTP_GET,
              handlerDriver0Setup); //ALPACA driver setup - as called by chooser !CHECK ROUTE
    server.on("/api/v1/switch/0/getswitchtype", HTTP_GET, handlerDriver0SwitchType);
    server.on("/api/v1/switch/0/setswitchtype", HTTP_ANY, handlerDriver0SwitchType);
    server.on("/api/v1/switch/0/numswitches", HTTP_ANY, handlerDriver0SetupNumSwitches);
    server.on("/api/v1/switch/0/switches", HTTP_ANY, handlerDriver0SetupSwitches);

    //Custom
    server.on("/status", HTTP_GET, handlerStatus);
    server.on("/restart", HTTP_ANY, handlerRestart);

    updater.setup(&server);
    server.begin();
    debugI("Web server handlers setup & started");

    //Starts the discovery responder server
    Udp.begin(udpPort);

    //Setup timers
    //setup interrupt-based 'soft' alarm handler for periodic acquisition of new bearing
    ets_timer_setfn(&timer, onTimer, NULL);
    ets_timer_setfn(&timeoutTimer, onTimeoutTimer, NULL);

    //fire timer every 500 msec
    //Set the timer function first
    ets_timer_arm_new(&timer, 500, 1 /*repeat*/, 1);
    //ets_timer_arm_new( &timeoutTimer, 2500, 0/*one-shot*/, 1);

    //Show welcome message
    debugI("Setup complete");

#if !defined DEBUG_ESP_MH && !defined DEBUG_DISABLED
    //turn off serial debug if we are not actively debugging.
    //use telnet access for remote debugging
    Debug.setSerialEnabled(false);
#endif
}

//Function to (re-)setup allocated pins for PWM or DAC use.
//Since relay board pins are driven by I2C expander and the code assumes the relays are mapped 1-2-1 to expander pins
//Need to document that PWM config must come after relay config.
/**
 * @brief Initialize the native sources in the current mapping.
 * @details Visit each configured channel and apply its native input or inactive output configuration.
 * @par Arguments
 * None.
 * @return void; effects are described above.
 */
void setPins(void)
{
    for (int i = 0; i < numSwitches; ++i)
        initializeNativeSource(*switchEntry[i]);
}

//Timer handler
/**
 * @brief Signal the periodic acquisition timer.
 * @details Set the loop flag from the timer callback without performing I/O.
 * @param[in,out] pArg (void *) Unused timer callback context.
 * @return void; effects are described above.
 */
void onTimer(void *pArg)
{
    newDataFlag = true;
}

//Used to complete timeout actions.
/**
 * @brief Signal expiry of the MQTT retry timer.
 * @details Set the retry flag for processing in the main loop.
 * @param[in,out] pArg (void*) Unused timer callback context.
 * @return void; effects are described above.
 */
void onTimeoutTimer(void *pArg)
{
    //Read command list and apply.
    timeoutFlag = true;
}

//Main processing loop
/**
 * @brief Service controller work cooperatively.
 * @details Poll ownership and hardware, apply MQTT endpoint changes, and service network/debug callbacks.
 * @par Arguments
 * None.
 * @return void; effects are described above.
 */
void loop()
{
    //Manage connected timeouts to prevent locking device to one user if user goes away.
    checkAlpacaConnectionTimeout();

    //Check
    serviceMappedSources();

    if (mqttEndpointChanged)
    {
        applyMqttEndpoint(client);
        ets_timer_disarm(&timeoutTimer);
        timerSet = false;
        timeoutFlag = false;
    }
    newDataFlag = false;

    if (client.connected())
    {
        //Service MQTT keep-alives
        client.loop();

        if (callbackFlag)
        {
            //publish results
            //publishStuff();
#if defined USE_ADC
            publishADC();
#endif
            publishHealth();
            callbackFlag = false;
        }
    }
    else
    {
        //reconnect();
        reconnectNB();
        client.subscribe(inTopic); //Seems to be needed here.
    }

    //Handle web requests
    server.handleClient();

    //Check for Discovery packets
    handleManagement();

#if !defined DEBUG_DISABLED
    //Handle remote telnet debug session
    Debug.handle();
#endif
}

/* MQTT callback for subscription and topic.
 * Only respond to valid states ""
 * Publish under ~/skybadger/sensors/<sensor type>/<host>
 * Note that messages have an maximum length limit of 18 bytes - set in the MQTT header file. 
 */
/**
 * @brief Schedule processing after an MQTT message.
 * @details Set the callback flag; the main loop performs publication rather than processing the payload here.
 * @param[in] topic (char*) Received MQTT topic; not interpreted by this callback.
 * @param[in] payload (byte*) Received MQTT bytes; not interpreted by this callback.
 * @param[in] length (unsigned int) Input length in bytes.
 * @return void; effects are described above.
 */
void callback(char *topic, byte *payload, unsigned int length)
{
    //set callback flag
    callbackFlag = true;
}

#if defined USE_ADC
/**
 * @brief Publish mapped analog channel readings to MQTT.
 * @details Resolve each analog channel and serialize a valid measurement or its error with source identity.
 * @par Arguments
 * None.
 * @return void; effects are described above.
 */
void publishADC(void)
{
    String output;
    ASCOM_JSON_OBJECT(root, 512);
    ASCOM_JSON_NESTED_ARRAY(values, root, "voltages");
    root["hostname"] = myHostname;
    String timestamp;
    getTimeAsString2(timestamp);
    root["time"] = timestamp;
    for (int id = 0; id < numSwitches; ++id)
    {
        if (switchEntry[id]->binding.mode != Mode::AnalogInput)
            continue;
        double value;
        const Result result = readMappedValue(id, value);
        ASCOM_JSON_OBJECT(item, 256);
        item["id"] = id;
        item["name"] = switchEntry[id]->switchName;
        item["source"] = sourceKey(switchEntry[id]->binding.source);
        item["valid"] = result == Result::Ok;
        if (result == Result::Ok)
            item["value"] = value;
        else
            item["error"] = sourceResultMessage(result);
        values.add(item);
    }
    ASCOM_JSON_SERIALIZE(root, output);
    const String topic = String(outSenseTopic) + "voltage/" + myHostname;
    client.publish(topic.c_str(), output.c_str(), true);
}
#endif //USE_ADC

/* MQTT function publishes health of device data to topic.  */
/**
 * @brief Publish the controller health announcement.
 * @details Serialize timestamp, hostname and listening state to the configured health topic.
 * @par Arguments
 * None.
 * @return void; effects are described above.
 */
void publishHealth(void)
{
    String outTopic;
    String output;
    String timestamp;

    //checkTime();
    getTimeAsString2(timestamp);

    //publish to our device topic(s)
    ASCOM_JSON_OBJECT(root, 256);
    root["time"] = timestamp;
    root["hostname"] = myHostname;
    root["message"] = "Listening";

    ASCOM_JSON_SERIALIZE(root, output);

    //Put a notice out regarding device health
    outTopic = String(outHealthTopic);
    outTopic.concat(myHostname);
    client.publish(outTopic.c_str(), output.c_str(), true);
    debugI("topic: %s, published with value %s \n", outTopic.c_str(), output.c_str());
}

/**
 * @brief Connect the controller to its configured WiFi network.
 * @details Set station mode and hostname, wait for association, report addresses and configure sleep behavior.
 * @par Arguments
 * None.
 * @return void; effects are described above.
 */
void setupWifi(void)
{
    WiFi.mode(WIFI_STA);
    WiFi.hostname(myHostname);

    WiFi.begin(String(ssid1).c_str(), String(password1).c_str());
    //WiFi.begin( String(ssid2).c_str(), String(password2).c_str() );
    Serial.println("Searching for WiFi..\n");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println(F("WiFi connected"));
    Serial.printf_P(PSTR("SSID: %s, Signal strength %i dBm \n\r"), WiFi.SSID().c_str(),
                    WiFi.RSSI());
    Serial.printf_P(PSTR("Hostname: %s\n\r"), WiFi.hostname().c_str());
    Serial.printf_P(PSTR("IP address: %s\n\r"), WiFi.localIP().toString().c_str());
    Serial.printf_P(PSTR("DNS address 0: %s\n\r"), WiFi.dnsIP(0).toString().c_str());
    Serial.printf_P(PSTR("DNS address 1: %s\n\r"), WiFi.dnsIP(1).toString().c_str());
    delay(500);

    //Setup sleep parameters
    //wifi_set_sleep_type(LIGHT_SLEEP_T);

    //WiFi.mode(WIFI_NONE_SLEEP);
    wifi_set_sleep_type(NONE_SLEEP_T);

    Serial.println(F("WiFi connected"));
    delay(500);
}
