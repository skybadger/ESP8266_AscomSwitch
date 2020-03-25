/*
 Program to implement ASCOM ALPACA compliant switch interface for remote relays used in power or signalling devices. 
 Typically implemented dusing wireless to talk to the device, and the device uses a PCF8574 I2C serial expander to control an 8 or 16 bit wide data bus
 You can get simple relay boards off the web for ten GBP or so and create a capble to connect the two together.
 Supports web interface on port 80 returning json string
 
 To do:
 Complete EEPROM calls
 Add support for initial state settings on setup page and in eeprom.
 Complete Setup page
 Complete support for device-based PWM. 
 Complete support for DAC output
  
 Done: 
 PCF8574 library added to support switches - needs physical integration testing.
 Add suport for PWM hardware chip(s).
  
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
const char* defaultHostname = "espASW01";
char* myHostname = nullptr;

//MQTT settings
char* thisID = nullptr;

WiFiClient espClient;
PubSubClient client(espClient);
volatile bool callbackFlag = 0;

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

//Dome shutter control via I2C Port Expander PCF8574
#include "PCF8574.h"
//- TYPE      ADDRESS-RANGE
//- PCF8574   0x20 to 0x27, 
//  PCF8574A  0x38 to 0x3F
//  TI 8574A is 0x70 to 0x7E, pullups on address pins add to base 0x70
//  Waveshare expander board is address 160
PCF8574 switchDevice( 160, Wire );
bool switchPresent = false;
uint32_t switchStatus = 0;

#include "Skybadger_common_funcs.h"
#include "JSONHelperFunctions.h"
#include "ASCOMAPICommon_rest.h" //ASCOM common driver web handlers. 
#include "Webrelay_eeprom.h"
#include "ESP8266_relayhandler.h"

void setup_wifi()
{
  int zz = 0;
  WiFi.hostname( myHostname );
  WiFi.mode(WIFI_STA); 
  WiFi.begin(ssid1, password1);
  Serial.print("Searching for WiFi..");
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
      Serial.print(".");
   if ( zz++ > 200 ) 
    device.restart();
  }

  Serial.println("WiFi connected");
  Serial.printf("Hostname: %s\n\r",      WiFi.hostname().c_str() );
  Serial.printf("IP address: %s\n\r",    WiFi.localIP().toString().c_str() );
  Serial.printf("DNS address 0: %s\n\r", WiFi.dnsIP(0).toString().c_str() );
  Serial.printf("DNS address 1: %s\n\r", WiFi.dnsIP(1).toString().c_str() );

  //Setup sleep parameters
  wifi_set_sleep_type(LIGHT_SLEEP_T);

  delay(5000);
  Serial.println( "WiFi connected" );
}

void setup()
{
  Serial.begin( 115200, SERIAL_8N1, SERIAL_TX_ONLY);
  Serial.println(F("ESP starting."));
  
  //Start NTP client
  configTime(TZ_SEC, DST_SEC, timeServer1, timeServer2, timeServer3 );
  
  //Setup default data structures
  Serial.println("Setup EEprom variables"); 
  setupFromEeprom();
  Serial.println("Setup eeprom variables complete."); 
  
  // Connect to wifi 
  setup_wifi();                   
  
  //Open a connection to MQTT
  client.setServer( mqtt_server, 1883 );
  client.connect( thisID, pubsubUserID, pubsubUserPwd ); 
  //Create a timer-based callback that causes this device to read the local i2C bus devices for data to publish.
  client.setCallback( callback );
  client.subscribe( inTopic );
  publishHealth();
    
  //Pins mode and direction setup for i2c on ESP8266-01
  pinMode(0, OUTPUT);
  pinMode(2, OUTPUT);
  //GPIO 3 (normally RX on -01) swap the pin to a GPIO. Use it for the DHT 
  pinMode(3, OUTPUT);
  
  //pinMode(12, INPUT_PULLUP); disable for DHT  - let DHT class address
  
  //I2C setup SDA pin 0, SCL pin 2 on ESP-01
  //I2C setup SDA pin 5, SCL pin 4 on ESP-12
  Wire.begin(0, 2);
  Wire.setClock(50000 );//100KHz target rate
  
  Serial.println("Setup relay controls");
  switchPresent = false;
  
  //initial switch state setup - set pins high to read inputs, drive pins low for low outputs. 
  switchDevice.begin( (const uint8_t) 0b11111111 );
  switchPresent = ( switchDevice.lastError() != PCF8574_OK );
  if ( !switchPresent )
  {
    Serial.printf( "ASCOMSwitch : Unable to find PCF8574 switch device \n");
    String msg = scanI2CBus();
    Serial.println( msg );
  }
  else
  {
    switchDevice.write(0, 1);delay(1000);
    switchDevice.write(0, 0);delay(1000);
    switchDevice.write(0, 1);
  }
    
  for ( int i=0;i< numSwitches ; i++ )
  {
    switch (switchEntry[i]->type)
    {
      case SWITCH_RELAY_NC:
      case SWITCH_RELAY_NO:
        switchEntry[i]->value = ( switchDevice.read( i ) == 1 )? 1.0F: 0.0F ;
        break;
      case SWITCH_PWM:
      case SWITCH_ANALG_DAC:
        switchEntry[i]->value = switchEntry[i]->value; //TO do - read digital value back from pin
        break;
      default:
        break;
    }
  }
  switchStatus  = switchDevice.read8();
  DEBUGS1( "switchStatus: "); DEBUGSL1( switchStatus );

  //Setup webserver handler functions
  server.on("/", handlerStatus );
  server.onNotFound(handlerNotFound); 
  
  //Common ASCOM handlers
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
  server.on("/api/v1/switch/0/getswitchtype",       HTTP_GET, handlerSwitchType );
  server.on("/api/v1/switch/0/setswitchtype",       HTTP_PUT, handlerSwitchType );
  server.on("/api/v1/switch/0/getswitchvalue",      HTTP_GET, handlerSwitchValue );
  server.on("/api/v1/switch/0/setswitchvalue",      HTTP_PUT, handlerSwitchValue );
  server.on("/api/v1/switch/0/minswitchvalue",      HTTP_GET, handlerMinSwitchValue );
  server.on("/api/v1/switch/0/maxswitchvalue",      HTTP_GET, handlerMaxSwitchValue );
  server.on("/api/v1/switch/0/switchstep",          HTTP_GET, handlerSwitchStep );

//Additional non-ASCOM custom setup calls
  server.on("/status",                              HTTP_GET, handlerStatus);
  server.on("/api/v1/switch/0/status",              HTTP_ANY, handlerStatus );
  server.on("/api/v1/switch/0/setup",               HTTP_ANY, handlerSetup );
  server.on("/api/v1/switch/0/setup",               HTTP_ANY, handlerSetup );
  server.on("/api/v1/switch/0/setupSwitches",       HTTP_ANY, handlerSetupSwitches );
  updater.setup( &server );
  server.begin();
  
  //Starts the discovery responder server
  Udp.begin( udpPort);
  
  //Setup timers
  //setup interrupt-based 'soft' alarm handler for periodic acquisition of new bearing
  ets_timer_setfn( &timer, onTimer, NULL ); 
  ets_timer_setfn( &timeoutTimer, onTimeoutTimer, NULL ); 
  
  //fire timer every 250 msec
  //Set the timer function first
  ets_timer_arm_new( &timer, 250, 1/*repeat*/, 1);
  //ets_timer_arm_new( &timeoutTimer, 2500, 0/*one-shot*/, 1);
  
  Serial.println( "Setup complete" );
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
  
  DynamicJsonBuffer jsonBuffer(256);
  JsonObject& root = jsonBuffer.createObject();

  if( WiFi.status() != WL_CONNECTED )
    device.restart(); 
  
  int udpBytesIn = Udp.parsePacket();
  if( udpBytesIn > 0  ) 
    handleDiscovery( udpBytesIn );
    
  if( newDataFlag == true ) 
  {
    root["time"] = getTimeAsString( timestamp );
    //Serial.println( getTimeAsString( timestamp ) )
  
    newDataFlag = false;
  }  

  if( client.connected() )
  {
    if (callbackFlag == true )
    {
      //publish results
      publishHealth();
      callbackFlag = false;
    }
    client.loop();
  }
  else
  {
    reconnectNB();
    client.subscribe (inTopic);
  }
  
  //Handle web requests
  server.handleClient();
 
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
  root["Time"] = timestamp;
  root["Message"] = "Listening";
  root.printTo( output);
  
  //Put a notice out regarding device health
  outTopic = outHealthTopic;
  outTopic.concat( myHostname );
  client.publish( outTopic.c_str(), output.c_str() );  
  Serial.printf( "topic: %s, published with value %s \n", outTopic.c_str(), output.c_str() );
 }
 
 void handleDiscovery( int udpBytesCount )
 {
    char inBytes[64];
    String message;
    DiscoveryPacket discoveryPacket;
 
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    
    Serial.printf("UDP: %i bytes received from %s:%i\n", udpBytesCount, Udp.remoteIP().toString().c_str(), Udp.remotePort() );

    // We've received a packet, read the data from it
    Udp.read( inBytes, udpBytesCount); // read the packet into the buffer

    // display the packet contents
    for (int i = 0; i < udpBytesCount; i++ )
    {
      Serial.print( inBytes[i]);
      if (i % 32 == 0)
      {
        Serial.println();
      }
      else Serial.print(' ');
    } // end for
    Serial.println();
   
    //Is it for us ?
    char protocol[16];
    strncpy( protocol, (char*) inBytes, 16);
    if ( strcasecmp( discoveryPacket.protocol, protocol ) == 0 )
    {
      Udp.beginPacket( Udp.remoteIP(), Udp.remotePort() );
      //Respond with discovery message
      root["IPAddress"] = WiFi.localIP().toString().c_str();
      root["Type"] = DriverType;
      root["AlpacaPort"] = 80;
      root["Name"] = WiFi.hostname();
      root["UniqueID"] = system_get_chip_id();
      root.printTo( message );
      Udp.write( message.c_str(), sizeof( message.c_str() ) * sizeof(char) );
      Udp.endPacket();   
    }
 }
 
