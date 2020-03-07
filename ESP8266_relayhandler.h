/*
ESP8266_relayhandler.h
This doc is a set of functions to handle the webb server functions for the esp8266_relay ASCOM switch interface.
There can be more than one switch. Hence the setup allows specifying the device umber and the host/device name.
This particular switch device assumes the use of port pins allocated from the device via a pcf8574 I2C port expansion device. 
Use of the PWM port type is going to need a more intelligence device such as a larger ESP8266 device. 
Use of a DAC requires (presumably) an i2C DAC 

In use of the ASCOM Api 
All URLs include an argument 'Id' which contais the number of the unit attached to this switch instance
The switch device number is in the path itself. Hence the getUriField function
Internally this code keeps the state of the switch in doubles and uses (value != 1.0F) to mean false. 

 To do:
 Debug, trial
 
 Layout:
 GPIO 4,2 to SDA
 GPIO 5,0 to SCL 
 All 3.3v logic. 
  
*/

#ifndef _ESP8266_RELAYHANDLER_H_
#define _ESP8266_RELAYHANDLER_H_

#include "Webrelay_eeprom.h"
#include <Wire.h>
#include "AlpacaErrorConsts.h"
#include "ASCOMAPISwitch_rest.h"


//Function definitions
void initSwitch( SwitchEntry* sourceSe, SwitchEntry* targetSe );
SwitchEntry* reSize( SwitchEntry* old, int newSize );
bool getUriField( char* inString, int searchIndex, String& outRef );
String& setupFormBuilder( String& htmlForm, String& errMsg );

/*
 * This function will write a copy of the provided deviceEntry structure into the internal memory array. 
 * switch Entry may be a pointer to a single item or an array of items. ID must be in range;
 */
void initSwitch( SwitchEntry* sourceSe, SwitchEntry* targetSe )
{
    targetSe->type        = SWITCH_RELAY_NO;
    strncpy( targetSe->description, sourceSe->description, MAX_NAME_LENGTH);
    targetSe->min         = sourceSe->min;
    targetSe->max         = sourceSe->max;
    targetSe->writeable   = sourceSe->writeable;
    targetSe->step        = sourceSe->step;
    targetSe->value       = sourceSe->value;
}

/*
 * This function re-sizes the existing switchEntry array by re-allocating memory and copying if required. 
 */
SwitchEntry** reSize( SwitchEntry** old, int newSize )
{
  SwitchEntry** pse = (SwitchEntry** ) malloc( sizeof (SwitchEntry*) * newSize );
  int i=0;
  SwitchEntry* newse;
  if ( newSize < numSwitches )
  {
    for ( i=0 ; i < newSize; i++ )
      pse[i] = old[i];      
    for ( ; i < numSwitches; i++ )
    {
      free( old[i]->description );
      free( old[i] );
    }
    switchEntry = pse;
  }
  else if ( newSize == numSwitches)
  {
    //do nothing
    free(pse);
  }
  else //bigger than before
  {
    for ( i = 0; i < numSwitches; i++ )
       pse[i] = old[i];      
    for ( ; i < newSize; i++ )
    {
      newse = (SwitchEntry*) malloc( sizeof (SwitchEntry)  );
      pse[i] = newse;
      initSwitch( pse[i], newse );
    }
    switchEntry = pse;
  }
  return pse;
}

bool getUriField( char* inString, int searchIndex, String& outRef )
{
  char *p = inString;
  char *str;
  char delims1[] = {"//"};
  char delims2[] = {"/:"};
  int chunkCtr = 0;
  bool  status = false;    
  int localIndex = 0;
  
  localIndex = String( inString ).indexOf( delims1 );
  if( localIndex >= 0 )
  {
    while ((str = strtok_r(p, delims2, &p)) != NULL) // delimiter is the semicolon
    {
       if ( chunkCtr == searchIndex && !status )
       {
          outRef = String( str );
          status = true;
       }
       chunkCtr++;
    }
  }
  else 
    status = false;
  
  return status;
}

//GET ​/switch​/{device_number}​/maxswitch
//The number of switch devices managed by this driver
void handlerMaxswitch(void)
{
    String message;
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = (uint32_t)server.arg("Id").toInt();

    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, "MaxSwitch", Success , "" );    
    root["Value"] = numSwitches;
    
    root.printTo(message);
    server.send(200, "text/json", message);
    return ;
}

//GET ​/switch​/{device_number}​/canwrite
//Indicates whether the specified switch device can be written to
void handlerCanWrite(void)
{
    String message;
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    int statusCode = 400;
    int switchID = -1;
    String argToSearchFor = "Id";
    if( hasArgIC( argToSearchFor, server, false ) )
      switchID = server.arg(argToSearchFor).toInt();
     
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, "CanWrite", Success , "" );    
    
    if ( switchID >= 0 && switchID < numSwitches ) 
    {
      root["Value"] = switchEntry[switchID]->writeable;
      statusCode = 200;
    }
    else
    {
      //root["Value"] = switchEntry[switchID]->writeable;
      root["ErrorMessage"] = "invalid switch Id";
      root["ErrorNumber"] = invalidValue ; 
    }

    root.printTo(message);
    server.send( statusCode , "text/json", message);
    return ;
}

//GET ​/switch​/{device_number}​/getswitch
//PUT ​/switch​/{device_number}​/setswitch
//Get/Set the state of switch device id as a boolean
void handlerSwitchState(void)
{
    String message;
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    int returnCode = 200;
    double switchValue; 
    bool bValue;
    bool newState = false;
    int switchID = -1;
    String argToSearchFor[2] = {"Id", "State"};
    
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, "SwitchState", Success , "" );    

    if( hasArgIC( argToSearchFor[0], server, false ) )
      switchID = server.arg( argToSearchFor[0] ).toInt();
    
    if ( switchID >= 0 && switchID < numSwitches )
    {
      switchValue = switchEntry[switchID]->value;
      if ( switchValue != 1.0F ) 
        bValue = false;
      else 
        bValue = true; 

      if( server.method() == HTTP_GET  )
      {
        if( strcasecmp( switchEntry[switchID]->description, "Relay_NO" ) == 0 || strcasecmp( switchEntry[switchID]->description, "Relay_NC" ) == 0  )
        {
          root["Value"] =  bValue;  
          returnCode = 200;
        }
        else
        {
            returnCode = 400;
            root["ErrorMessage"] = "invalid state retrieval (boolean) for switch type "  ;
            root["ErrorNumber"] = invalidValue ;
        }
      }
      else if (server.method() == HTTP_PUT && hasArgIC( argToSearchFor[1], server, false ) )
      {
        if( server.arg( argToSearchFor[1] ).equalsIgnoreCase( "true" ) )
          newState = true;
        else
          newState = false;

        if( strcasecmp( switchEntry[switchID]->description, "Relay_NO") == 0 || strcasecmp( switchEntry[switchID]->description, "Relay_NC") == 0  )
          {
            if ( newState != bValue )
            {
              switchEntry[switchID]->value = (newState)? 1.0F: 0.0F;
              switchDevice.write( switchID, newState );
            }
            returnCode = 200;
          }
         else
        {
            returnCode = 400;
            root["ErrorMessage"] = "invalid state setting (boolean) for non-boolean switch type";
            root["ErrorNumber"] = invalidValue ;
        }
      }
      else
      {
         String output = "";
         Serial.println( "Error: method not available" );
         root["ErrorNumber"] = 0x403;
         output = "http verb:";
         output += server.method();
         output += " not available";
         root["ErrorMessage"] = output; 
         returnCode = 400;
      }  
    }
    root.printTo(message);
    server.send(returnCode, "text/json", message);
    return;
}

//GET ​/switch​/{device_number}​/getswitchdescription
//Gets the description of the specified switch device
void handlerSwitchDescription(void)
{
    String message;
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    int returnCode = 200;
    String argToSearchFor = "Id";
    int switchID = -1;
          
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, "SwitchDescription", Success, "" );    

    if( hasArgIC( argToSearchFor, server, false ) )
      switchID = server.arg( argToSearchFor ).toInt();

    root["Value"] = switchEntry[switchID]->description;
    
    root.printTo(message);
    server.send(returnCode, "text/json", message);
    return;
}

//GET ​/switch​/{device_number}​/getswitchname
//PUT ​/switch​/{device_number}​/setswitchname
//Get/set the name of the specified switch device
void handlerSwitchName(void)
{
    String message;
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    int returnCode = 200;
    int switchID;
    String newName =  "";
    String argToSearchFor[2] = {"Id", "Name"};
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, "SwitchName", 0, "" );    
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      switchID = server.arg(argToSearchFor[0]).toInt();
     
    if ( switchID >= 0 && switchID < numSwitches  )
    {
      if ( server.method() == HTTP_GET )
      {
          root["Value"] = switchEntry[switchID]->switchName;
          returnCode = 200;
      }
      else if( server.method() == HTTP_PUT && hasArgIC( argToSearchFor[1], server, false ) )
      {
          int sLen = strlen( server.arg( argToSearchFor[1] ).c_str());
          if ( sLen > MAX_NAME_LENGTH -1 )
          {
            root["ErrorMessage"]= "Switch name too long";
            root["ErrorNumber"] = invalidValue ;
            returnCode = 400;
          }
          else
          {
            if ( switchEntry[switchID]->switchName != nullptr ) 
              switchEntry[switchID]->switchName  = (char*) calloc( MAX_NAME_LENGTH, sizeof(char) );
            strcpy( switchEntry[switchID]->switchName, server.arg( argToSearchFor[1] ).c_str());
          }                    
      }
      else
      {
         //Invalid http verb 
         returnCode = 400;
         root["ErrorMessage"]= "Invalid HTTP verb found";
         root["ErrorNumber"] = invalidOperation;
      }
    }
    else
    {
      //invalid switch id 
      returnCode = 400;
      root["ErrorMessage"]= "Invalid switch ID - outside range";
      root["ErrorNumber"] = invalidValue ;

    }
    root.printTo(message);
    server.send(returnCode, "text/json", message);
    return;
}

//GET ​/switch​/{device_number}​/getswitchname
//PUT ​/switch​/{device_number}​/setswitchname
//Get/set the name of the specified switch device
void handlerSwitchType(void)
{
    String message;
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    int returnCode = 200;
    int switchID;
    String newName =  "";
    String argToSearchFor[2] = {"Id", "Name"};
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, "SwitchType", 0, "" );    
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
      switchID = server.arg(argToSearchFor[0]).toInt();
     
    if ( switchID >= 0 && switchID < numSwitches  )
    {
      if ( server.method() == HTTP_GET )
      {
          root["Value"] = switchEntry[switchID]->type;
      }
      else if( server.method() == HTTP_PUT && hasArgIC( argToSearchFor[1], server, false ) )
      {
          enum SwitchType newType = ( enum SwitchType ) server.arg(argToSearchFor[1]).toInt();          
          switch( newType )
          {
          case SWITCH_RELAY_NO:
          case SWITCH_RELAY_NC:
          case SWITCH_ANALG_DAC:
          case SWITCH_PWM:
              switchEntry[switchID]->type = (enum SwitchType) newType;
              returnCode = 200;
              break;
          default:
              root["ErrorMessage"]= "Invalid switch type not found ";
              root["ErrorNumber"] = invalidValue ;
              returnCode = 400;
              break;
          }
      }
      else
      {
         returnCode = 400;
         root["ErrorMessage"]= "Invalid method or arguments found";
         root["ErrorNumber"] = invalidValue ;
      }
    }
    root.printTo(message);
    server.send(returnCode, "text/json", message);
    return;
}

//GET ​/switch​/{device_number}​/getswitchvalue
//PUT ​/switch​/{device_number}​/setswitchvalue
//Get/Set the value of the specified switch device as a double
void handlerSwitchValue(void)
{
    String message;
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    int returnCode = 200;
    double value = 0.0F;
    uint32_t switchID = 0;
    String argToSearchFor[2] = {"Id", "Value"};
    
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, "SwitchValue", 0, "" );    
    
    if ( hasArgIC( argToSearchFor[0], server, false ) )
      switchID = server.arg( argToSearchFor[0] ).toInt();
      
    if ( server.method() == HTTP_GET && switchID >= 0 && switchID < (uint32_t) numSwitches )
    {
        root["Value"] = switchEntry[switchID]->value;
    }
    else if( server.method() == HTTP_PUT && 
             hasArgIC( argToSearchFor[1], server, false ) && 
             switchID >= 0 && 
             switchID < (uint32_t) numSwitches )
    {
        value = server.arg( argToSearchFor[2] ).toDouble();
        if( switchEntry[switchID]->type == SWITCH_PWM )
        {
          if ( value >=  0.0F && value <= switchEntry[switchID]->max ) 
          {
            switchEntry[switchID]->value = value;  
            //switchDevice.write( switchID, value ); TODO - haven't implemented yet. 
            //e.g. analogue_write( 256, 15);
          }
        }
        else
        {
          root["ErrorMessage"] = "Invalid value (double) for this switch type (boolean) . Check description";
          root["ErrorNumber"] = invalidValue ;
          returnCode = 400;
        }            
    }
    else
    {
       returnCode = 400;
       root["ErrorMessage"] = "Invalid method or arguments foor this URI. Check description";
       root["ErrorNumber"] = invalidValue ;
    }
    root.printTo(message);
    server.send(returnCode, "text/json", message);
    return;
}

//GET ​/switch​/{device_number}​/minswitchvalue
//Gets the minimum value of the specified switch device as a double
void handlerMinSwitchValue(void)
{
    String message;
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    int returnCode = 200;
    uint32_t switchID  = -1;
    String argToSearchFor = "id";
    
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, "MinSwitchValue", 0, "" );    
    
    if ( hasArgIC( argToSearchFor, server, false  ) )
      switchID = server.arg( argToSearchFor ).toInt();
    
    root["Value"] = switchEntry[switchID]->min;
    
    root.printTo(message);
    server.send(returnCode, "text/json", message);
    return;
}

//GET ​/switch​/{device_number}​/maxswitchvalue
//Gets the maximum value of the specified switch device as a double
void handlerMaxSwitchValue(void)
{
    String message;
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    int returnCode = 200;
    uint32_t switchID  = -1;
    String argToSearchFor = "Id";
    
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, "MaxSwitchValue", 0, "" );    

    if ( hasArgIC(argToSearchFor, server, false ) )
      switchID = server.arg(argToSearchFor).toInt();
    
    if( switchID >= 0 && switchID < (uint32_t) numSwitches ) 
    {
      root["value"] = switchEntry[switchID]->max;
      returnCode = 200;
    }
    else
    {
       root["ErrorMessage"] = "Invalid method or arguments for this URI. Check description";
       root["ErrorNumber"] = invalidValue  ;
       returnCode = 200;
    }
    root.printTo(message);
    server.send(returnCode, "text/json", message);
    return;
}

//GET ​/switch​/{device_number}​/switchstep
//Returns the step size that this device supports (the difference between successive values of the device).
void handlerSwitchStep(void)
{
    String message;
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;
    int returnCode = 200;
    String argToSearchFor = "Id";
    
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, "SwitchStep", 0, "" );    

    if ( hasArgIC(argToSearchFor, server, false ) )
      switchID = server.arg(argToSearchFor).toInt();
    
    if( switchID >= 0 && switchID < (uint32_t) numSwitches ) 
    {
      root["value"] = switchEntry[switchID]->step;
      returnCode = 200;
    }
    else
    {
       root["ErrorMessage"] = "Invalid method or arguments for this URI. Check description";
       root["ErrorNumber"] = invalidValue ;
       returnCode = 200;
    }

    root.printTo(message);
    server.send(returnCode, "text/json", message);
    return;
}

////////////////////////////////////////////////////////////////////////////////////
//Additional non-ASCOM custom setup calls

void handlerNotFound()
{
  String message;
  int responseCode = 400;
  uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
  uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
  DynamicJsonBuffer jsonBuffer(250);
  JsonObject& root = jsonBuffer.createObject();
  jsonResponseBuilder( root, clientID, transID, "HandlerNotFound", invalidOperation , "No REST handler found for argument - check ASCOM Switch v2 specification" );    
  root["Value"] = 0;
  root.printTo(message);
  server.send(responseCode, "text/json", message);
}

void handlerNotImplemented()
{
  String message;
  int responseCode = 400;
  uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
  uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();

  DynamicJsonBuffer jsonBuffer(250);
  JsonObject& root = jsonBuffer.createObject();
  jsonResponseBuilder( root, clientID, transID, "HandlerNotFound", notImplemented  , "No REST handler implemented for argument - check ASCOM Dome v2 specification" );    
  root["Value"] = 0;
  root.printTo(message);
  server.send(responseCode, "text/json", message);
}

//GET ​/switch​/{device_number}​/status
//Get a descriptor of all the switches managed by this driver for discovery purposes
void handlerStatus(void)
{
    String message, timeString;
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    int i=0;
    int returnCode = 400;
    
    DynamicJsonBuffer jsonBuffer(512);
    JsonObject& root = jsonBuffer.createObject();
    JsonArray& entries = root.createNestedArray( "switches" );
    jsonResponseBuilder( root, clientID, transID, "Status", 0, "" );    
    
    root["time"] = getTimeAsString( timeString );
    root["host"] = myHostname;
    
    for( i = 0; i < numSwitches; i++ )
    {
      //Can I re-use a single object or do I need to create a new one each time? 
      JsonObject& entry = jsonBuffer.createObject();
      entry["type"]        = switchEntry[i]->type;
      entry["description"] = switchEntry[i]->description;
      entry["min"]         = switchEntry[i]->min;
      entry["max"]         = switchEntry[i]->max;
      entry["step"]        = switchEntry[i]->step;
      if( switchEntry[i]->type == SWITCH_RELAY_NO || switchEntry[i]->type == SWITCH_RELAY_NC )
      {
        entry["state"]     = (switchEntry[i]->value == 1.0F ) ? true : false ;
      }
      else 
        entry["value"]     = switchEntry[i]->value; //Needs check limits to 1-1024, DAC and PWM limits. 
      entry["writeable"]   = switchEntry[i]->writeable;
      entries.add( entry );
    }
    Serial.println( message);
    root.prettyPrintTo(message);
    server.send(returnCode, "text/json", message);
    return;
}

/*
 * Handler to do custom setup that can't be done without a windows ascom driver setup form. 
 */
 void handlerSetup(void) 
 {
    String message, timeString, err= "";
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;
    
    int returnCode = 400;
    String argToSearchFor[] = { "hostname", "numSwitches"};
     
    if ( server.method() == HTTP_GET )
    {
        message = setupFormBuilder( message, err );      
        returnCode = 200;    
    }
    else if ( server.method() == HTTP_POST || server.method() == HTTP_PUT )
    {
        if( hasArgIC( argToSearchFor[0], server, false )  )
        {
          String newHostname = server.arg(argToSearchFor[0]) ;
          //process form variables.
          if( newHostname.length() > 0 && newHostname.length() < MAX_NAME_LENGTH-1 )
          {
            //process new hostname
            strncpy( myHostname, newHostname.c_str(), MAX_NAME_LENGTH );
          }

          message = setupFormBuilder( message, err );      
          returnCode = 200;    
          saveToEeprom();
          server.send(returnCode, "text/html", message);
          device.reset();
        }
        else if( hasArgIC( argToSearchFor[1], server, false ) )
        {
          int newNumSwitches = server.arg(argToSearchFor[1]).toInt();
          if( newNumSwitches >= 0 && newNumSwitches <=16 )
          {
            //update the switches
            ;;
          err = "Switch resizing not yet ready";
          message = setupFormBuilder( message, err );      
          returnCode = 200;    
          }
        }
    }
    else
    {
      err = "Bad HTTP request verb";
      message = setupFormBuilder( message, err );      
    }
    server.send(returnCode, "text/html", message);
    return;
 }

 void handlerSetupSwitches(void) 
 {
    String message, timeString, err= "";
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;
    int i;
    int returnCode = 200;
    String argToSearchFor[] = { "Id","switchName", "type", "max", "min", "step", "writeable", "value", "description" };
    
    if ( server.method() == HTTP_POST || server.method() == HTTP_PUT )
    {
        //Expecting an array of variables in form submit
        //Need to parse and handle
        for( i=0; i< numSwitches; i++ )
        {        
          if( hasArgIC( argToSearchFor[0], server, false )  )
          {
            ;;
          }
          err = "Not yet implemented";
          returnCode = 200;
        }       
    }
    message = setupFormBuilder( message, err );      
    server.send(returnCode, "text/html", message);
    return;  
 }
/*
 Handler for setup dialog - issue call to <hostname>/setup and receive a webpage
 Fill in the form and submit and handler for each form button will store the variables and return the same page.
 optimise to something like this:
 https://circuits4you.com/2018/02/04/esp8266-ajax-update-part-of-web-page-without-refreshing/
 Bear in mind HTML standard doesn't support use of PUT in forms and changes it to GET so arguments get sent in plain sight as 
 part of the URL.
 */
String& setupFormBuilder( String& htmlForm, String& errMsg )
{
  String hostname = WiFi.hostname();
  
  htmlForm = "<!DocType html><html lang=en ><head></head><meta charset=\"utf-8\">";
  htmlForm += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  htmlForm += "<link rel=\"stylesheet\" href=\"https://maxcdn.bootstrapcdn.com/bootstrap/4.3.1/css/bootstrap.min.css\">";
  htmlForm += "<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/3.4.1/jquery.min.js\"></script>";
  htmlForm += "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/popper.js/1.14.7/umd/popper.min.js\"></script>";
  htmlForm += "<script src=\"https://maxcdn.bootstrapcdn.com/bootstrap/4.3.1/js/bootstrap.min.js\"></script>";
  htmlForm += "<body><div class=\"container\">";
  
  htmlForm += "<div class=\"row\" id=\"topbar\" bgcolor='A02222'>";
  htmlForm += "<p> This is the setup page for the Skybadger <a href=\"https://www.ascom-standards.org\">ASCOM</a> Switch device 'espRLY01' which uses the <a href=\"https://www.ascom-standards.org/api\">ALPACA</a> v1.0 API</b>";
  htmlForm += "</div>";

  if( errMsg != NULL && errMsg.length() > 0 ) 
  {
    htmlForm += "<div class=\"row\" id=\"errorbar\" bgcolor='A02222'>";
    htmlForm += "<b>Error Message </b>";
    htmlForm += "</div>";
    htmlForm += "<hr>";
  }
 
  //Device settings hostname and number of switches on this device
  htmlForm += "<div class=\"row\" id=\"deviceAttrib\" bgcolor='blue'>\n";
  htmlForm += "<h2> Enter new hostname for device</h2><br/>";
  htmlForm += "<p>Changing the hostname will cause the device to reboot and may change the IP address!</p>\n";
  htmlForm += "<form action=\"http://";
  htmlForm.concat( myHostname );
  htmlForm += "/setup/\" method=\"POST\" id=\"hostname\" >\n";
  htmlForm += "<input type=\"text\" name=\"hostname\" value=\"";
  htmlForm.concat( myHostname );
  htmlForm += "\">\n";

  htmlForm += "<h2>Update switches</h2><br/>";
  htmlForm += "<p>Upscaling will copy the existing setup to the new setup but you will need to edit the added switches. </p>";
  htmlForm += "<p>Downscaling will delete the configuration for the switches dropped</p><br>";
  htmlForm += "<p>New switch count: <input type=\"number\" name=\"numSwitches\" min=\"1\" max=\"16\" value=\"8\"></p>";
  htmlForm += "<input type=\"submit\" value=\"Submit\"> </form> </div>";

  htmlForm += "<div class=\"col-sm-2\"> ";
  htmlForm += "<form action=\"http://";
  htmlForm += myHostname;
  htmlForm += "/api/v1/switch/setup/switch\">";
  htmlForm += "<h2>Switch configuration </h2>";
  htmlForm += "<br><p>In order to configure the switches, select the switch you need below.</p>";
  
  htmlForm += "";
  htmlForm += "<input type=\"radio\" name=\"switchNum\" value=\"0\" checked > 0 <br>";
//      <input type="radio" name="switchNum" value="1" > 1 <br>
  htmlForm += "<input type=\"submit\" value=\"Submit\">";
  htmlForm += "</form>";
  htmlForm += "</div >";

/*   
  <!DocType html>
<html lang=en >
<head></head>
<meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://maxcdn.bootstrapcdn.com/bootstrap/4.3.1/css/bootstrap.min.css">
  <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.4.1/jquery.min.js"></script>
  <script src="https://cdnjs.cloudflare.com/ajax/libs/popper.js/1.14.7/umd/popper.min.js"></script>
  <script src="https://maxcdn.bootstrapcdn.com/bootstrap/4.3.1/js/bootstrap.min.js"></script>
  
  <body>
  <div class="container" >
     <div class="row" id="topbar" bgcolor='A02222'>
     <p> This is the setup page for the Skybadger <a href="https://www.ascom-standards.org">ASCOM</a> Switch device 'espRLY01' which uses the <a href="https://www.ascom-standards.org/api">ALPACA</a> v1.0 API</b>
     </div>

     <div class="row" id="errorbar" bgcolor='A02222'>
     <b>    Error Message </b>
     </div>
     <div class="row" id="deviceName" bgcolor='blue'>
     <div class="col-sm-2">
     <form method="POST" action="http://espRLY01/setup" >
     <h2>Change hostname</h2>
     <p>Changing the hostname for this device will cause the device to reboot and may change the IP address!</p>
     <p>Enter new Hostname: <input type="text" name="hostname" value="espRLY01" > </p>

     <h2> Update switches</h2>
     <p>Upscaling will copy the existing setup to the new setup but you will need to edit the added switches. </p>
     <p>Downscaling will delete the configuration for the switches dropped</p><br>
     <p>New switch count: <input type="number" name="numSwitches" min="1" max="16" value="8"></p>
     <input type="submit" value="Submit">
     </form>
     </div>
    
     <div class="col-sm-2"> 
     <form action="http://espRLY01/setupSwitch">
     <h2>Switch configuration </h2>
     <br>
     <p>In order to configure the switches, select the switch you need below.</p>

      <input type="radio" name="switchNum" value="0" checked> 0 <br>
      <input type="radio" name="switchNum" value="1" > 1 <br>
      <input type="radio" name="switchNum" value="2" > 2 <br>
      <input type="radio" name="switchNum" value="3" > 3 <br>
      <input type="radio" name="switchNum" value="4" > 4 <br>
      <input type="radio" name="switchNum" value="5" > 5 <br>
      <input type="submit" value="Submit">
      </div >   
      </form>
      </div>
</body>
</html>
  
  htmlForm += "<input type=\"number\" name=\"unitCount\" value=\"";
  htmlForm.concat( numSwitches );
  htmlForm += "\">\n";
  htmlForm += "<input type=\"submit\" value=\"submit\">\n</form>\n";
  htmlForm +="</div>";
  
  //Parameters of individual switches on this device
  htmlForm +="<div id="switchAttrib" bgcolor='green'><b>\n";
  htmlForm += "<h1> Setup what switches the device controls</h1>\n";
  htmlForm += "<p> Setting a switch incorrectly may damage whatever is connected to it. </p>\n";
  htmlForm += "<form action=\"http://";
  htmlForm.concat( myHostname );
  htmlForm += "/api/v1/switch/setup";
  htmlForm += switchID;
  htmlForm += "/switchAttrib\" method=\"PUT\" id=\"switchAttributes\" >\n";

  htmlForm += "<table>";
  for ( int i = 0 i< numSwitches; i++ )
  {
    htmlForm += "<tr>";
    // unit number
    htmlForm += "<td>";
    htmlForm += i;
    htmlForm += "</td><td>";
    //unit type
    htmlForm += "<select name=\"unitType\" size=\"3\">";
    for ( int j= 0; j< sizeof( switchTypes ); j++ )
    { //  <option value=switchTypes[1]>switchTypes[1]</option>
      htmlForm += "<option value=";
      htmlForm += switchTypes[j]";
      htmlForm += ">";
      htmlForm += switchTypes[j];
      htmlForm += "</option>";
    }
    htmlForm += "</select></td>";
    //unit description
    htmlForm += "<td>";
    
    htmlForm += "</td>";
    //unit min
    //unit max
    //unit value
    //unit increment
    //unit writeable
    htmlForm += "<td>";
    
    htmlForm += "</td>";
    
    htmlForm += "</tr>";
  }

  //Update device::switchID descriptions
  htmlForm += "<h1> Enter new descriptive name for filter wheel</h1>\n";
  htmlForm += "<form action=\"/switch/0/Wheelname\" method=\"PUT\" id=\"wheelname\" >\n";
  htmlForm += "<input type=\"text\" name=\"wheelname\" value=\"";
  htmlForm.concat( wheelName );
  htmlForm += "\">\n";
  htmlForm += "<input type=\"submit\" value=\"submit\">\n</form>\n";
    
  //Switches by position
  htmlForm += "<h1> Enter switch settings for each switch </h1>\n";
  htmlForm += "<form action=\"http://";
  htmlForm.concat(hostname);
  htmlForm += "API/v1/switch/0/FilterNames\" method=\"PUT\" id=\"filternames\" >\n";
  htmlForm += "<ol>\n";
  for ( i=0; i< filtersPerWheel; i++ )
  {
    htmlForm += "<li>Filter name <input type=\"text\" name=\"filtername_";
    htmlForm.concat( i);
    htmlForm += "\" value=\"" + String(filterNames[i]) + "\"></li>\n";
  }
  htmlForm += "</ol>\n";
  htmlForm += "<input type=\"submit\" value=\"submit\">\n</form>\n";
  
  //Filter focus offsets by position
  htmlForm += "<h1> Enter focuser offset for each filter </h1>\n";
  htmlForm += "<form action=\"http://";
  htmlForm.concat(hostname);
  htmlForm += "/FilterWheel/0/FocusOffsets\" method=\"PUT\" id=\"offsets\">\n";
  htmlForm += "<ol>\n";
  for ( i=0; i< filtersPerWheel; i++ )
  {
    htmlForm += "<li> Filter <input type=\"text\" name=\"focusoffset_";
    htmlForm.concat(i);
    htmlForm += "\" value=\"";
    htmlForm.concat( focusOffsets[i] );
    htmlForm += "\"></li>\n";
  }
  htmlForm += "</ol>\n";
  htmlForm += "<input type=\"submit\" value=\"submit\">\n</form>\n";

  */
  htmlForm += "</body>\n</html>\n";

  return htmlForm;
}
#endif
