<h1>ESP8266_ASCOMSwitch</h1>
This application provides a remote ASCOM-enabled switch device for use in Astronomical observatories and instrument setups. 

This instance runs on the ESP8266-01 wifi-enabled SoC device to provide remote REST access and control of a set of (typically) 8 device-attached relays or potentially more complex devices to control power to attached devices. 
The device reports system health to the central MQTT service and supports OTA updates.

The device implements simple client (STATION) WiFi, including setting the hostname in DNS and requires use of local DHCP services to provide a device IPv4 address, naming and network resolution services, and NTP time services. 

The unit uses a PCF8574 I2C bus expander to control the eight bits of attached devices. Newer devices than the 8574 can support 16 bits and the device itself supports high and low addressing for multiple devices on one bus, to allow control of up to 32 attached pins. 
Use of the larger ESP8266-12 SoC will also allow PWM and ADC devices to be managed by mapping outputs to specific pins and functions.
You'd have to edit the code further for that.... but its ready.

<h2>Dependencies:</h2>
<ul>
<li>Ascom 6.5 with ASCOM Remote or 6.5 sp1 with built-in choose support for Alpaca. https://ascom-standards.org/Developer/Alpaca.htm </li>
<li>Arduino 1.86 IDE, https://www.arduino.cc/en/software/ </li>
<li>ESP8266 libraries V2.4+ http://arduino.esp8266.com/stable/package_esp8266com_index.json</li>
<li>Arduino MQTT client (https://pubsubclient.knolleary.net/api.html)</li>
<li>Arduino JSON library (pre v6) https://arduinojson.org/v5/api/ </li>
<li>PCF8574 arduino i2c library https://github.com/xreef/PCF8574_library</li>
<li>EepromAnything  - note I added support for c_strings to mine. https://github.com/semiotproject/Arduino-libraries/blob/master/EEPROMAnything </li>
<li>RemoteDebugger https://github.com/JoaoLopesF/RemoteDebug </li>
</ul> 

<h3>Features</h3> 
This program will use a ESP8266 Soc to control a relay board like this one: https://www.amazon.co.uk/AZDelivery-Module-Optocoupler-Arduino-8-Relais/dp/B07CT7SLYQ/ref=sr_1_28?keywords=arduino+relay+board&qid=1645892421&sr=8-28 and others over a Wifi connection using the Alpaca API specification. The relay boards are available in various sizes ( 16, 8, 4, 2 relays ) from the web for very little money. 
The code assumes a 5v enabled-low pin per relay control and a separate power supply is used as the controlled relay output. 
In setting a switch, the relay is closed by puling the related pin on the PCF8574 i2c port expander low. 
This code supports use of PWM if the SoC device supports it and there are free pins to assign to use it. 
Note use of PWM will mask a relay if a pin is assigned in the default relay range . 
i.e. if you have a 4-relay panel, configure the number of switches for example as 5 and set the  PWM pin at switch 5, assigning a pin to it and bring that pin out to a power device on your pcb.
This code also suports use of an I2C DAC as an analogue output device, say for using as a linear power controller into a transitor output stage or a servo feedback controller. This feature is not fully implemented yet.
The 'connected' setting is now unifirmly checked and saved across all reposuitiories that implement the ALPACA API to ensure that the REST command is coming from a client who has prevously called 'connected' and that client needs to release the 'connected' sretting before somewone else can isue chnaging commands to the switch. 
What this meamns is that a well-behaved client should always call 'connected'=true at the start of a session and 'connected'=false at the end, even for short sessoins or other users will be locked out until a reboot will clear this saved client setting. 


<h3>Testing</h3>
<h4>Using ASCOM Remote</h4>
Read-only monitoring by serial port - Tx only is available from device at 115,600 baud (8n1) at 3.3v. This provides debug monitoring output via Outty or another com terminal.
Wifi is used for MQTT reporting only and servicing REST API web requests
Use http://ESPASW01/status to receive json-formatted output of current pins. 
Use the batch file to test direct URL response via CURL.
Setup the ASCOM remote client and use the VBS file to test response of the switch as an ASCOM device using the ASCOM remote interface. 
<h4>Using the ASCOM Chooser ALPACA discovery (post ASCOM 6.5SP1)</h4>
Open the ASCOM chooser when selecting a driver. Ensure discovery is enabled. 

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
DAC is still not supported - thecode is largely in place but its not tested and not DAC device is prbed for on startup. 
I assume use of the MCP4675 single channel 12-bit voltage DAC 

<h3>Caveats:</h3> 
Currently there is no user access control on the connection to the web server interface. Anyone can connect. so use this code behind a well-managed reverse proxy.
If you are testing by burnig firmware into multiple ESP8266 devices and re-cycling them, DNS can get confused due to differnt devices with different MAC addresses trying to 34eb the same hostname in short order. Either burn the first one from the IDE and then use the HTTP Updater for the rest ( http://'hostname'/update) or clean out DNS each time. 
Address collisions resolve themselves after a DHCP lease time has expired. 

<h3>Structure:</h3>
This code pulls the source code into the file using header files inclusion. Hence there is an order, typically importing ASCOM headers last. 

