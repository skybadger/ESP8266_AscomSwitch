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
 
Test:
curl -X PUT http://espASW01/api/v1/switch/0/Connected -d "ClientID=0&ClientTransactionID=0&Connected=true" (note cases)
http://espASW01/api/v1/switch/0/status
telnet espASW01 32272 (UDP)
 */
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
#define MAX_TIME_INACTIVE 0 //to turn off the de-activation of a telnet session
#include "RemoteDebug.h"  //https://github.com/JoaoLopesF/RemoteDebug

//Create a remote debug object
RemoteDebug Debug;

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

//Strings
const char* defaultHostname = "espASW00";
char* myHostname = nullptr;

//MQTT settings
char* thisID = nullptr;

WiFiClient espClient;
PubSubClient client(espClient);
volatile bool callbackFlag = false;

// Create an instance of the server
// specify the port to listen on as an argument
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;

//UDP Port can be edited in setup page
int udpPort = ALPACA_DISCOVERY_PORT;
WiFiUDP Udp;

//Hardware device system functions - reset/restart etc
EspClass device;
ETSTimer timer, timeoutTimer;
volatile bool newDataFlag = false;
volatile bool timeoutFlag = false;
volatile bool timerSet = false;

void onTimer(void);
void onTimeoutTimer(void);

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
PCF8574 switchDevice; //top 3 bits set only. 
bool switchPresent = false;
uint32_t switchStatus = 0;

#if defined USE_ADC
#include <Adafruit_ADS1015.h>
// Adafruit_ADS1115 adc;  /* Use this for the 16-bit version */
Adafruit_ADS1015 adc;     /* Use this for the 12-bit version */
bool adcPresent = false;
uint16_t adcReading[4] = {0,0,0,0}; 
float adcScaleFactor[4] = { 22.3/3.3, 10.0/3.3, 5.5/3.3, 1.0 }; //scale factors from the resistor network for each input
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
float adcGainFactor[] = { 0.000125, 0.00025, 0.0005, 0.001, 0.002, 0.003 };
int adcChannel = 0;
int lastChannel = 2;
#endif 

const int eepromSize = 4 + (MAXSWITCH * MAX_NAME_LENGTH * 3) + (2* sizeof(int) )  + (MAXSWITCH * ( sizeof(SwitchEntry) + (2*MAX_NAME_LENGTH) ) ) + 10;

//Order sensitive
#include "Skybadger_common_funcs.h"
#include "JSONHelperFunctions.h"
#include "ASCOMAPICommon_rest.h" //From library/ASCOM_REST - ASCOM common driver descriptors and handlers. Override as required. 
#include "Webrelay_eeprom.h"
#include "ESP8266_relayhandler.h"
#include "AlpacaManagement.h"

void setup_wifi()
{
  int zz = 0;
  WiFi.mode(WIFI_STA); 
  WiFi.hostname( myHostname );
  WiFi.begin( ssid2, password2 );
  Serial.print("Searching for WiFi..");
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
      Serial.print(".");
   if ( zz++ > 200 ) 
    device.restart();
  }

  Serial.println("WiFi connected");
  Serial.printf("SSID: %s, Signal strength %i dBm \n\r", WiFi.SSID().c_str(), WiFi.RSSI() );
  Serial.printf("Hostname: %s\n\r",      WiFi.hostname().c_str() );
  Serial.printf("IP address: %s\n\r",    WiFi.localIP().toString().c_str() );
  Serial.printf("DNS address 0: %s\n\r", WiFi.dnsIP(0).toString().c_str() );
  Serial.printf("DNS address 1: %s\n\r", WiFi.dnsIP(1).toString().c_str() );

  //Setup sleep parameters
  //wifi_set_sleep_type(LIGHT_SLEEP_T);
  wifi_set_sleep_type(NONE_SLEEP_T);

  delay(5000);
  Serial.println( "WiFi connected" );
}

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
  setup_wifi();                   

  //Open a connection to MQTT
  DEBUGSL1("Setting up MQTT."); 
  client.setServer( mqtt_server, 1883 );
  client.connect( thisID, pubsubUserID, pubsubUserPwd ); 
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
  //Wire.begin( 2, 0 );//Normal arrangement
  Wire.begin( 0, 2 );//was 0, 2 for normal arrangement, trying 2, 0 for ASW02
  Wire.setClock(100000 );//100KHz target rate

  //Debugging over telnet setup
  // Initialize the server (telnet or web socket) of RemoteDebug
  //Debug.begin(HOST_NAME, startingDebugLevel );
  Debug.begin( WiFi.hostname().c_str(), Debug.ERROR ); 
  Debug.setSerialEnabled(true);//until set false 
  // Options
  // Debug.setResetCmdEnabled(true); // Enable the reset command
  // Debug.showProfiler(true); // To show profiler - time between messages of Debug
  //In practice still need to use serial commands until debugger is up and running.. 
  debugE("Remote debugger enabled and operating");

////////////////////////////////////////////////////////////////////////////////////////

  String outbuf = scanI2CBus();
  Serial.println( outbuf.c_str() );

////////////////////////////////////////////////////////////////////////////////////////
  
  DEBUGSL1("Setup relay controls");
  switchPresent = false;
  
  //initial switch state setup - set pins high to read inputs, drive pins low for low outputs. Low outputs activate the relays.
  switchDevice.begin( 160, Wire, (const uint8_t) 0xFF );
  error = switchDevice.lastError();
  debugD( "Switch device - lastError is %i", error );
  
  if ( error != PCF8574_OK )
  {
    switchPresent = false;
    debugD( "ASCOMSwitch : Unable to find switch PCF8574 device");
  }
  else
  {
    switchPresent = true;
    //Toggle a relay to indicate we're working
    switchDevice.write(0, 1);delay(1000);
    switchDevice.write(0, 0);delay(1000);
    switchDevice.write(0, 1);
    debugV( "ASCOMSwitch : PCF8574 switch device found\n");
  }
    
  debugI( "ASCOMSwitch : Setting up switches from components \n");
  //Relays use active low - which is the purpose of the reverseRelayLogic flag.
  //If they read as high then they are not activated ...
  for ( int i=0;i< numSwitches ; i++ )
  {
    switch (switchEntry[i]->type)
    {
      case SWITCH_RELAY_NC:
      case SWITCH_RELAY_NO:
        if( reverseRelayLogic ) 
          switchEntry[i]->value = ( switchDevice.read( i ) == 1 )? 0.0F: 1.0F ;
        else
          switchEntry[i]->value = ( switchDevice.read( i ) == 1 )? 1.0F: 0.0F ;
        break;
      case SWITCH_PWM:
      case SWITCH_ANALG_DAC:
        switchEntry[i]->value = switchEntry[i]->value; //TO do - read digital value back from pin ?
        break;
      default:
        break;
    }
  }
  switchStatus = switchDevice.read8();
  debugI( "switchStatus: %i ", switchStatus );

#if defined USE_ADC
  Serial.print("Probe AD1015: ");
  //                                                                ADS1015  ADS1115
  //                                                                -------  -------
  // ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 3mV      0.1875mV (default)
  // ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit = 2mV      0.125mV
  // ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit = 1mV      0.0625mV
  // ads.setGain(GAIN_FOUR);       // 4x gain   +/- 1.024V  1 bit = 0.5mV    0.03125mV
  // ads.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  1 bit = 0.25mV   0.015625mV
  // ads.setGain(GAIN_SIXTEEN);    // 16x gain  +/- 0.256V  1 bit = 0.125mV  0.0078125mV
  adc.begin();
  adc.setGain(GAIN_ONE);
  adcPresent = true;//No function to allow us to sense the ADC. 
#endif 

  //Setup webserver handler functions
  server.on("/", handlerStatus );
  server.onNotFound(handlerNotFound); 
  
  //Common ASCOM handlers
  String preUri = "/api/v1/";
  preUri += ASCOM_DEVICE_TYPE;
  preUri += "/";
  preUri += instanceNumber;
  preUri += "/";

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
  server.on("/api/v1/switch/0/maxswitch",           HTTP_GET, handlerMaxswitch );
  server.on("/api/v1/switch/0/canwrite",            HTTP_GET, handlerCanWrite );
  server.on("/api/v1/switch/0/getswitchdescription", HTTP_GET, handlerSwitchDescription );
  server.on("/api/v1/switch/0/getswitch",           HTTP_GET, handlerSwitchState );
  server.on("/api/v1/switch/0/setswitch",           HTTP_PUT, handlerSwitchState );
  server.on("/api/v1/switch/0/getswitchname",       HTTP_GET, handlerSwitchName );
  server.on("/api/v1/switch/0/setswitchname",       HTTP_PUT, handlerSwitchName );  
  server.on("/api/v1/switch/0/getswitchvalue",      HTTP_GET, handlerSwitchValue );
  server.on("/api/v1/switch/0/setswitchvalue",      HTTP_PUT, handlerSwitchValue );
  server.on("/api/v1/switch/0/minswitchvalue",      HTTP_GET, handlerMinSwitchValue );
  server.on("/api/v1/switch/0/maxswitchvalue",      HTTP_GET, handlerMaxSwitchValue );
  server.on("/api/v1/switch/0/switchstep",          HTTP_GET, handlerSwitchStep );

  //Management API
  server.on("/management/description",              HTTP_GET, handleMgmtDescription );
  server.on("/management/apiversions",              HTTP_GET, handleMgmtVersions );
  server.on("/management/v1/configureddevices",     HTTP_GET, handleMgmtConfiguredDevices );

  //Additional non-ASCOM custom setup calls
  server.on("/setup",                               HTTP_GET, handlerSetup ); // Device setup - includes ASCOM api
  server.on("/api/v1/switch/0/setup",               HTTP_GET, handlerSetup ); //ALPACA device setup - as called by chooser
  server.on("/setup/v1/switch/0/setup",             HTTP_GET, handlerSetup ); //ALPACA ASCOM service setup - as called by chooser
  server.on("/api/v1/switch/0/getswitchtype",       HTTP_GET, handlerSwitchType );
  server.on("/api/v1/switch/0/setswitchtype",       HTTP_PUT, handlerSwitchType );
  server.on("/setup/numswitches" ,                  HTTP_ANY, handlerSetupNumSwitches );
  server.on("/setup/hostname" ,                     HTTP_ANY, handlerSetupHostname );
  server.on("/setup/location" ,                     HTTP_ANY, handlerSetLocation );
  server.on("/setup/switches",                      HTTP_ANY, handlerSetupSwitches );
  
  server.on("/status",                              HTTP_GET, handlerStatus);
  server.on("/restart",                             HTTP_ANY, handlerRestart);

  updater.setup( &server );
  server.begin();
  DEBUGSL1("Web server handlers setup & started");
  
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
  Serial.println( "Setup complete" );
  
  //Redirect all serial to telnet - stops blue led flashing in the dark.  
  Debug.setSerialEnabled(true);
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
  String timestamp;
  String output;
  
  if( newDataFlag == true ) 
  {  
#if defined USE_ADC 
    if ( adcPresent )
    {
      adcReading[0] = adc.readADC_SingleEnded(0); // >12v source
      debugV("AIN0: %i", adcReading[0] );
      adcReading[1] = adc.readADC_SingleEnded(1); //5v regulated supply
      debugV("AIN1: %i", adcReading[1] );      
      adcReading[2] = adc.readADC_SingleEnded(2); //3.3v regulated supply
      debugV("AIN2: %i", adcReading[2] );      
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
    client.subscribe(inTopic);
  }
  
  //Handle web requests
  server.handleClient();

  //Check for Discovery packets
  //handleManagement();

  //Handle remote telnet debug session
  Debug.handle();
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
  
  debugI( "Publish ADC entered, adcPresent:  %i", adcPresent );
  if( adcPresent )
  {
    JsonObject& root = jsonBuffer.createObject();
    output="";//reset

    outTopic = outSenseTopic;
    outTopic.concat("voltage/");
    outTopic.concat(myHostname);

    root["sensor"] = "ADS1015";
    root["time"] = timestamp;
    root["Raw Voltage"] = adcReading[0] * adcScaleFactor[0] * adcGainFactor[4];   

    root.printTo( output );
    //Don't miss the 'true' off or it won't publish - Dunno why, yet. 
    pubState = client.publish( outTopic.c_str(), output.c_str(), true );

    output="";//reset
    root["sensor"] = "ADS1015";
    root["time"] = timestamp;
    root["5v Voltage"] = adcReading[1] * adcScaleFactor[1] * adcGainFactor[4];

    root.printTo( output );
    //Don't miss the 'true' off or it won't publish - Dunno why, yet. 
    pubState = client.publish( outTopic.c_str(), output.c_str(), true );

    output="";//reset
    root["sensor"] = "ADS1015";
    root["time"] = timestamp;
    root["3.3v Voltage"] = adcReading[2] * adcScaleFactor[2] * adcGainFactor[4];

    root.printTo( output );
    //Don't miss the 'true' off or it won't publish - Dunno why, yet. 
    pubState = client.publish( outTopic.c_str(), output.c_str(), true );

/*
    if( !pubState)
      Serial.print( "Failed to publish HTU21D humidity sensor measurement ");    
    else    
      Serial.println( "Published HTU21D humidity sensor measurement ");
    Serial.print( outTopic );Serial.println( output );    
*/
  DEBUGS1( outTopic ); DEBUGS1( " published: "); DEBUGSL1( output );
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
  outTopic = outHealthTopic;
  outTopic.concat( myHostname );
  client.publish( outTopic.c_str(), output.c_str() );  
  Serial.printf( "topic: %s, published with value %s \n", outTopic.c_str(), output.c_str() );
 }
  
