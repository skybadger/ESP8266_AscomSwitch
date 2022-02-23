/*
ASCOM project common variables
*/
#ifndef _WEBRELAY_COMMON_H_
#define _WEBRELAY_COMMON_H_

#define _REVERSE_RELAY_LOGIC_
#if defined _REVERSE_RELAY_LOGIC_
const bool reverseRelayLogic = true;
#else
const bool reverseRelayLogic = false;
#endif

//Turn on or off use of the ADC for voltage monitoring
//#define USE_ADC

//ASCOM driver common descriptor variables 
unsigned int transactionId;
unsigned int connectedClient;
int connectionCtr = 0; //variable to count number of times something has connected compared to disconnected. 
bool connected = false;
#define ALPACA_DISCOVERY_PORT 32227
int udpPort = ALPACA_DISCOVERY_PORT;
static const char* PROGMEM DriverName = "Skybadger.ESPSwitch";
static const char* PROGMEM DriverVersion = "0.0.1";
static const char* PROGMEM DriverInfo = "Skybadger.ESPSwitch RESTful native device. ";
static const char* PROGMEM Description = "Skybadger ESP2866-based wireless ASCOM switch device";
static const char* PROGMEM InterfaceVersion = "2";
static const char* PROGMEM DriverType = "Switch";
//espasw00 GUID - "0010-0000-0000-0000"; //prototype & demo
//espasw01 GUID - "0010-0000-0000-0001";  //Pier Switch
//espasw02 GUID - "0010-0000-0000-0002";  //Dome switch
static const char* GUID PROGMEM = "0010-0000-0000-0000";

#define ASCOM_DEVICE_TYPE "switch" //used in server handler uris
//Instance defines which driver of the type it is at this IP address. Its almost always 0 unless you are running more than one switch, cover or filter/focuser 
//from a single device
const int defaultInstanceNumber = 0;
int instanceNumber = defaultInstanceNumber;
String preUri = "/api/v1/" + String( ASCOM_DEVICE_TYPE ) + "/" + String( instanceNumber) + "/";

//Mgmt Api Constants
const int instanceVersion = 3; //the iteration version identifier for this driver. Update every major change - relate to your repo versioning
char* Location = nullptr;

#define TZ              0       // (utc+) TZ in hours
#define DST_MN          00      // use 60mn for summer time in some countries
#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)

const String switchTypes[] = {"Relay_NO", "Relay_NC", "PWM", "DAC","Not Selected"};
const int switchTypesLen = 5;
enum SwitchType { SWITCH_RELAY_NO, SWITCH_RELAY_NC, SWITCH_PWM, SWITCH_ANALG_DAC, SWITCH_NOT_SELECTED };

/*
 Typical values for PWM And ADC are 0 - 1024/1024, PWM in terms of fraction of the wave is high 
 and DAC in terms of the output voltage as a fraction of Vcc. 
 */

typedef struct 
{
  char* description = nullptr;
  char* switchName = nullptr;
  enum SwitchType type = SWITCH_RELAY_NO;
  int pin = -1; //Use for DAC and PWM outputs
  bool writeable = true;       //Flag - can a user change this after initial setup 
  float min = 0.0;
  float max = 1.0;
  float step = 1.0;
  float value = 0.0F;
} SwitchEntry;

#define DEFAULT_NUM_SWITCHES 4;
const int defaultNumSwitches = DEFAULT_NUM_SWITCHES;
//Define the maximum number of switches supported - limited by memory really 
const int MAXSWITCH = 16;
const int MAX_NAME_LENGTH = 40;

//define the max resolution available to control a DAC or PWM
const int MAX_DIGITAL_STEPS = 1024; //Limited by PWM resolution
//Value limits
const float MAXDIGITALVAL = 1024.0F; //ie 12 bit PWM and DAC resolution supported.  Vcc assumed max range.
const float MAXBINARYVAL = 1.0F;      // true or false ? open or closed settings on relay. 
const float MINVAL = 0.0F;

//Pin limits
const int NULLPIN = 0; 
#if defined ESP8266-01
const int MINPIN = 0; //device specific
const int MAXPIN = 3; //device specific

//GPIO 0 is Serial tx, GPIO 2 I2C SDA, GPIO 1 SCL, so it depends on whether you use i2c or not.
//Cos that just leaves Rx at GPIO3 and external power circuitry is required. 
//In the case of the switch device as a dew heater controller, use 2 switch and 1 pwm pin to control all heater outputs.  
//This means GPIO 3 needs connecting to both switching outputs. 
const int pinMap[] = { 0, 1, 2, 3, NULLPIN }; 
#elif defined ESP8266-12
const int MINPIN = 0; //device specific
const int MAXPIN = 16; //device specific

//Most pins are used for flash, so we assume those for SSI are available.
//Typically use 4 and 5 for I2C, leaves 
const int pinMap[] = { 2, 14, 12, 13, 15, NULLPIN };
#else
const int pinMap[] = {NULLPIN};
#endif 

#endif
