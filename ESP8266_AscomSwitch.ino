/*
 Program to implement ASCOM ALPACA compliant switch interface for remote relays used in power or signalling devices. 
 Typically implemented dusing wireless to talk to the device, and the device uses a PCF8574 I2C serial expander to control an 8 or 16 bit wide data bus
 You can get simple relay boards off the web for ten GBP or so and create a capble to connect the two together.
 Supports web interface on port 80 returning json string

Notes: 
 This design now requires a larger memory size than 1MB for OTA operation and to return the html pages and debug output correctly. 
 Step field is interpreted as number of digital steps in full range from min to max. ie a 10-bit DAC will allow a value of 1024 
 The i2c pin connections need to be reversed netween ASW01 and ASW 02. ASW01 is the normal way around. 
 
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
           
*/
#define ESP8266_01
#define DEBUG_ESP_MH
//Use for client performance testing 
//#define DEBUG_ESP_HTTP_CLIENT
//Manage the remote debug interface, it takes 6K of memory with all the strings 
//#define DEBUG_DISABLED
//#define DEBUG_DISABLE_AUTO_FUNC true
#define WEBSOCKET_DISABLED true           //No impact to memory requirement
#define MAX_TIME_INACTIVE 0 //to turn off the de-activation of a telnet session

#include "RemoteDebug.h"  //https://github.com/JoaoLopesF/RemoteDebug
#include "DebugSerial.h"
#include "SkybadgerStrings.h"
#include "Webrelay_common.h"
#include "AlpacaErrorConsts.h"
#include <esp8266_peri.h> //register map and access
#include <ESP8266WiFi.h>
#include <PubSubClient.h> //https://pubsubclient.knolleary.net/api.html
#include <EEPROM.h>
#include "EEPROMAnything.h"
#include <Wire.h>         //https://playground.arduino.cc/Main/WireLibraryDetailedReference
#include <Time.h>         //Look at https://github.com/PaulStoffregen/Time for a more useful internal timebase library
#include <WiFiUdp.h>      //WiFi UDP discovery responder
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ArduinoJson.h>  //https://arduinojson.org/v5/api/

//Create a remote debug object
#if !defined DEBUG_DISABLED
RemoteDebug Debug;
#endif 

extern "C" { 
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
const char* BuildVersionName PROGMEM = "LWIPv2 lo memory, RDebug enabled \n";
#else
const char* BuildVersionName PROGMEM = "LWIPv2 lo memory, RDebug disabled \n";
#endif 

//If we want to link a switch output to a heater - is this a step too far? 
/*
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

WiFiClient espClient;
PubSubClient client(espClient);
volatile bool callbackFlag = false;

// Create an instance of the server
// specify the port to listen on as an argument
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;

//UDP Port can be edited in setup page
WiFiUDP Udp;

//Hardware device system functions - reset/restart etc
EspClass device;
ETSTimer timer, timeoutTimer;
volatile bool newDataFlag = false;
volatile bool timeoutFlag = false;
volatile bool timerSet = false;

void onTimer(void);
void onTimeoutTimer(void);
void setupWifi( void );
void setPins( void );

//Make these variables rather than constants to allow the custom setup to change them and store them to EEPROM
int numSwitches = 0;
SwitchEntry** switchEntry;

//8-bit port control via I2C Port Expander PCF8574
#include "PCF8574.h"
//- TYPE      ADDRESS-RANGE
//- PCF8574   0x20 to 0x27, 
//  PCF8574A  0x38 to 0x3F
//  TI 8574A is 0x70 to 0x7E, pullups on address pins add to base 0x70
//  Waveshare expander board is address 160
PCF8574 switchDevice( (const uint8_t) 160, &Wire );
bool switchPresent = false;
uint32_t switchStatus = 0;

#if defined USE_ADC
//the purpose of the ADC is to measure the input voltages and report on them for system health purposes. 
//Typically there are four - raw, regulated 12v, regulated 5v and regulated 3.3
//since the raw is regulated by the time it reaches us, we need a dedicated input for that one. 
#include <Adafruit_ADS1015.h>
// Adafruit_ADS1115 adc;  /* Use this for the 16-bit version */
Adafruit_ADS1015 adc;     /* Use this for the 12-bit version */
bool adcPresent = false;
int adcChannelIndex = 0;
int adcChannel = 0;
int lastChannel = 3;
const int adcChannelMax = 4; //Physical max per device is 4, zero-indexed. 
int adcGainSettings[ adcChannelMax ] = { 0,0,0,0, };
uint16_t adcReading[4] = {0,0,0,0}; 

//scale factors from the resistor network for each input
//Switch 01
//const float adcScaleFactor[4] = { 22.3/3.3, 10.0/3.3, 1.0, 1.0 }; 
//switch 02 
const float adcScaleFactor[4] = { 22.3/3.3, 8.2/3.3, 1.2/3.3, 1.0 }; 


// AD0 is single ended 0-25v raw DC input
// AD1 is single ended 0-6v regulated DC input. 
// AD2 is single ended 0-3.3v regulated DC input. 
  //                                                                ADS1015  ADS1115
  //                                                                -------  -------
  // ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 3mV      0.1875mV (default)
  // ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit = 2mV      0.125mV
  // ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit = 1mV      0.0625mV
  // ads.setGain(GAIN_FOUR);       // 4x gain   +/- 1.024V  1 bit = 0.5mV    0.03125mV
  // ads.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  1 bit = 0.25mV   0.015625mV
  // ads.setGain(GAIN_SIXTEEN);    // 16x gain  +/- 0.256V  1 bit = 0.125mV  0.0078125mV
const adsGain_t adcGainConstants[] = {GAIN_TWOTHIRDS, GAIN_ONE, GAIN_TWO, GAIN_FOUR, GAIN_EIGHT, GAIN_SIXTEEN };
const int ADCGainSize = 6;
const float adcGainFactor[] = { 0.003F, 0.002F, 0.001F, 0.0005F, 0.00025F, 0.000125F };
#endif 

const int eepromSize = 4 + (MAXSWITCH * MAX_NAME_LENGTH * 3) + (2* sizeof(int) )  + (MAXSWITCH * ( sizeof(SwitchEntry) + (2*MAX_NAME_LENGTH) ) ) + 10;

//Order sensitive
#include "Skybadger_common_funcs.h"
#include "JSONHelperFunctions.h"
#include "ASCOMAPICommon_rest.h" //From library/ASCOM_REST - ASCOM common driver descriptors and handlers. Override as required. 
#include "Webrelay_eeprom.h"
#include "ESP8266_relayhandler.h"
#include "AlpacaManagement.h"

void setup()
{
  int error = PCF8574_OK;
  Serial.begin( 115200, SERIAL_8N1, SERIAL_TX_ONLY);
  Serial.println(F("ESP starting."));
  
  //Start NTP client
  configTime(TZ_SEC, DST_SEC, timeServer1, timeServer2, timeServer3 );
  delay( 5000);
  
  //Setup default data structures
  DEBUGSL1("Setup EEprom variables"); 
  EEPROM.begin( eepromSize ); 
  //setDefaults();
  setupFromEeprom();
  DEBUGSL1("Setup eeprom variables complete."); 

  // Connect to wifi 
  setupWifi();                   

  //Open a connection to MQTT
  DEBUGSL1("Setting up MQTT."); 
  client.setServer( mqtt_server, 1883 );
  String lastWillTopic = outHealthTopic; 
  lastWillTopic.concat( myHostname );
  client.connect( thisID, pubsubUserID, pubsubUserPwd, lastWillTopic.c_str(), 1, true, "disconnected" ); 
  //Create a timer-based callback that causes this device to read the local i2C bus devices for data to publish.
  client.setCallback( callback );
  client.subscribe( inTopic );
  publishHealth();
    
  //Pins mode and direction setup for i2c on ESP8266-01
  pinMode(0, OUTPUT);
  pinMode(2, OUTPUT);
  //GPIO 3 (normally RX on -01) swap the pin to a GPIO or PWM. 
  pinMode(3, OUTPUT );

  //https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/
  //My normal I2C setup is SDA GPIO 0 on pin2, SCL GPIO 2 on pin 3 on the ESP-01
  //for the small board in the dome switch I had to swap this around.
  //I2C setup SDA pin 5, SCL pin 4 on ESP-12
  //Switch 01
  //Wire.begin( 2, 0 );//Normal arrangement
  //Switch 02 
  Wire.begin( 2, 0 );
  Wire.setClock(100000 );//100KHz target rate

#if !defined DEBUG_DISABLED
  //Debugging over telnet setup
  // Initialize the server (telnet or web socket) of RemoteDebug
  //Debug.begin(HOST_NAME, startingDebugLevel );
  Debug.begin( WiFi.hostname().c_str(), Debug.VERBOSE ); 
  Debug.setSerialEnabled(true);//until set false 
  // Options
  // Debug.setResetCmdEnabled(true); // Enable the reset command
  // Debug.showProfiler(true); // To show profiler - time between messages of Debug
  //In practice still need to use serial commands until debugger is up and running.. 
  DEBUG_ESP("%s\n", "Remote debugger enabled and operating");
#endif //remote debug

  //for use in debugging reset - may need to move 
  DEBUG_ESP( "Device reset reason: %s\n", device.getResetReason().c_str() );
  DEBUG_ESP( "device reset info: %s\n",   device.getResetInfo().c_str() );

////////////////////////////////////////////////////////////////////////////////////////

  String outbuf = scanI2CBus();
  Serial.println( outbuf.c_str() );
  DEBUG_ESP( "I2C scan output: %s\n", outbuf.c_str() );

////////////////////////////////////////////////////////////////////////////////////////
  
  DEBUGSL1("Setup relay controls");
  switchPresent = false;
  
  //initial switch state setup - set pins high to read inputs, drive pins low for low outputs. Low outputs activate the relays.
  //switchDevice.begin( 160, Wire, (const uint8_t) 0xFF );
  switchDevice.begin( 0xFF );
  error = switchDevice.lastError();
  DEBUG_ESP( "Switch device - lastError is %i\n", error );
  
  if ( error != PCF8574_OK )
  {
    switchPresent = false;
    DEBUG_ESP( "%s\n", "Unable to find switch PCF8574 device");
  }
  else
  {
    switchPresent = true;
#if defined DEBUG_ESP_MH
    //Toggle a relay to indicate we're working
    switchDevice.write(0, 1);delay(1000);
    switchDevice.write(0, 0);delay(1000);
    switchDevice.write(0, 1);
#endif    
    DEBUG_ESP( "%s\n", "PCF8574 switch device found");
    DEBUG_ESP( "%s\n", "Setting up switches from components");
    setPins();
    switchStatus = switchDevice.read8();
    DEBUG_ESP( "switchStatus: %i\n", switchStatus );
  }

#if defined USE_ADC
  DEBUG_ESP("Probe AD1015: ");
  //                                                                ADS1015  ADS1115
  //                                                                -------  -------
  // adc.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 3mV      0.1875mV (default)
  // ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit = 2mV      0.125mV
  // ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit = 1mV      0.0625mV
  // ads.setGain(GAIN_FOUR);       // 4x gain   +/- 1.024V  1 bit = 0.5mV    0.03125mV
  // ads.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  1 bit = 0.25mV   0.015625mV
  // ads.setGain(GAIN_SIXTEEN);    // 16x gain  +/- 0.256V  1 bit = 0.125mV  0.0078125mV
  adc.begin();
  adcPresent = true;//No function to allow us to sense the ADC. 
  
  for ( int i = 0; i <= lastChannel && i < adcChannelMax; i ++ )
  {
     //We start from low gains and wide voltage ranges to high gains and small volts. 
     for ( int k = 0; k < ADCGainSize; k++ ) 
     {
        adc.setGain( adcGainConstants[k] );   
        delay(15);
        adcReading[i] = adc.readADC_SingleEnded(adcChannelIndex); 
        if ( adcReading[i] >= 512 && adcReading[i] <= 2046 )
        { 
          adcGainSettings[i] = k;
          DEBUG_ESP("Gain setting for channel %d is %f\n", i, adcGainFactor[k] ) ;
          break;
        }
     }   
  }

#endif 

  //Setup webserver handler functions
  server.on("/", handlerStatus );
  server.onNotFound(handlerNotFound); 
  
  //Common ASCOM handlers

  //Generic ASCOM descriptor functions
  server.on("/api/v1/switch/0/action",              HTTP_PUT, handleAction );
  server.on("/api/v1/switch/0/commandblind",        HTTP_PUT, handleCommandBlind );
  server.on("/api/v1/switch/0/commandbool",         HTTP_PUT, handleCommandBool );
  server.on("/api/v1/switch/0/commandstring",       HTTP_PUT, handleCommandString );
  server.on("/api/v1/switch/0/connected",           handleConnected );
  server.on("/api/v1/switch/0/description",         HTTP_GET, handleDescriptionGet );
  server.on("/api/v1/switch/0/driverinfo",          HTTP_GET, handleDriverInfoGet );
  server.on("/api/v1/switch/0/driverversion",       HTTP_GET, handleDriverVersionGet );
  server.on("/api/v1/switch/0/interfaceversion",    HTTP_GET, handleInterfaceVersionGet );
  server.on("/api/v1/switch/0/name",                HTTP_GET, handleNameGet );
  server.on("/api/v1/switch/0/supportedactions",    HTTP_GET, handleSupportedActionsGet );
   
  //Switch-specific functions
  server.on("/api/v1/switch/0/maxswitch",           HTTP_GET, handlerDriver0Maxswitch );
  server.on("/api/v1/switch/0/canwrite",            HTTP_GET, handlerDriver0CanWrite );
  server.on("/api/v1/switch/0/getswitchdescription", HTTP_GET, handlerDriver0SwitchDescription );
  server.on("/api/v1/switch/0/getswitch",           HTTP_GET, handlerDriver0SwitchState );
  server.on("/api/v1/switch/0/setswitch",           HTTP_PUT, handlerDriver0SwitchState );
  server.on("/api/v1/switch/0/getswitchname",       HTTP_GET, handlerDriver0SwitchName );
  server.on("/api/v1/switch/0/setswitchname",       HTTP_PUT, handlerDriver0SwitchName );  
  server.on("/api/v1/switch/0/getswitchvalue",      HTTP_GET, handlerDriver0SwitchValue );
  server.on("/api/v1/switch/0/setswitchvalue",      HTTP_PUT, handlerDriver0SwitchValue );
  server.on("/api/v1/switch/0/minswitchvalue",      HTTP_GET, handlerDriver0MinSwitchValue );
  server.on("/api/v1/switch/0/maxswitchvalue",      HTTP_GET, handlerDriver0MaxSwitchValue );
  server.on("/api/v1/switch/0/switchstep",          HTTP_GET, handlerDriver0SwitchStep );

//Additional ASCOM ALPACA Management setup calls
  //Per device
  server.on("/setup",                               HTTP_GET, handlerDeviceSetup );
  server.on("/setup/hostname" ,                     HTTP_ANY, handlerDeviceHostname );
  server.on("/setup/udpport",                       HTTP_ANY, handlerDeviceUdpPort );
  server.on("/setup/location",                      HTTP_ANY, handlerDeviceLocation );
  //TODO addd mqtt host and port settings. 
  
  //Management API
  server.on("/management/apiversions",              HTTP_GET, handleMgmtVersions );
  server.on("/management/v1/description",           HTTP_GET, handleMgmtDescription );
  server.on("/management/v1/configureddevices",     HTTP_GET, handleMgmtConfiguredDevices );

  server.on("/api/v1/switch/0/setup",               HTTP_GET, handlerDriver0Setup ); //ALPACA driver setup - as called by chooser
  server.on("/api/v1/switch/0/getswitchtype",       HTTP_GET, handlerDriver0SwitchType );
  server.on("/api/v1/switch/0/setswitchtype",       HTTP_ANY, handlerDriver0SwitchType );
  server.on("/api/v1/switch/0/numswitches" ,        HTTP_ANY, handlerDriver0SetupNumSwitches );
  server.on("/api/v1/switch/0/switches",            HTTP_ANY, handlerDriver0SetupSwitches );
  
  //Custom
  server.on("/status",                              HTTP_GET, handlerStatus);
  server.on("/restart",                             HTTP_ANY, handlerRestart);

  updater.setup( &server );
  server.begin();
  DEBUG_ESP("%s\n", "Web server handlers setup & started" );
  
  //Starts the discovery responder server
  Udp.begin( udpPort);
  
  //Setup timers
  //setup interrupt-based 'soft' alarm handler for periodic acquisition of new bearing
  ets_timer_setfn( &timer, onTimer, NULL ); 
  ets_timer_setfn( &timeoutTimer, onTimeoutTimer, NULL ); 
  
  //fire timer every 500 msec
  //Set the timer function first
  ets_timer_arm_new( &timer, 500, 1/*repeat*/, 1);
  //ets_timer_arm_new( &timeoutTimer, 2500, 0/*one-shot*/, 1);
  
  //Show welcome message
  DEBUG_ESP( "%s\n", "Setup complete" );
  
#if !defined DEBUG_ESP && !defined DEBUG_DISABLED
  //turn off serial debug if we are not actively debugging.
  //use telnet access for remote debugging
   Debug.setSerialEnabled(false); 
#endif
}

//Function to (re-)setup allocated pins for PWM or DAC use. 
//Since relays pins re driven by I2C expander and the code asusmes the relays are mapped 1-2-1 to expander pins 
//Need to document that PWM config must come after relay config. 
void setPins( void )
{
  int i=0;
  for ( i=0; i< numSwitches ; i++ )
  {
    switch (switchEntry[i]->type)
    {
      case SWITCH_PWM:
        //Set the pin modes and set the value in case they are newly chosen
        if ( switchEntry[i]->pin != NULLPIN && switchEntry[i]->pin <= MINPIN && switchEntry[i]->pin <= MAXPIN ) 
          analogWrite( switchEntry[i]->pin, (int) switchEntry[i]->value );
        break;
       
      case SWITCH_ANALG_DAC:
        //Not yet implemented - 
        //Needs an I2C DAC device 
        //switchEntry[i]->value = switchEntry[i]->value; //TO do - read digital value back from pin ?
        break;
      case SWITCH_RELAY_NC:       
      case SWITCH_RELAY_NO:
        //refresh the values and ?? pin modes in case they have changed types
        //If they have been used for analogue output (PWM) and then changed to digital - need to set them to 0 before use. 
        analogWrite( switchEntry[i]->pin, 0);
        //But since we use the I2C expander for the relay controls and not the local pins, might be able to ignore. 
        //Relays use active low - which is the purpose of the reverseRelayLogic flag.
        //If they read as high then they are not activated ...
        if( reverseRelayLogic ) 
        {
          switchDevice.write( i, ! (bool) ( switchEntry[i]->value > 0.0F )  );
          switchEntry[i]->value = ( switchDevice.read( i ) == 1 )? 0.0F: 1.0F ;
        }
        else
        {
          switchDevice.write( i, (bool) ( switchEntry[i]->value > 0.0F ) );
          switchEntry[i]->value = ( switchDevice.read( i ) == 1 )? 1.0F: 0.0F ;
        }
        
      default:
        break;
    }
  }
}

//Timer handler for 'soft' 
void onTimer( void * pArg )
{
  newDataFlag = true;
}

//Used to complete timeout actions. 
void onTimeoutTimer( void* pArg )
{
  //Read command list and apply. 
  timeoutFlag = true;
}

//Main processing loop
void loop()
{
#if defined USE_ADC 
  int adcChannelIndex = 0;
#endif
  
  if( newDataFlag == true ) 
  {  
#if defined USE_ADC 
    //work out what gain to use that brings back the highest valid output
    if ( adcPresent )
    {
      for ( adcChannelIndex = 0; adcChannelIndex <= lastChannel && adcChannelIndex < adcChannelMax; adcChannelIndex++ )
      {
        adc.setGain( adcGainConstants[ adcGainSettings[adcChannelIndex]] );
        delay(15); //per channel
        adcReading[adcChannelIndex] = adc.readADC_SingleEnded(adcChannelIndex);
        DEBUG_ESP( "ADC value[%d]: %d", adcChannelIndex, adcReading[adcChannelIndex] );
        debugV("Raw AIN[%d]: %i\n", adcChannelIndex, adcReading[adcChannelIndex] );
        debugV("Processed AIN scaling: %3.3f, AIN gain:%f\n", adcScaleFactor[adcChannelIndex], adcGainFactor[ adcGainSettings[adcChannelIndex]] );
        debugV("Processed AIN[%d]: %f\n", adcChannelIndex, adcReading[adcChannelIndex] * adcScaleFactor[adcChannelIndex] * adcGainFactor[ adcGainSettings[adcChannelIndex]] );
        yield();       
      }
      debugV( "ratios of data presented: 0:1 %2.3f\n", (float) adcReading[0]/adcReading[1] );
    }
#endif     

    newDataFlag = false;
  }  

  if ( client.connected() )
  {
    //Service MQTT keep-alives
    client.loop();

    if (callbackFlag ) 
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
    client.subscribe(String( inTopic).c_str()) ; //Seems to be needed here. 
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
void callback(char* topic, byte* payload, unsigned int length) 
{  
  //set callback flag
  callbackFlag = true;  
}

#if defined USE_ADC 
 void publishADC( void )
 {
  String outTopic;
  String output;
  String timestamp;
  bool pubState = false;
  
  getTimeAsString2( timestamp );

  //publish to our device topic(s)
  DynamicJsonBuffer jsonBuffer(256);
  
  debugI( "Publish ADC entered, adcPresent:  %i\n", adcPresent );
  if( adcPresent )
  {
    JsonObject& root = jsonBuffer.createObject();
    output="";//reset

    outTopic = String(outSenseTopic); //P_STRING
    outTopic.concat("voltage/");
    outTopic.concat(myHostname);

    for( int i = 0; i<= lastChannel && i < adcChannelMax ; i++ )
    {
      output="";//reset
      root["sensor"] = "ADS1015";
      root["time"] = timestamp;
      root["adcReading"] = adcReading[i];
      root["adcScale"] = adcScaleFactor[i];
      root["adcGain"] = adcGainSettings[i];
      switch (i) 
      {
        case 0: root["12v Voltage"] = adcReading[i] * adcScaleFactor[i] * adcGainFactor[adcGainSettings[i]] ;
          break;
        case 1: root["5v Voltage"] = adcReading[i] * adcScaleFactor[i] * adcGainFactor[adcGainSettings[i]];
          break;
        case 2: root["3v3 Voltage"] = adcReading[i] * adcScaleFactor[i] * adcGainFactor[adcGainSettings[i]];
          break;
        case 3: root["Raw Voltage"] = adcReading[i] * adcScaleFactor[i] * adcGainFactor[adcGainSettings[i]]; 
          break;
        default: 
          break; 
      }
      root.printTo( output );
      pubState = client.publish( outTopic.c_str(), output.c_str(), true );
      debugI( "topic : %s published with values: %s\n", outTopic.c_str(),  output.c_str() );
    }
   }
 }
#endif //USE_ADC

/*
 * Had to do a lot of work to get this to work 
 * Mostly around - 
 * length of output buffer
 * reset of output buffer between writing json strings otherwise it concatenates. 
 * Writing to serial output was essential.
 */
 void publishHealth( void )
 {
  String outTopic;
  String output;
  String timestamp;
  
  //checkTime();
  getTimeAsString2( timestamp );

  //publish to our device topic(s)
  DynamicJsonBuffer jsonBuffer(256);
  JsonObject& root = jsonBuffer.createObject();
  root["time"] = timestamp;
  root["hostname"] = myHostname;
  root["message"] = "Listening";
  
  root.printTo( output);
  
  //Put a notice out regarding device health
  outTopic = String( outHealthTopic );
  outTopic.concat( myHostname );
  client.publish( outTopic.c_str(), output.c_str(), true );  
  debugI( "topic: %s, published with value %s \n", outTopic.c_str(), output.c_str() );
 }

void setupWifi( void )
{
  WiFi.hostname( myHostname );  
  WiFi.mode(WIFI_STA);
  WiFi.hostname( myHostname );  

  WiFi.begin( String(ssid1).c_str(), String(password1).c_str() );
  Serial.println( "Searching for WiFi..\n" );
  
  while (WiFi.status() != WL_CONNECTED) 
  {
     delay(500);
     Serial.print(".");
  }

  Serial.println( F("WiFi connected") );
  Serial.printf_P( PSTR("SSID: %s, Signal strength %i dBm \n\r"), WiFi.SSID().c_str(), WiFi.RSSI() );
  Serial.printf_P( PSTR("Hostname: %s\n\r"),       WiFi.hostname().c_str() );
  Serial.printf_P( PSTR("IP address: %s\n\r"),     WiFi.localIP().toString().c_str() );
  Serial.printf_P( PSTR("DNS address 0: %s\n\r"),  WiFi.dnsIP(0).toString().c_str() );
  Serial.printf_P( PSTR("DNS address 1: %s\n\r"),  WiFi.dnsIP(1).toString().c_str() );
  delay(500);

  //Setup sleep parameters
  //wifi_set_sleep_type(LIGHT_SLEEP_T);

  //WiFi.mode(WIFI_NONE_SLEEP);
  wifi_set_sleep_type(NONE_SLEEP_T);

  Serial.println( F("WiFi connected" ) );
  delay(500);
}
