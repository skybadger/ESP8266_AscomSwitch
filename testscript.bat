
Echo "Testing driver info "
curl espasw01.i-badger.co.uk

REM "Connecting "
curl -X PUT -d "ClientID=99&ClientTransactionID=123&connected=true" "http://espasw01/api/v1/switch/0/connected"
timeout /t 2

echo "Testing basic driver information"
curl "http://espasw01/api/v1/switch/0/connected?ClientID=99&ClientTransactionID=123"
timeout /t 2
curl "http://espasw01/api/v1/switch/0/description?ClientID=99&ClientTransactionID=123"
timeout /t 2
curl "http://espasw01/api/v1/switch/0/driverinfo?ClientID=99&ClientTransactionID=123" 
timeout /t 2
curl "http://espasw01/api/v1/switch/0/name?ClientID=99&ClientTransactionID=123"
timeout /t 2
curl "http://espasw01/api/v1/switch/0/driverversion?ClientID=99&ClientTransactionID=123"
timeout /t 2
curl "http://espasw01/api/v1/switch/0/interfaceversion?ClientID=99&ClientTransactionID=123"
timeout /t 2
curl "http://espasw01/api/v1/switch/0/supportedactions?ClientID=99&ClientTransactionID=123"
timeout /t 2

Echo "Testing Custom actions"
curl -X PUT -d "ClientID=99&ClientTransactionID=123&thing=123" "http://espasw01/api/v1/switch/0/action"
timeout /t 2
curl -X PUT -d "ClientID=99&ClientTransactionID=123" "http://espasw01/api/v1/switch/0/commandblind"
timeout /t 2
curl -X PUT -d "ClientID=99&ClientTransactionID=123" "http://espasw01/api/v1/switch/0/commandbool"
timeout /t 2
curl -X PUT -d "ClientID=99&ClientTransactionID=123" "http://espasw01/api/v1/switch/0/commandstring"
timeout /t 2

ECHO "Testing switch get statements"
curl "http://espasw01/api/v1/switch/0/maxswitch?ClientID=99&ClientTransactionID=123"
timeout /t 2
curl "http://espasw01/api/v1/switch/0/canwrite?ClientID=99&ClientTransactionID=123&Id=0"
timeout /t 2
curl "http://espasw01/api/v1/switch/0/canwrite?ClientID=99&ClientTransactionID=123&Id=8"
timeout /t 2
curl "http://espasw01/api/v1/switch/0/getswitchdescription?ClientID=99&ClientTransactionID=123&Id=2" 
timeout /t 2
curl "http://espasw01/api/v1/switch/0/getswitch?ClientID=99&ClientTransactionID=123&Id=2" 
timeout /t 2
curl "http://espasw01/api/v1/switch/0/getswitch?ClientID=99&ClientTransactionID=123&Id=8" 
timeout /t 2
curl "http://espasw01/api/v1/switch/0/getswitchname?ClientID=99&ClientTransactionID=123&Id=2" 
timeout /t 2
curl "http://espasw01/api/v1/switch/0/getswitchvalue?ClientID=99&ClientTransactionID=123&Id=2" 
timeout /t 2
curl "http://espasw01/api/v1/switch/0/minswitchvalue?ClientID=99&ClientTransactionID=123&Id=2" 
timeout /t 2
curl "http://espasw01/api/v1/switch/0/maxswitchvalue?ClientID=99&ClientTransactionID=123&Id=2" 
timeout /t 2
curl "http://espasw01/api/v1/switch/0/switchstep?ClientID=99&ClientTransactionID=123&Id=2" 
timeout /t 2

curl "http://espasw01/api/v1/switch/0/name?ClientID=99&ClientTransactionID=123"
timeout /t 2
curl "http://espasw01/api/v1/switch/0/driverversion?ClientID=99&ClientTransactionID=123"
timeout /t 2
curl "http://espasw01/api/v1/switch/0/interfaceversion?ClientID=99&ClientTransactionID=123"
timeout /t 2
curl "http://espasw01/api/v1/switch/0/supportedactions?ClientID=99&ClientTransactionID=123"
timeout /t 2

ECHO "Testing put statements"
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=0&state=true" "http://espasw01/api/v1/switch/0/setswitch"
timeout /t 2
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=0&state=true" "http://espasw01/api/v1/switch/0/setswitch"
timeout /t 2
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=0&state=false" "http://espasw01/api/v1/switch/0/setswitch"
timeout /t 2
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=1&state=false" "http://espasw01/api/v1/switch/0/setswitch"
timeout /t 2
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=2&state=false" "http://espasw01/api/v1/switch/0/setswitch"
timeout /t 2
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=7&state=false" "http://espasw01/api/v1/switch/0/setswitch"
timeout /t 2
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=9&state=false" "http://espasw01/api/v1/switch/0/setswitch"
timeout /t 2
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=7&name='fabulous switch' "http://espasw01/api/v1/switch/0/setswitchname"
timeout /t 2

curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=7&value=0.0" "http://espasw01/api/v1/switch/0/setswitchvalue"
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=7&value=0.5" "http://espasw01/api/v1/switch/0/setswitchvalue"
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=7&value=1.5" "http://espasw01/api/v1/switch/0/setswitchvalue"
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=7&state=true" "http://espasw01/api/v1/switch/0/setswitch"
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=3&state=true" "http://espasw01/api/v1/switch/0/setswitch"
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=2&state=true" "http://espasw01/api/v1/switch/0/setswitch"
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=1&state=true" "http://espasw01/api/v1/switch/0/setswitch"
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=0&state=true" "http://espasw01/api/v1/switch/0/setswitch"
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=0&state=false" "http://espasw01/api/v1/switch/0/setswitch"
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=0&state=true" "http://espasw01/api/v1/switch/0/setswitch"
curl -X PUT -d "ClientID=99&ClientTransactionID=123&Id=1&state=true" "http://espasw01/api/v1/switch/0/setswitch"

REM still to be tested
REM Non-ascom - yet to be tested 
REM status - works 
REM setup page
REM setupswitches
REM UDP discovery 