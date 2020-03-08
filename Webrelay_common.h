/*
ASCOM project common variables
*/
#ifndef _WEBRELAY_COMMON_H_
#define _WEBRELAY_COMMON_H_

const int MAX_NAME_LENGTH = 25;
#define DEFAULT_NUM_SWITCHES 8;
const int defaultNumSwitches = DEFAULT_NUM_SWITCHES;

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

const String switchTypes[] = {"PWM","Relay_NO","DAC","Relay_NC"};
enum SwitchType { SWITCH_PWM, SWITCH_RELAY_NO, SWITCH_RELAY_NC, SWITCH_ANALG_DAC };

/*
 Typical values for PWM And ADC are 0 - 1024/1024, PWM in terms of fraction of the wave is high 
 and DAC in terms of the output voltage as a fraction of Vcc. 
 
 */
//define the max resolution available to control a DAC or PWM
#define MAX_DIGITAL_STEPS 1024 

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
