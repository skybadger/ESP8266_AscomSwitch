<h1>ESP8266_ASCOMSwitch</h1>
This application provides a remote ASCOM-enabled switch device for use in Astronomical observatories and instrument setups. 

This instance runs on the ESP8266-01 wifi-enabled SoC device to provide remote REST access and control of a set of (typically) 8 device-attached relays or potentially more complex devices to control power to attached devices. 
The device reports system health to the central MQTT service and supports OTA updates.

The device implements simple client (STATION) WiFi, including setting the hostname in DNS and requires use of local DHCP services to provide a device IPv4 address, naming and network resolution services, and NTP time services. 

The unit uses a PCF8574 I2C bus expander to control the eight bits of attached devices. Newer devices than the 8574 can support 16 bits and the device itself supports high and low addressing for multiple devices on one bus, to allow control of up to 32 attached pins. 
Use of the larger ESP8266-12 SoC will also allow PWM and ADC devices to be managed by mapping outputs to specific pins and functions.
You'd have to edit the code further for that.... but its ready.

<h2>Dependencies:</h2>
<ul><li>Arduino 1.86 IDE, </li>
<li>ESP8266 V2.4+ </li>
<li>Arduino MQTT client (https://pubsubclient.knolleary.net/api.html)</li>
<li>Arduino JSON library (pre v6) </li>
<li>PCF8574</li>

<h3>Testing</h3>
Read-only monitoring by serial port - Tx only is available from device at 115,600 baud (8n1) at 3.3v. This provides debug monitoring output via Outty or another com terminal.
Wifi is used for MQTT reporting only and servicing REST API web requests
Use http://ESPASW01/status to receive json-formatted output of current pins. 
Use the batch file to test direct URL response via CURL.
Setup the ASCOM remote cliet and use the VBS file to test response of the switch as an ASCOM device using the ASCOM remote interface. 

<h3>Use</h3>
Install latest ASCOM drivers onto your platform. Add the ASCOM ALPACA remote interface.
Start the remote interface, configure it for the DNS name above on port 80 and select the option to explicitly connect. 

To setup the pin names, use the pin name field - e.g. '12v relay', focuser etc.
To setup the pin types, use the pin descriptions field - accepted settings are PWM, Relay_NO, Relay_NC, DAC. 
Use the custom setup Urls: 
<ul>
 <li>http://"hostname"/api/v1/switch/0/setup - web page to manually configure settings ASCOM ALPACA doesn't provide for unless you have a windows driver setup page. </li>
 <li>http://"hostname"/api/v1/switch/0/status - json listing of all attached pin control blocks</li>
 <li></li>
 </ul>
Once configured, the device keeps your settings through reboot by use of the onboard EEProm memory.

<h3>ToDo:</h3>
Add support for ESP12 additional pin mappings and functions in web pages. ITs already there in REST handlers

<h3>Caveats:</h3> 
Currently there is no user access control on the connection to the web server interface. Anyone can connect. so use this behind a well-managed reverse proxy.
Also, the ''connected' settings isn't checked to ensure that the REST command is coming from a client who has prevously called 'connected' and should effectively be in charge of the device from that point.

<h3>Structure:</h3>
This code pulls the source code into the file using header files inclusion. Hence there is an order, typically importing ASCOM headers last. 

