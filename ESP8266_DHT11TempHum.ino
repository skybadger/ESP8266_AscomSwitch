/*
 Program to talk to DHT one-wire temp/hum sensor and send output to MQTT topic /skybadger/device/temperature
 Supports web interface on port 80 returning json string
 To do:
 add Wire interface to BME/P180/280 class device. 
 DHT bit works
 BMP 280 works + BME280 library 
 BMP280 is desireable since it also supports humidity for direct dewpoint measurement nd has greater precision for temperature.
 
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
#include "DHTesp.h"
#include <Wire.h> //https://playground.arduino.cc/Main/WireLibraryDetailedReference
#include <Time.h> //Look at https://github.com/PaulStoffregen/Time for a more useful internal timebase library
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>

//BMP 280 device uses sensor library 
#include "i2c.h"
#include "i2c_BMP280.h"

//Ntp dependencies - available from v2.4
#include <time.h>
#include <sys/time.h>
#include <coredecls.h>
#define TZ              1       // (utc+) TZ in hours
#define DST_MN          60      // use 60mn for summer time in some countries
#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)
time_t now; //use as 'gmtime(&now);'

IPAddress timeServer(193,238,191,249); // pool.ntp.org NTP server
unsigned long NTPseconds; //since 1970

//Strings
const char* myHostname = "espTHP01";
const char* ssid = "";
const char* password = "";
const char* pubsubUserID = "";
const char* pubsubUserPwd = "";
const char* mqtt_server = "obbo";
const char* thisID = "espTHP01";
const char* outHealthTopic = "skybadger/devices/";
const char* outSenseTopic = "skybadger/sensors/";
const char* inTopic = "skybadger/devices/heartbeat";

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];

//Web server data formatter
DynamicJsonBuffer jsonBuffer(256);

// Create an instance of the server
// specify the port to listen on as an argument
ESP8266WebServer server(80);

//Hardware device system functions - reset/restart etc
EspClass device;

//Handler function definitions
void handleRoot();

float humidity = 0.0F;
float temperature = 0.0F;
float pressure = 0.0F;
float dewpoint = 0.0F;
DHTesp dht;
BMP280 bmp280;

void setup_wifi()
{
  //Start NTP client
  configTime(TZ_SEC, DST_SEC, "pool.ntp.org");

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
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void setup()
{
  Serial.begin( 115200, SERIAL_8N1, SERIAL_TX_ONLY);
  Serial.println();
  Serial.println(F("ESP starting."));

  // Connect to wifi 
  setup_wifi();                   
  
  //Open a connection to MQTT
  client.setServer(mqtt_server, 1883);
  client.connect(thisID, pubsubUserID, pubsubUserPwd ); 
  
  //Create a timer-based callback that causes this device to read the local i2C bus devices for data to publish.
  client.setCallback(callback);
  
   //Configure i2c connection to BMP280 pressure monitor
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
  dht.setup(3, DHTesp::DHT11); // Connect DHT sensor to GPIO 3

  Serial.print("Probe BMP280: ");
  if (bmp280.initialize()) Serial.println("Sensor found");
  else
  {
      Serial.println("Sensor missing");
      while (1) {}
  }

  // onetime-measure:
  bmp280.setEnabled(0);
  bmp280.triggerMeasurement();

  //Setup json library
  
  //Setup webserver handler functions
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  
  //Don't need a post handler yet
  //server.onUpload();    
  server.begin();
  
  Serial.println();
  Serial.println("Status\tHumidity (%)\tTemperature (C)");
}

void loop()
{
  delay(dht.getMinimumSamplingPeriod());
  humidity  = dht.getHumidity();
  temperature = dht.getTemperature();

  //Get the pressure and turn into dew point info
  pressure = 103265.0;
    
  //Print debug output to serial
  Serial.print(dht.getStatusString());
  Serial.print("\t");
  Serial.print(humidity, 1);
  Serial.print("\t\t");
  Serial.print(temperature, 1);
  Serial.println("\t\t");

  bmp280.awaitMeasurement();

  float bTemperature;
  bmp280.getTemperature(bTemperature);
  bmp280.getPressure( pressure );
  
  bmp280.triggerMeasurement();

  Serial.print(" Pressure: ");
  Serial.print( pressure );
  Serial.print(" Pa; T: ");
  Serial.print( bTemperature);
  Serial.println(" C");
    
  /*
   * if (!client.connected()) 
  {
    reconnect();
  }
  
  client.loop();
  */
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

  void handleNotFound()
  {
  String message = "URL not understood\n";
  message += "Simple read: http://<this device>\n";
  server.send(404, "text/plain", message);
  }

  void handleRoot()
  {
    int args;
    String timeString = "", message = "";
    int argsFoundMask = 0;

    JsonObject& root = jsonBuffer.createObject();

    root["time"] = getTimeAsString( timeString );
    root["temperature"] = temperature;
    root["pressure"] = pressure;
    root["humidity"] = humidity;
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
  JsonObject& root = jsonBuffer.createObject();

  root["time"] = timestamp;
  root["temperature"] = temperature;
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
  
  root.remove( "humidity/");
  root["pressure"] = pressure;
  root.printTo( output );
  outTopic = outSenseTopic;
  outTopic.concat("pressure/");
  outTopic.concat(myHostname);
  client.publish( outTopic.c_str(), output.c_str() );        
}

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
      /*
      char* topic = (char*) malloc(sizeof(char) * (sizeof(outTopic) + sizeof("/") + sizeof(thisID) + 1) );
      memcpy( topic, outTopic, sizeof(outTopic)-1);
      memcpy( &topic[sizeof(outTopic)], "/", 1);
      memcpy( &topic[sizeof(outTopic)+1], thisID, sizeof( thisID));
      */            
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
