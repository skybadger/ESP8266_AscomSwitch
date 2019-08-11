/*
 Program to talk to DHT one-wire temp/hum sensor and send output to MQTT topic /skybadger/device/temperature
 Supports web interface on port 80 returning json string
 To do:
 add Wire interface to BME/P180/280 class device. 
 DHT bit works
 BMP 280 works + BME280 library 
 BMP280 is desirable since it also supports humidity for direct dewpoint measurement and has greater precision for temperature.
 QMC5883L 
 
 Layout:
 Pin 13 to DHT data 
 GPIO 4,2 to SDA
 GPIO 5,0 to SCL 
 All 3.3v logic. 
  
Tested working prior to adding HMC5883L 
Todo - remove the temp sensor reboot on not detected - that way can add after power cycle.
Todo - add HMC5883L library
 */

#include <esp8266_peri.h> //register map and access
#include <ESP8266WiFi.h>
#include <PubSubClient.h> //https://pubsubclient.knolleary.net/api.html
#include <EEPROM.h>
#include <Wire.h>         //https://playground.arduino.cc/Main/WireLibraryDetailedReference
#include <Time.h>         //Look at https://github.com/PaulStoffregen/Time for a more useful internal timebase library
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>

#include "DHTesp.h"

//QMC5883 device library 
#include <QMC5883L.h>

//BMP 280 device uses sensor library 
#include "i2c.h"
#include "i2c_BMP280.h"

//Ntp dependencies - available from v2.4
#include <time.h>
#include <sys/time.h>
#include <coredecls.h>
#define TZ              0       // (utc+) TZ in hours
#define DST_MN          60      // use 60mn for summer time in some countries
#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)
time_t now; //use as 'gmtime(&now);'

IPAddress timeServer(193,238,191,249); // pool.ntp.org NTP server
unsigned long NTPseconds; //since 1970

//Strings
const char* myHostname =      "espTHP01";
const char* ssid =            "BadgerHome";
const char* password =        "This is Badger Home";
//const char* ssid =            "Skybadger_Away";
//const char* password =        "This is Skybadger Away";

const char* pubsubUserID =    "Skybadger";
const char* pubsubUserPwd =   "alltooeasy1";
const char* mqtt_server =     "Y-BADGER.i-badger.co.uk";
const char* thisID =          "espTHP01";
const char* outHealthTopic =  "/skybadger/devices/";
const char* outSenseTopic =   "/skybadger/sensors/";
const char* inTopic =         "/skybadger/devices/heartbeat";

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];

// Create an instance of the server
// specify the port to listen on as an argument
ESP8266WebServer server(80);

//Hardware device system functions - reset/restart etc
EspClass device;
ETSTimer timer;
volatile bool newDataFlag = false;

void onTimer(void);
String& getTimeAsString(String& );

//Handler function definitions
void handleRoot();

DHTesp dht;
BMP280 bmp280;
QMC5883L compass;
int error = 0;
Vector raw; 

bool compassPresent = false;
bool dht11Present = false;
bool bmp280Present = false;

//Last data points
float bearing = 0.0f;
float humidity = 0.0F;
float pressure = 0.0F;
float dewpoint = 0.0F;
float aTemperature = 0.0f;
float bTemperature = 0.0f;
float cTemperature = 0.0f;

void setup_wifi()
{
  IPAddress googleDNS(8,8,8,8);
  IPAddress badgerDNS(192,168,0,48);
  IPAddress gatewayDNS(192,168,0,1);

  WiFi.hostname( myHostname );
  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  Serial.print("Searching for WiFi..");
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
      Serial.print(".");
  }

  //WiFi.setDNS( badgerDNS, gatewayDNS );
  Serial.println("WiFi connected");
  Serial.print("Hostname: ");
  Serial.println( myHostname );
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("DNS address 0: ");WiFi.dnsIP(0).printTo(Serial);Serial.println("");
  Serial.println("DNS address 1: ");WiFi.dnsIP(1).printTo(Serial);Serial.println("");
}

void setup()
{
  Serial.begin( 115200, SERIAL_8N1, SERIAL_TX_ONLY);
  Serial.println(F("ESP starting."));
  
  //Start NTP client
  configTime(TZ_SEC, DST_SEC, "pool.ntp.org");
  
  //Setup timestruct
  now = time(nullptr);

  // Connect to wifi 
  setup_wifi();                   
  
  //Open a connection to MQTT
  client.setServer( mqtt_server, 1883 );
  client.connect( thisID, pubsubUserID, pubsubUserPwd ); 
  //Create a timer-based callback that causes this device to read the local i2C bus devices for data to publish.
  client.setCallback( callback );
  client.subscribe( inTopic );
  
  //Pins mode and direction setup for i2c on ESP8266-01
  pinMode(0, OUTPUT);
  pinMode(2, OUTPUT);
  //GPIO 3 (RX) swap the pin to a GPIO. Use it for the DHT 
  pinMode(3, OUTPUT);
  
  //pinMode(12, INPUT_PULLUP); disable for DHT  - let DHT class address
  
  //I2C setup SDA pin 0, SCL pin 2 on ESP-01
  //I2C setup SDA pin 5, SCL pin 4 on ESP-12
  Wire.begin(0, 2);
  Wire.setClock(100000 );//100KHz target rate

  // Autodetect is not working reliable, don't use the following line
  // dht.setup(17);
  // use this instead: 
  //dht.setup(3, DHTesp::DHT11); // Connect DHT sensor to GPIO 3

  Serial.print("Probe BMP280: ");
  //bmp280Present = bmp280.initialize();
  if ( !bmp280Present ) 
  {
    Serial.println("BMP280 Sensor missing");
  }
  else
  {
    Serial.println("BMP280 Sensor found");
    // onetime-measure:
    bmp280.setEnabled(0);
    bmp280.triggerMeasurement();
  }
  
  Serial.println("Setup compass");
  //compassPresent = compass.begin();
  //if( !compassPresent ) // If there is an error, print it out.
  //   Serial.println( "Compass not found");

  //Setup webserver handler functions
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound); 
  server.begin();
  
  //Setup timers
  //setup interrupt-based 'soft' alarm handler for periodic acquisition of new bearing
  ets_timer_setfn( &timer, onTimer, NULL ); 
  
  //fire timer every 250 msec
  //Set the timer function first
  ets_timer_arm_new( &timer, 250, 1/*repeat*/, 1);

  //Setup sleep parameters
  //wifi_set_sleep_type(LIGHT_SLEEP_T);

  Serial.println( "Setup complete" );
}

//Timer handler for 'soft' 
void onTimer( void * pArg )
{
  newDataFlag = true;
}

//Main processing loop
void loop()
{
  String timestamp;
  String output;
  static int loopCount = 0;
  
  DynamicJsonBuffer jsonBuffer(256);
  JsonObject& root = jsonBuffer.createObject();

  if( newDataFlag == true ) 
  {
    root["time"] = getTimeAsString( timestamp );
    //Serial.println( getTimeAsString( timestamp ) );

#ifndef _TESTING    
    //delay(dht.getMinimumSamplingPeriod());
    if( dht11Present && (loopCount++ > 4) )
    {
      humidity  = dht.getHumidity();
      aTemperature = dht.getTemperature();
      dewpoint = dht.computeDewPoint( aTemperature, humidity, false );
      loopCount = 0;
    }
    
    //Get the pressure and turn into dew point info
    if( bmp280Present )
    {
      bmp280.awaitMeasurement();
      bmp280.getTemperature( bTemperature );
      bmp280.getPressure( pressure );   
      bmp280.triggerMeasurement();
    }
    else
    {
      pressure = 103265.0;    
    }
    
    //generate output records
    if( compassPresent) 
    {
      // Retrieve the raw values from the compass (not scaled).
      raw = compass.readRaw();  
      bearing = 180.0/M_PI * atan2( raw.YAxis, raw.XAxis);
      bearing = ( bearing < 0.0F ) ? bearing+360.0F: bearing;
      cTemperature = compass.getTemperature();
    }
#else
    Serial.println(".");
#endif
    newDataFlag = false;
  }  

  server.handleClient();
 
  if (!client.connected()) 
  {
    reconnect();
  }
  else 
    client.loop();

}

  void handleNotFound()
  {
  String message = "URL not understood\n";
  message.concat( "Simple read: http://");
  message.concat( myHostname );
  message.concat ( "\n");
  server.send(404, "text/plain", message);
  }

  void handleRoot()
  {
    int args;
    String timeString = "", message = "";
    int argsFoundMask = 0;
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();

    root["time"] = getTimeAsString( timeString );
    if( dht11Present )
    {
      root["Humidity"] = humidity;
      root["Dewpoint"] = dewpoint;
    }

    if( bmp280Present )  
    {
      root["pressure"] = pressure;
    }
    
    if ( compassPresent )
    {
      JsonArray& mags = root.createNestedArray("Magnetic Fields");
      mags.add( raw.XAxis );
      mags.add( raw.YAxis );
      mags.add( raw.ZAxis );
      root["Bearing"] = bearing;
    }
    
    JsonArray& temps = root.createNestedArray("temperatures");
    temps.add( aTemperature );
    temps.add( bTemperature );
    temps.add( cTemperature );

    //root.printTo( Serial );
    root.printTo(message);
    server.send(200, "application/json", message);
  }

/* MQTT callback for subscription and topic.
 * Only respond to valid states ""
 */
void callback(char* topic, byte* payload, unsigned int length) 
{
  String output;
  String outTopic;
  String timestamp = "";
   
  //checkTime();
  getTimeAsString( timestamp );

  //publish to our device topic(s)
  DynamicJsonBuffer jsonBuffer(256);
  JsonObject& root = jsonBuffer.createObject();

#ifndef _TESTING
  if( dht11Present)
  {
    root["temperature"] = aTemperature;
    root.printTo( output );
    outTopic = outSenseTopic;
    outTopic.concat("temperature/");
    outTopic.concat(myHostname);
    client.publish( outTopic.c_str(), output.c_str() );

    root.remove( "temperature");
    root["humidity"] = humidity;
    root.printTo( output );
    outTopic = outSenseTopic;
    outTopic.concat("humidity/");
    outTopic.concat(myHostname);
    client.publish( outTopic.c_str() , output.c_str() );
  }
  
  if( bmp280Present)
  {
    root.remove( "humidity/");
    root["pressure"] = pressure;
    root["time"] = timestamp;
    root.printTo( output );
    outTopic.concat(myHostname);
    client.publish( outTopic.c_str(), output.c_str() );        
    
    outTopic = outSenseTopic;
    outTopic.concat("temperature/");
    root["time"] = timestamp;
    root.printTo( output );
    outTopic.concat(myHostname);
    client.publish( outTopic.c_str(), output.c_str() );        
  }

  if( compassPresent )
  {
    root.remove( "humidity/");
    root["Bx"] = raw.XAxis;
    root["By"] = raw.YAxis;    
    root["Bz"] = raw.ZAxis;    
    root["time"] = timestamp;
    root.printTo( output );
    outTopic = outSenseTopic;
    outTopic.concat("magneticField/");
    outTopic.concat(myHostname);
    client.publish( outTopic.c_str(), output.c_str() );           
  }
#endif
  
  root["time"] = timestamp;
  output = "Measurement published";
  outTopic = outHealthTopic;
  outTopic.concat( myHostname );
  client.publish( outTopic.c_str(), output.c_str() );  
  Serial.print( outTopic );Serial.print( " published: " ); Serial.println( output );

}

/*
Returns the current state of the client. If a connection attempt fails, this can be used to get more information about the failure.
int - the client state, which can take the following values (constants defined in PubSubClient.h):
-4 : MQTT_CONNECTION_TIMEOUT - the server didn't respond within the keepalive time
-3 : MQTT_CONNECTION_LOST - the network connection was broken
-2 : MQTT_CONNECT_FAILED - the network connection failed
-1 : MQTT_DISCONNECTED - the client is disconnected cleanly
0 : MQTT_CONNECTED - the client is connected
1 : MQTT_CONNECT_BAD_PROTOCOL - the server doesn't support the requested version of MQTT
2 : MQTT_CONNECT_BAD_CLIENT_ID - the server rejected the client identifier
3 : MQTT_CONNECT_UNAVAILABLE - the server was unable to accept the connection
4 : MQTT_CONNECT_BAD_CREDENTIALS - the username/password were rejected
5 : MQTT_CONNECT_UNAUTHORIZED - the client was not authorized to connect
*/

void reconnect() 
{
  String output;
  // Loop until we're reconnected
  while (!client.connected()) 
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(thisID, pubsubUserID, pubsubUserPwd )) 
    {
      Serial.println("connected");
      // Once connected, publish an announcement...
      output = (String)myHostname;
      output.concat( " connected" );
      client.publish( outHealthTopic, output.c_str() );
      // ... and resubscribe
      client.subscribe(inTopic);
    }
    else
    {
     // Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      for(int i = 0; i<5000; i++)
      {
        delay(1);
        //delay(20);
        //yield();
      }
    }
  }
}

String& getTimeAsString(String& output)
{
    //get time, maintained by NTP
    now = time(nullptr);
    struct tm* gnow = gmtime( &now );
    output += String(gnow->tm_year + 1900) + ":" + \
                   String(gnow->tm_mon) + ":" + \
                   String(gnow->tm_mday) + ":" + \
                   String(gnow->tm_hour) + ":" + \
                   String(gnow->tm_min) + ":" + \
                   String(gnow->tm_sec);
    return output;
}
