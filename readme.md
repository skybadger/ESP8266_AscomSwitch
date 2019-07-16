"ESP8266_DHT111TempHum
environment sensor over WiFi connection" 
This application runs on the ESP8266-02 wifi-enabled SoC device to capture sensor readings and transmit them to the local MQTT service. 
In my arrangement, Node-red flows are used to listen for and graph the updated readings in the dashboard UI. 
The unit is setup for I2C operation and is expecting to see SCL on GIO0 and SDA on GPIO2 with the one-wire interface to the DHT sensor on GPIO3. 
The pressure sensor is the BMP280. If that is not present, the unit will continually reboot. 

Dependencies:
Arduino 1.6, 
ESP8266 V2.4+ 
Arduino MQTT client (https://pubsubclient.knolleary.net/api.html)
Arduino DHT111 sensor library (https://github.com/beegee-tokyo/DHTesp/archive/master.zip)
Arduino JSON library (pre v6) 
BMP280 library (https://github.com/orgua/iLib )

Testing
Access by serial port  - Tx only is available from device at 115,600 baud at 3.3v. THis provides debug output .
Wifi is used for MQTT reporting only and servicing web requests
Use http://ESPTHM01 to receive json-formatted output of current sensors. 

Use:
I use mine to source a dashboard via hosting the mqtt server in node-red. It runs off a solar-panel supply in my observatory dome. 

ToDo:
Add a HMC5883 compass interface 