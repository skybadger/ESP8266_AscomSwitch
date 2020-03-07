"ESP8266_ASCOMSwitch
This application provides a remote ASCOM-enabled switch device for use in Astronomical observatories and instrument setups. 

This application runs on the ESP8266-01 wifi-enabled SoC device to provide remote REST access and control of a set of (typically) 8 device-attached relays or potentially more complex devices to control power to attached devices. 
The device also reports system health to the central MQTT service. 

The device supports simple client (STATION) WiFi, including setting the hostname in DNS and use of local DHCP to provide a system address and naming and network resolution service. 

The unit uses a PCF8574 I2C bus expander to control the eight bits of attached devices. Newer devices can support 16 bits and the device itself supports high and low addressing for multiple device on the one bus, to allow control of up to 32 attached pins. 
Use of the larger ESP8266-12 will also allow PWM and ADC devices to be managed. 
You'd have to edit the code for that.... 

Dependencies:
Arduino 1.86 IDE, 
ESP8266 V2.4+ 
Arduino MQTT client (https://pubsubclient.knolleary.net/api.html)
Arduino JSON library (pre v6) 
PCF8574

Testing
Access by serial port  - Tx only is available from device at 115,600 baud at 3.3v. THis provides debug monitoring output .
Wifi is used for MQTT reporting only and servicing rest API web requests
Use http://ESPRLY01 to receive json-formatted output of current pins. 

Use:
Install latest ASCOM drivers onto your platform. Add the ASCOM ALPACA remote interface.
Start the remote interface, configure it for the DNS name above on port 80 and select the option to explicitly connect. 

To setup the pin names, use the pin name field - e.g. '12v relay', focuser etc.
To setup the pin types, use the pin descriptions field - accepted settings are PWM, Relay_NO, Relay_NC, DAC. 
Use the custom setup Urls: 
http://espRLY01/setup - web page to manually configure settings ASCOM ALPACA doesn't provide for unless you have a windows driver setup page. 
http://espRLY01/status - json listing of all attached pin control blocks
http://espRLY01/config - individual switch configuration page. 
Once configured, the device should keep your settings through use of the EEProm. 

ToDo:

Caveats: 
Currently there is no user access control on the connection to the web server interface. Anyone can connect. so use this behind a well-managed reverse proxy.

Structure:
This code pulls the source code into the file using header files. Hence there is an order, typically importing ASCOM headers last. 

