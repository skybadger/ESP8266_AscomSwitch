/*
ASCOM project common variables
*/
#ifndef _WEBRELAY_COMMON_H_
#define _WEBRELAY_COMMON_H_

#if !defined DEBUG
#define DEBUG
#endif 

#define _REVERSE_RELAY_LOGIC_
#if defined _REVERSE_RELAY_LOGIC_
const bool reverseRelayLogic = true;
#else
const bool reverseRelayLogic = false;
#endif

const int MAX_NAME_LENGTH = 25;
#define DEFAULT_NUM_SWITCHES 2;
const int defaultNumSwitches = DEFAULT_NUM_SWITCHES;
#define ASCOM_DEVICE_TYPE "switch" //used in server handler uris
const int defaultInstanceNumber = 0;
int instanceNumber = 0;

//ASCOM driver common variables 
unsigned int transactionId;
unsigned int connectedClient = -1;
bool connected = false;
const String DriverName = "Skybadger.ESPSwitch";
const String DriverVersion = "0.0.1";
const String DriverInfo = "Skybadger.ESPSwitch RESTful native device. ";
const String Description = "Skybadger ESP2866-based wireless ASCOM switch device";
const String InterfaceVersion = "2";
const String DriverType = "Switch";

#define TZ              0       // (utc+) TZ in hours
#define DST_MN          00      // use 60mn for summer time in some countries
#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)

const String switchTypes[5] = {"Relay_NO", "Relay_NC", "PWM", "DAC","Not Selected"};
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
  bool writeable = true;
  float min = 0.0;
  float max = 1.0;
  float step = 1.0;
  float value = 0.0F;
} SwitchEntry;

//Define the maximum number of switches supported - limited by memory really 
const int MAXSWITCH = 16;
//define the max resolution available to control a DAC or PWM
const int MAX_DIGITAL_STEPS = 1024;
//Value limits
const float MAXDIGITALVAL = 1024.0F; //ie 12 bit resolution supported.  Vcc assumed max range.
const float MAXBINARYVAL = 1.0F;      // true or false ? open or closed settings on relay. 
const float MINVAL = 0.0F;

//Pin limits
const int NULLPIN = 0; 
#if defined ESP8266-01
//GPIO 0 is Serial tx, GPIO 2 I2C SDA, GPIO 1 SCL, so it depends on whether you use i2c or not.
//Cos that just leaves Rx.  
const int pinMap[] = {3}; 
#elif defined ESP8266-12
//Most pins are used for flash, so we assume those for SSI are available.
//Typically use 4 and 5 for I2C, leaves 
const int pinMap[] = { 2, 14, 12, 13, 15};
#else
const int pinMap[] = {NULLPIN};
#endif 

const int MINPIN = 1; //device specific
const int MAXPIN = 16; //device specific

//UDP discovery service responder struct.
#define ALPACA_DISCOVERY_PORT 32227
struct DiscoveryPacket
 {
  const char* protocol = "alpadiscovery1" ;
  byte version; //1-9, A-Z
  byte reserved[48];
 }; 
 
 //Req: Need to provide a method to change the discovery port using setup
 //Discovery response: 
 //JSON "AlpacaPort":<int>
 //JSON: unique identifier
 //JSON: hostname of device ?
 //JSON: type of device ?
 
#endif
