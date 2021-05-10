/*
ESP8266_relayhandler.h
This is a firmware application to implement the ASCOM ALPACA switch interface API.
Each device can manage more than one switch - up to numswitches which is user configured.
Each nominal switch can be 1 of 4 types - binary relays (no and nc) and digital pwm and DAC outputs.
Hence the setup allows specifying the number of switches per device and the host/device name.
This particular switch device assumes the use of port pins allocated from the device via a pcf8574 I2C port expansion device. 
Using an ESP8266-01 this leaves one pin free to be a digital device. 
Using an ESP8266-12 this leaves a number of pins free to be a digital device.
Use of a DAC requires (presumably) an i2C DAC or an onboard DAC tied to a pin.

In use of the ASCOM Api 
All URLs include an argument 'Id' which contains the number of the switch attached to this device instance
The switch device number is in the path itself. Hence the getUriField function
Internally this code keeps the state of the switch in the switchEntry structure and uses (value != 1.0F) to mean false. 

 To do:
 Debug, trial
 
 Layout: 
 (ESP8266-12)
 GPIO 4,2 to SDA
 GPIO 5,0 to SCL 
 (ESP8266-01)
 GPIO 0 - SDA
 GPIO 1 - Rx - re-use as PWM output for testing purposes
 GPIO 2 - SCL
 GPIO 3 - Tx
 All 3.3v logic. 
  
*/

#ifndef _ESP8266_RELAYHANDLER_H_
#define _ESP8266_RELAYHANDLER_H_

#include "Webrelay_eeprom.h"
#include <Wire.h>
#include "AlpacaErrorConsts.h"
#include "ASCOMAPISwitch_rest.h"


//Function definitions
void copySwitch( SwitchEntry* sourceSe, SwitchEntry* targetSe );
void initSwitch( SwitchEntry* targetSe );
SwitchEntry** reSize( SwitchEntry** old, int newSize );
bool getUriField( char* inString, int searchIndex, String& outRef );
String& setupFormBuilder( String& htmlForm, String& errMsg );

void handlerMaxswitch(void);
void handlerCanWrite(void);
void handlerSwitchState(void);
void handlerSwitchDescription(void);
void handlerSwitchName(void);

/*
 * This function will write a copy of the provided deviceEntry structure into the internal memory array. 
 * switch Entry may be a pointer to a single item or an array of items. ID must be in range;
 */
void copySwitch( SwitchEntry* sourceSe, SwitchEntry* targetSe )
{
    strncpy( targetSe->description, sourceSe->description, MAX_NAME_LENGTH);
    strncpy( targetSe->switchName, sourceSe->switchName, MAX_NAME_LENGTH);
    targetSe->writeable   = sourceSe->writeable;
    targetSe->type        = SWITCH_RELAY_NO;
    targetSe->min         = sourceSe->min;
    targetSe->max         = sourceSe->max;
    targetSe->step        = sourceSe->step;
    targetSe->value       = sourceSe->value;
    targetSe->pin         = sourceSe->pin;
}

void initSwitch( SwitchEntry* targetSe )
{
    String output;
    output = "Default description";
    strncpy( targetSe->description, output.c_str(), MAX_NAME_LENGTH);

    output = "Switch Name";
    strncpy( targetSe->switchName, output.c_str(), MAX_NAME_LENGTH);

    targetSe->writeable   = true;
    targetSe->type        = SWITCH_RELAY_NO;
    targetSe->min         = 0.0F;
    targetSe->max         = 0.0F;
    targetSe->step        = 1.0F;
    targetSe->value       = 0.0F;
    targetSe->pin         = NULLPIN;
}
/*
 * This function re-sizes the existing switchEntry array by re-allocating memory and copying if required. 
 * returns pointer to array of switches
 */
SwitchEntry** reSize( SwitchEntry** old, int newSize )
{
  int i=0;
  SwitchEntry* newse;
  SwitchEntry** pse = (SwitchEntry** ) calloc( sizeof (SwitchEntry*),  newSize );
  
  DEBUGS1( "reSize called: " );DEBUGSL1( newSize );

  if ( newSize < numSwitches )
  {
    for ( i=0 ; i < newSize; i++ )
      pse[i] = old[i];      
    //Handle the remaining 
    for ( ; i < numSwitches; i++ )
    {
      if( old[i]->description != nullptr ) 
        free( old[i]->description );
      if( old[i]->switchName != nullptr ) free( old[i]->switchName );
        free( old[i] );
    }
  }
  else if ( newSize == numSwitches)
  {
    //do nothing
    free(pse);
    pse = old;
  }
  else //bigger than before
  {
    for ( i = 0; i < numSwitches; i++ )
       pse[i] = old[i];      
    for ( ; i < newSize; i++ )
    {
      newse = (SwitchEntry*) calloc( sizeof (SwitchEntry), 1 );
      newse->description = (char*)calloc( MAX_NAME_LENGTH, sizeof(char) );
      newse->switchName = (char*)calloc( MAX_NAME_LENGTH, sizeof(char) );
      pse[i] = newse;
      initSwitch( newse );
    }
  }
  numSwitches = newSize;
  DEBUGS1("reSize completed, numSwitches updated to");DEBUGSL1(newSize);
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

    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, "CanWrite", Success , "" );    

    if( hasArgIC( argToSearchFor, server, false ) )
    {
      switchID = server.arg(argToSearchFor).toInt();
      if ( switchID >= 0 && switchID < numSwitches ) 
      {
        root["Value"] = switchEntry[switchID]->writeable;
        statusCode = 200;
      }
      else
      {
        statusCode = 400;
        root["ErrorMessage"] = "Argument switch Id out of range";
        root["ErrorNumber"] = (int) invalidValue ; 
      }
    }
    else
    {
        statusCode = 400;
        root["ErrorMessage"] = "Missing switchID argument";
        root["ErrorNumber"] = (int) invalidOperation ;       
    }
    root.printTo(message);
    server.send( statusCode , "text/json", message);
    return ;
}

//GET ​/switch​/{device_number}​/getswitch
//PUT ​/switch​/{device_number}​/setswitch
//Get/Set the state of switch device id as a boolean - treats as binary
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
    else
    {
       String output = "Missing argument: switchID";
       root["ErrorNumber"] = invalidOperation;
       root["ErrorMessage"] = output; 
       returnCode = 400;
       server.send(returnCode, "text/json", message);
       return;
    }
 
    DEBUGS1( "SwitchID:"); DEBUGSL1( switchID); 
    if ( switchID >= 0 && switchID < numSwitches )
    {
      if( server.method() == HTTP_GET  )
      {
        switch ( switchEntry[switchID]->type ) 
        {
          case SWITCH_RELAY_NO:
          case SWITCH_RELAY_NC:
            switchValue = switchEntry[switchID]->value;
            if ( switchValue != 1.0F ) 
              bValue = false;
            else 
              bValue = true;      
            root["Value"] =  bValue;  
            returnCode = 200;
            break;
          case SWITCH_PWM:
          case SWITCH_ANALG_DAC:
          default:
            returnCode = 400;
            root["ErrorMessage"] = "Invalid state retrieval for switch type - not boolean"  ;
            root["ErrorNumber"] = invalidValue ;
          break;
        }
      }
      else if (server.method() == HTTP_PUT && hasArgIC( argToSearchFor[1], server, false ) )
      {
        switch( switchEntry[switchID]->type )
        {
          case SWITCH_RELAY_NO:
          case SWITCH_RELAY_NC:
              DEBUGSL1( "Found relay to set");
              if( server.arg( argToSearchFor[1] ).equalsIgnoreCase( "true" ) )
                newState = true;
              else
                newState = false;
              if( reverseRelayLogic )
                switchDevice.write( switchID, (newState) ? 0 : 1 );
              else
                switchDevice.write( switchID, (newState) ? 1 : 0 );
              switchEntry[switchID]->value = (newState)? 1.0F : 0.0F;
              returnCode = 200;              
            break;
          case SWITCH_PWM:
          case SWITCH_ANALG_DAC:
          default:
            returnCode = 400;
            root["ErrorMessage"] = "Invalid state for non-boolean switch type"  ;
            root["ErrorNumber"] = invalidOperation ;
            break;
        }
      } 
      else
      {
         String output = "";
         Serial.println( "Error: method not available" );
         root["ErrorNumber"] = invalidOperation;
         output = "http verb:";
         output += server.method();
         output += " not available";
         root["ErrorMessage"] = output; 
         returnCode = 400;
      }  
    }
    else
    {
        returnCode = 400;
        root["ErrorMessage"] = "Invalid switch ID as argument";
        root["ErrorNumber"] = invalidValue ;
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
    {
      switchID = server.arg( argToSearchFor ).toInt();
      if( switchID >=0 && switchID < numSwitches )
          root["Value"] = switchEntry[switchID]->description;
      else
      {
         root["ErrorNumber"] = invalidValue;
         root["ErrorMessage"] = "Out of range argument: switchID"; 
         returnCode = 400;    
      }
    }  
    else
    {
       root["ErrorNumber"] = invalidOperation;
       root["ErrorMessage"] = "Missing argument: switchID"; 
       returnCode = 400;    
    }

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
    jsonResponseBuilder( root, clientID, transID, "SwitchName", Success, "" );    
    
    if( hasArgIC( argToSearchFor[0], server, false ) )
    {
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
            int sLen = strlen( server.arg( argToSearchFor[1] ).c_str() );
            if ( sLen > MAX_NAME_LENGTH -1 )
            {
              root["ErrorMessage"]= "Switch name too long";
              root["ErrorNumber"] = invalidValue ;
              returnCode = 400;
            }
            else
            {
              if ( switchEntry[switchID]->switchName != nullptr ) 
                free( switchEntry[switchID]->switchName );
              switchEntry[switchID]->switchName  = (char*) calloc( MAX_NAME_LENGTH, sizeof(char) );
              strcpy( switchEntry[switchID]->switchName, server.arg( argToSearchFor[1] ).c_str() );
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
    }
    else
    {
      //invalid switch id 
      returnCode = 400;
      root["ErrorMessage"]= "Missing switch ID";
      root["ErrorNumber"] = invalidOperation ;
    }

    root.printTo(message);
    server.send(returnCode, "text/json", message);
    return;
}

//Non-ascom function
//GET ​/switch​/{device_number}​/getswitchtype
//PUT ​/switch​/{device_number}​/setswitchtype
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
    {
      switchID = server.arg(argToSearchFor[0]).toInt();
    }  
    else
    {
       returnCode = 400;
       root["ErrorMessage"]= "Missing switchID argument";
       root["ErrorNumber"] = invalidValue ;     
       root.printTo(message);
       server.send(returnCode, "text/json", message);
       return;
    }
     
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
         root["ErrorMessage"]= "Invalid HTTP verb or arguments found";
         root["ErrorNumber"] = invalidOperation ;
      }
    }
    else
    {
       returnCode = 400;
       root["ErrorMessage"]= "Argument switchID out of range";
       root["ErrorNumber"] = invalidValue ;
    }
    root.printTo(message);
    server.send(returnCode, "text/json", message);
    return;
}

//GET ​/switch​/{device_number}​/getswitchvalue
//PUT ​/switch​/{device_number}​/setswitchvalue
//Get/Set the value of the specified switch device as a double - ie not a binary setting
void handlerSwitchValue(void)
{
    String message;
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    int returnCode = 200;
    float value = 0.0F;
    uint32_t switchID = 0;
    String argToSearchFor[2] = {"Id", "Value"};
    
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, "SwitchValue", 0, "" );    
    
    if ( hasArgIC( argToSearchFor[0], server, false ) )
    {
      switchID = server.arg( argToSearchFor[0] ).toInt();
    }
    else
    {
      root["ErrorMessage"] = "Missing argument - switchID ";
      root["ErrorNumber"] = invalidValue ;
      returnCode = 400;      
      root.printTo(message);
      server.send(returnCode, "text/json", message);
      return;      
    }
      
    if ( switchID >= 0 && switchID < (uint32_t) numSwitches )
    {
        if( server.method() == HTTP_GET )
        {
          switch( switchEntry[switchID]->type )
          {
            case SWITCH_PWM: 
                  //analogWrite( switchEntry[i]->pin, output );
                  root["Value"] = switchEntry[switchID]->value;
                  break;
            case SWITCH_ANALG_DAC:
                  //e.g. analogue_write( 256, 15);
            //Not supported yet - need to be able to add pin mapping to this & requires
            //More pins than a simple ESP8266 & PCF8574A combo
                    root["Value"] = switchEntry[switchID]->value;
                  returnCode = 200;
                  break;                
            case SWITCH_RELAY_NO:
            case SWITCH_RELAY_NC:
                  returnCode = 400;
                  root["ErrorMessage"] = "Invalid analogue operation for binary/boolean switch type - use getSwitch";
                  root["ErrorNumber"] = invalidOperation ;
                  break;
            default:
              break;           
          }
        }
        else if( server.method() == HTTP_PUT && hasArgIC( argToSearchFor[1], server, false ) )
        {
          value = (float) server.arg( argToSearchFor[1] ).toDouble();
          switch( switchEntry[switchID]->type ) 
          {
            case SWITCH_PWM: 
                  if ( value >= switchEntry[switchID]->min && 
                       value <= switchEntry[switchID]->max )
                  {
                    switchEntry[switchID]->value = value;
                    analogWrite( switchEntry[switchID]->pin, switchEntry[switchID]->value );
                    returnCode = 200;
                  }
                  else
                  {
                    root["ErrorMessage"] = "Digital write out of range for switch";
                    root["ErrorNumber"] = invalidOperation ;
                    returnCode = 200;
                  }                  
                  break;
                  
            case SWITCH_RELAY_NO:
            case SWITCH_RELAY_NC:
                  returnCode = 400;
                  root["ErrorMessage"] = "Invalid analogue operation for binary/boolean switch type";
                  root["ErrorNumber"] = invalidOperation ;
                  break;
            case SWITCH_ANALG_DAC:
                  if ( value >= switchEntry[switchID]->min && 
                       value <= switchEntry[switchID]->max )
                  {
                    switchEntry[switchID]->value = value;
                    //e.g. analogue_write( switchEntry[switchID]->pin, switchEntry[switchID]->pin );
                    root["ErrorMessage"] = "DAC Not implemented yet - Invalid digital operation for switch";
                    root["ErrorNumber"] = invalidOperation ;
                  }
                  returnCode = 200;
                  break;
            default:
              break;
          }
        }
        else
        {
           returnCode = 400;
           root["ErrorMessage"] = "Invalid HTTP verb method for this URI or missing output value";
           root["ErrorNumber"] = invalidOperation ;
        }
    }
    else
    {
      root["ErrorMessage"] = "SwitchID value out of range.";
      root["ErrorNumber"] = invalidValue ;
      returnCode = 200;
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
    int switchID  = -1;
    String argToSearchFor = "id";
    
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, "MinSwitchValue", Success, "" );    
    
    if ( hasArgIC( argToSearchFor, server, false  ) )
    {
      switchID = server.arg( argToSearchFor ).toInt();
      if( switchID >= 0 && switchID < numSwitches )
      {  
        root.set<double>("Value", switchEntry[switchID]->min );
      }
      else
      {
        root["ErrorMessage"] = "SwitchID value out of range.";
        root["ErrorNumber"] = invalidValue ;
        returnCode = 400;        
      }
    }
    else
    {
      root["ErrorMessage"] = "SwitchID argument missing .";
      root["ErrorNumber"] = invalidOperation ;
      returnCode = 400;
    }
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
    int switchID  = -1;
    String argToSearchFor = "Id";
    
    DynamicJsonBuffer jsonBuffer(256);
    JsonObject& root = jsonBuffer.createObject();
    jsonResponseBuilder( root, clientID, transID, "MaxSwitchValue", Success, "" );    

    if ( hasArgIC(argToSearchFor, server, false ) )
    {
      switchID = server.arg(argToSearchFor).toInt();
      if ( switchID >= 0 && switchID < numSwitches )
      {
        root.set<double>("Value", switchEntry[switchID]->max );
        returnCode = 200;
      }
      else
      {
        root["ErrorMessage"] = "SwitchID value out of range.";
        root["ErrorNumber"] = invalidValue ;
        returnCode = 400;              
      }
    }
    else
    {
       root["ErrorMessage"] = "Missing switchID argument.";
       root["ErrorNumber"] = invalidOperation  ;
       returnCode = 400;
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
    jsonResponseBuilder( root, clientID, transID, "SwitchStep", Success, "" );    

    if ( hasArgIC(argToSearchFor, server, false ) )
    {
      switchID = server.arg(argToSearchFor).toInt();
      if( switchID >= 0 && switchID < (uint32_t) numSwitches ) 
      {
        root.set<double>("Value", switchEntry[switchID]->step );        
        returnCode = 200;
      }
      else
      {
         root["ErrorMessage"] = "SwitchID out of range.";
         root["ErrorNumber"] = invalidValue ;
         returnCode = 400;
      }
    }
    else
    {
       root["ErrorMessage"] = "Missing switchID argument.";
       root["ErrorNumber"] = invalidOperation ;
       returnCode = 400;    
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

void handlerRestart()
{
  String message;
  int responseCode = 200;
  message.concat( "restarting on user request" );
  server.send(responseCode, "text/plain", message);
  device.restart();
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
    root["connected"] = (connected)?"true":"false";
    root["clientId"] = connectedClient;
    
    for( i = 0; i < numSwitches; i++ )
    {
      //Can I re-use a single object or do I need to create a new one each time? 
      JsonObject& entry = jsonBuffer.createObject();
      entry["description"] = switchEntry[i]->description;
      entry["name"]        = switchEntry[i]->switchName;
      entry["type"]        = (int) switchEntry[i]->type;      
      entry["pin"]         = (int) switchEntry[i]->pin;      
      entry.set("writeable", switchEntry[i]->writeable );
      entry["min"]         = switchEntry[i]->min;
      entry["max"]         = switchEntry[i]->max;
      entry["step"]        = switchEntry[i]->step;
      if( switchEntry[i]->type == SWITCH_RELAY_NO || switchEntry[i]->type == SWITCH_RELAY_NC )
      {
        entry["state"]     = (switchEntry[i]->value == 1.0F ) ? true : false ;
      }
      else 
        entry["value"]     = switchEntry[i]->value; //Needs check limits to 1-1024, DAC and PWM limits. 
      entries.add( entry );
    }
    Serial.println( message);
    root.prettyPrintTo(message);
    server.send(returnCode, "text/json", message);
    return;
}

/*
 * Handlers to do custom setup that can't be done without a windows ascom driver setup form. 
 */
 void handlerSetup(void)
 {
   String message, timeString, err= "";
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;
    
    int returnCode = 400;
    if ( server.method() == HTTP_GET )
    {
        message = setupFormBuilder( message, err );      
        server.send( returnCode, "text/html", message ); 
    }
 }
 
 /*
  * Handler to update the hostname from the form.
  */
 void handlerSetupHostname(void) 
 {
    String message, timeString, err= "";
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;
    
    int returnCode = 400;
    String argToSearchFor[] = { "hostname", "numSwitches"};
     
    if ( server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET)
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
          else
             err = "New name too long";

          message = setupFormBuilder( message, err );      
          returnCode = 200;    
          saveToEeprom();
          server.send(returnCode, "text/html", message);
          device.reset();
        }
    }
    else
    {
      returnCode=400;
      err = "Bad HTTP request verb";
      message = setupFormBuilder( message, err );      
    }
    server.send(returnCode, "text/html", message);
    return;
 }

 /*
  * Handle update of the number of attached switches
  */
 void handlerSetupNumSwitches(void) 
 {
    String message, timeString, err= "";
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    uint32_t switchID = -1;
    
    int returnCode = 400;
    String argToSearchFor[] = { "numSwitches"};
     
    if ( server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET )
    {
        if( hasArgIC( argToSearchFor[0], server, false )  )
        {
          //process form variables.
          int newNumSwitches = server.arg(argToSearchFor[0]).toInt();
          if( newNumSwitches == 0 || newNumSwitches > MAXSWITCH )
          {
            err = "Switch size exceeds device range or is zero.";
            returnCode = 400;
          }
          else
          {
            //update the switches
            switchEntry = reSize( switchEntry, newNumSwitches );            
            numSwitches = newNumSwitches;
            saveToEeprom();
            returnCode = 200;              
          }

          message = setupFormBuilder( message, err );  
          server.send( returnCode, "text/html", message);
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

 /*
  * lengthy function to take the setup form output for each switch to manage its control parameters.
  * We don't attempt to set its value here. We just set the type, limits, features etc. The main API controls its use.
  */
 void handlerSetupSwitches(void) 
 {
    String message, timeString, err= "";
    uint32_t clientID = (uint32_t)server.arg("ClientID").toInt();
    uint32_t transID = (uint32_t)server.arg("ClientTransactionID").toInt();
    int i;
    int returnCode = 200;
    String argToSearchFor[9] = { "switchId", "name", "description", "type", "writeable", "pin", "min", "max", "step" };
    
    DEBUGS1("Entered handlerSetupSwitches");
    if ( server.method() == HTTP_POST || server.method() == HTTP_PUT || server.method() == HTTP_GET )
    {
      //Expecting an array of variables in form submission
      //Need to parse and handle
      if( hasArgIC( argToSearchFor[0], server, false ) )
      {
        String arg; 
        String name = "default_name", description="default_description";
        double min = 0.0, max=0.0, step=0.0;
        int pin = NULLPIN;
        enum SwitchType type = SWITCH_RELAY_NO;
        bool  writeable = false;        
        bool allFound = true;
        int id = -1;
        
        //Get the id of the current switch to be updated 
        id = (int) server.arg( argToSearchFor[0] ).toInt();
        DEBUGS1("handlerSwitchSetup - found switchId ");DEBUGSL1( id );
        
        //Check for all args present 
        int i=1;
        int foundCount = 0;
        for ( i=1; i < 9; i++ ) 
        {
          arg = argToSearchFor[i] + id;
          DEBUGS1("handlerSwitchSetup - arg sought: ");DEBUGSL1(arg.c_str());

          if( hasArgIC( arg, server, false )  )
          {
            DEBUGS1("handlerSwitchSetup - arg found ");DEBUGSL1(arg.c_str());
            DEBUGS1("handlerSwitchSetup - foundCount ");DEBUGSL1(foundCount);
            allFound &= true;
            foundCount ++;
          }  
          else 
          {
            allFound &= false;          
          }
        }                    
  
        //4 is the number of variables expected for an analogue switch entry, 8 for a digital. 
        if( !allFound && foundCount == 4 ) 
        {
          enum SwitchType localType = (enum SwitchType) server.arg( argToSearchFor[3] + id ).toInt();
          DEBUGS1( "handlerSwitchSetup: looking for type: ");DEBUGSL1( localType );
          
          if ( ( localType == SWITCH_RELAY_NC ) || ( localType == SWITCH_RELAY_NO ) )
          {
            DEBUGSL1( "handlerSwitchSetup: found analogue switch type - only expecting 4 variables");
            //Actually we're all good - the disable form variables are not passed when disabled.
            //That means we only see 4 rather than all 8 of them.                
            allFound = true;
            //Cache the existing values so they don't get overwritten with defaults. 
            min = switchEntry[id]->min;
            max = switchEntry[id]->max;
            step = switchEntry[id]->step;
            pin = switchEntry[id]->pin;
          }
          else
          {
            DEBUGSL1( "handlerSwitchSetup: type not found for analogue switch type - can't validate the fewer variables found");
          }
        }
        else if ( !allFound && foundCount != 4 ) 
        {
          DEBUGS1("handlerSwitchSetup - not all args found - failed at ");DEBUGSL1( i );
          err = "Not all form variables supplied";
          returnCode = 0x402; //check
        }
  
        //Get the values and sense-check
        //Switch name
        DEBUGSL1("handlerSwitchSetup - all args found - checking values ");
        arg = argToSearchFor[1] + id;
        if( hasArgIC( arg, server, false ) && returnCode == 200 )
        {
          name = server.arg( arg );
          if ( name.length() >= MAX_NAME_LENGTH  ) 
          {
            err = "Name longer than max allowed ";//Should be prevented by form controls.
            returnCode = 0x401;
            DEBUGSL1("handlerSwitchSetup - switch name field not successfully parsed");            
          }
        }
          
        //Switch description
        arg = argToSearchFor[2] + id;
        if( hasArgIC( arg, server, false ) && returnCode == 200 )
        {
          description = server.arg( arg );
          if ( description.length() >= MAX_NAME_LENGTH  ) 
          {
            err = "Description longer than max allowed ";//Should be prevented by form controls.
            returnCode = 0x403;
            DEBUGSL1("handlerSwitchSetup - switch description field not successfully parsed");            
          }
        }
  
        //Switch type
        arg = argToSearchFor[3] + id;
        if( hasArgIC( arg, server, false ) && returnCode == 200 )
        {
          String typeAsString = server.arg( arg );
          bool found = false;
          DEBUGS1("handlerSwitchSetup - switch type field found :");DEBUGSL1( typeAsString  );
          type = SWITCH_NOT_SELECTED;
          for ( int j=0; j < 5 && !found; j++ )
          {
            if ( switchTypes[j].compareTo( typeAsString ) == 0 )
            {
              found = true;              
              type = (enum SwitchType) j;
              DEBUGS1("handlerSwitchSetup - switch type field matched :");DEBUGSL1( type  );
            }
          }
          if ( !found )
          {
            DEBUGSL1("handlerSwitchSetup - switch type field NOT understood");
            returnCode = 0x403;
            ;//
          }
          
          if ( !( type == SWITCH_RELAY_NC || 
                  type == SWITCH_RELAY_NO || 
                  type == SWITCH_PWM || 
                  type == SWITCH_ANALG_DAC ) ) 
          {
            err = "Type not yet supported";//Should be prevented by form controls.
            returnCode = 0x400;
            DEBUGSL1("handlerSwitchSetup - switch type field not successfully parsed");
          }
        }
        
        //Switch writeable
        arg = argToSearchFor[4] + id;
        if( hasArgIC( arg, server, false ) && returnCode == 200 )
        {
          writeable = (bool) server.arg( arg ).equalsIgnoreCase( "on" );
          DEBUGS1("handlerSwitchSetup - writeable value found:");DEBUGSL1( writeable );
        }
        //If writeable is not checked, chrome doesn't send it as a form variable - hence its missing. 
        //So we have to assume its false if missing. Solved by replacing a checkbox with a pre-checked radio  
        else 
          writeable = false;
        
        if( foundCount == 4 && allFound ) 
        {
          //Switch pin
          arg = argToSearchFor[5] + id;
          if( hasArgIC( arg, server, false ) && returnCode == 200 )
          {
            pin = (int) server.arg( arg ).toInt();
            if( type == SWITCH_PWM || type == SWITCH_ANALG_DAC )
            {
              if ( ( pin != NULLPIN ) && !( pin >= MINPIN && pin <= MAXPIN ) )
              {
                err = "Digital Pin value not in range";//Should be prevented by form controls.
                returnCode = 0x403;
                DEBUGSL1("handlerSwitchSetup - switch pin field not successfully parsed");
              }
            }
            else 
            {
              if ( pin != NULLPIN ) 
              {
                err = "Binary switches don't have non-null pin numbers";//Should be prevented by form controls.
                returnCode = 0x403;
                DEBUGSL1("handlerSwitchSetup - switch pin field should be NULLPIN");              
              }
            }
          }
    
          //Switch min
          arg = argToSearchFor[6] + id;
          if( hasArgIC( arg, server, false ) && returnCode == 200  )
          {
            min = server.arg( arg ).toDouble();            
            if( type == SWITCH_PWM || type == SWITCH_ANALG_DAC )
            {
              if ( ( min < MINVAL ) || ( min > MAXDIGITALVAL ) ) 
              {
                err = "Min value out of digital range";//Should be prevented by form controls.
                returnCode = 0x400;
                DEBUGSL1("handlerSwitchSetup - switch max field not successfully parsed OOR for type");
              }
            }
            else //Binary switches 
            {
              if( ( min < MINVAL ) || ( min > MAXBINARYVAL ) ) 
              {
                  err = "Min value out of binary/analogue range";//Should be prevented by form controls.
                  returnCode = 0x400;
                  DEBUGSL1("handlerSwitchSetup - switch min field not in range");                
              }
            }
          }
    
          //Switch max
          arg = argToSearchFor[7] + id;
          if( hasArgIC( arg, server, false ) && returnCode == 200 )
          {
            max = (float) server.arg( arg ).toDouble();
            if( type == SWITCH_PWM || type == SWITCH_ANALG_DAC )
            {
              if ( ( max < MINVAL ) || ( max > MAXDIGITALVAL ) ) 
              {
                err = "Max value out of digital range";//Should be prevented by form controls.
                returnCode = 0x400;
                DEBUGSL1("handlerSwitchSetup - switch max field not in digital range");              
              }
            }
            else 
            {
              if( ( max < MINVAL) || ( max > MAXBINARYVAL ) ) 
              {
                  err = "Max value out of binary/analogue range";//Should be prevented by form controls.
                  returnCode = 0x400;
                  DEBUGSL1("handlerSwitchSetup - switch max field not in analogue range");                            
              }
            }
          }
    
          //Switch step
          arg = argToSearchFor[8] + id;
          if( hasArgIC( arg, server, false ) && returnCode == 200 )
          {
            step = (float) server.arg( arg ).toDouble();
            if( type == SWITCH_PWM || type == SWITCH_ANALG_DAC )
            {
              if ( step < MINVAL || step > MAXDIGITALVAL ) 
              {
                err = "Max step value out of digital range";//Should be prevented by form controls.
                returnCode = 0x400;
                DEBUGSL1("handlerSwitchSetup - switch step field not in digital range");                            
              }
            }
            else 
            {
              if( ( step < MINVAL ) || ( step > MAXBINARYVAL ) ) 
              {
                  err = "Step value out of binary/analogue range";//Should be prevented by form controls.
                  returnCode = 0x400;
                  DEBUGSL1("handlerSwitchSetup - switch step field not in analogue range");                              
              }
            }            
          }
        }//end of exclusion for disabled fields based on switch types. 

      /*
      Apply some logic to the combinations of values to check for errors
      Not checking for :  duplicates (names/description/pins)
                          step values are reasonable.
      */
        if ( returnCode == 200 )
        {
          DEBUGSL1("handlerSwitchSetup - named fields found and parsed OK");

          if ( ( min > max ) )
          {
            returnCode = 0x402;
            err = "Bad values : check min <= max ";              
            DEBUGSL1("handlerSwitchSetup - switch min/max/step field not in range");              
          }
          else //Save the values found - they look OK otherwise
          {
            DEBUGSL1("handlerSwitchSetup - saving new values");
            strcpy( switchEntry[id]->switchName, name.c_str() );
            strcpy( switchEntry[id]->description, description.c_str() );
            switchEntry[id]->max = max;
            switchEntry[id]->min = min;
            switchEntry[id]->step = step;
            switchEntry[id]->type = (enum SwitchType ) type;
            switchEntry[id]->pin = pin;
            switchEntry[id]->writeable = writeable;
            //Update current value to fall within new range 
            switchEntry[id]->value = switchEntry[id]->min;
            //Save the new setup
            saveToEeprom();
            returnCode = 200;
          }         
        }
      }
      else
      {
        err = "Switch Id ('switchId') missing ";
        returnCode = 401; //check
      }                         
    }//End of GET/POST/PUT
    else //Not an acceptable HTML verb
    { 
      err = "Not a supported HTML verb (PUT/GET/POST)";
      returnCode = 403; //check    
    }

    DEBUGSL1("handlerSwitchSetup - leaving ");

    //Finally - respond. 
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
  
/*
<!DocType html>
<html lang=en >
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<link rel="stylesheet" href="https://maxcdn.bootstrapcdn.com/bootstrap/4.3.1/css/bootstrap.min.css">
<script src="https://ajax.googleapis.com/ajax/libs/jquery/3.4.1/jquery.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/popper.js/1.14.7/umd/popper.min.js"></script>
<script src="https://maxcdn.bootstrapcdn.com/bootstrap/4.3.1/js/bootstrap.min.js"></script>
<script>function setTypes( a ) {   var searchFor = "types"+a;  var x = document.getElementById(searchFor).value;  if( x.indexOf( "PWM" ) > 0 || x.indexOf( "DAC" ) > 0 )  {      document.getElementById("pin"+a).disabled = false;      document.getElementById("min"+a).disabled = false;      document.getElementById("max"+a).disabled = false;      document.getElementById("step"+a).disabled = false;  }  else  {      document.getElementById("pin"+a).disabled = true;      document.getElementById("min"+a).disabled = true;      document.getElementById("max"+a).disabled = true;      document.getElementById("step"+a).disabled = true;  }}</script>
</meta>
<style>
legend { font: 10pt;}
h1 { margin-top: 0; }
form {
    margin: 0 auto;
    width: 500px;
    padding: 1em;
    border: 1px solid #CCC;
    border-radius: 1em;
}
div+div { margin-top: 1em; }
label span {
    display: inline-block;
    width: 150px;
    text-align: right;
}
input, textarea {
    font: 1em sans-serif;
    width: 150px;
    box-sizing: border-box;
    border: 1px solid #999;
}
input[type=checkbox], input[type=radio], input[type=submit] {
    width: auto;
    border: none;
}
input:focus, textarea:focus { border-color: #000; }
textarea {
    vertical-align: top;
    height: 5em;
    resize: vertical;
}
fieldset {
    width: 410px;
    box-sizing: border-box;
    border: 1px solid #999;
}
button { margin: 20px 0 0 124px; }
label {  position: relative; }
label em {
  position: absolute;
  right: 5px;
  top: 20px;
}
</style>
</head>
*/
 
  htmlForm = "<!DocType html><html lang=en ><head><meta charset=\"utf-8\">";
  htmlForm += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  htmlForm += "<link rel=\"stylesheet\" href=\"https://maxcdn.bootstrapcdn.com/bootstrap/4.3.1/css/bootstrap.min.css\">";
  htmlForm += "<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/3.4.1/jquery.min.js\"></script>";
  htmlForm += "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/popper.js/1.14.7/umd/popper.min.js\"></script>";
  htmlForm += "<script src=\"https://maxcdn.bootstrapcdn.com/bootstrap/4.3.1/js/bootstrap.min.js\"></script>";
  
  //Used to enable/disable the input fields for binary relays vs digital PWM and DAC outputs. 
  htmlForm += "<script>function setTypes( a ) { \
  var searchFor = \"type\"+a;\
  var x = document.getElementById(searchFor).value;\
  if( x.indexOf( \"PWM\" ) >= 0 || x.indexOf( \"DAC\" ) >= 0 )\
  {\
      document.getElementById(\"pin\"+a).disabled = false;\
        document.getElementById(\"pin\"+a).min=";
  htmlForm += MINPIN;
  htmlForm += "; document.getElementById(\"pin\"+a).max=";
  htmlForm += MAXPIN;
  htmlForm += "; document.getElementById(\"min\"+a).disabled=false;";
  htmlForm += "document.getElementById(\"min\"+a).min=";
  htmlForm += MINVAL;
  htmlForm += "; document.getElementById(\"min\"+a).max=";
  htmlForm += MAXDIGITALVAL;
  htmlForm += "; document.getElementById(\"max\"+a).disabled=false;\
        document.getElementById(\"max\"+a).min=";
  htmlForm += MINVAL;
  htmlForm += "; document.getElementById(\"max\"+a).max=";
  htmlForm += MAXDIGITALVAL;
  htmlForm += "; document.getElementById(\"step\"+a).disabled=false;\
        document.getElementById(\"step\"+a).min=";
  htmlForm += MINVAL;
  htmlForm += "; document.getElementById(\"step\"+a).max = ";
  htmlForm += MAXDIGITALVAL;
  htmlForm += "}\n\
  else\n\
  {\
      document.getElementById(\"pin\"+a).disabled = true;\      
        document.getElementById(\"pin\"+a).value=";
        htmlForm += NULLPIN;
        htmlForm += "; ";
        htmlForm += "document.getElementById(\"pin\"+a).min=0;\
          document.getElementById(\"pin\"+a).max=";
        htmlForm += NULLPIN;
        htmlForm += "; ";
      htmlForm +=" document.getElementById(\"min\"+a).disabled = true;\
        document.getElementById(\"min\"+a).value=";
        htmlForm += MINVAL;
        htmlForm += "; ";
        htmlForm += "document.getElementById(\"min\"+a).min=";
        htmlForm += MINVAL;
        htmlForm += "; ";
        htmlForm += "document.getElementById(\"min\"+a).max=";
        htmlForm += MAXBINARYVAL;
        htmlForm += "; ";
      htmlForm += "document.getElementById(\"max\"+a).disabled = true;\
          document.getElementById(\"max\"+a).value=";  
        htmlForm += MAXBINARYVAL;
        htmlForm += "; ";
        htmlForm += "document.getElementById(\"max\"+a).min=";
        htmlForm += MINVAL;
        htmlForm += "; ";
        htmlForm += "document.getElementById(\"max\"+a).max=";
        htmlForm += MAXBINARYVAL;
        htmlForm += "; ";
      htmlForm += "; document.getElementById(\"step\"+a).disabled = true;\  
        document.getElementById(\"step\"+a).value = 0.0;\  
        document.getElementById(\"step\"+a).min = 0.0;\  
        document.getElementById(\"step\"+a).max = 1.0;\  
  }\
}</script>";
  htmlForm += "<style>\
legend { font: 10pt;}\
h1 { margin-top: 0; }\
form { margin: 0 auto; width: 500px;padding: 1em;border: 1px solid #CCC;border-radius: 1em;}\
div+div { margin-top: 1em; }\
label span{display: inline-block;width: 150px;text-align: right;}\
input, textarea {font: 1em sans-serif;width: 150px;box-sizing: border-box;border: 1px solid #999;}\
input[type=checkbox], input[type=radio], input[type=submit]{width: auto;border: none;}\
input:focus,textarea:focus{border-color:#000;}\
textarea {vertical-align: top;height: 5em;resize: vertical;}\
fieldset {width: 410px;box-sizing: border-box;border: 1px solid #999;}\
button {margin: 20px 0 0 124px;}\
label {position:relative;}\
label em { position: absolute;right: 5px;top: 20px;}\
</style>";

  htmlForm += "</head>";
  htmlForm += "<body><div class=\"container\">";
  htmlForm += "<div class=\"row\" id=\"topbar\" bgcolor='A02222'>";
  htmlForm += "<p> This is the setup page for the <a href=\"http://www.skybadger.net\">Skybadger</a> <a href=\"https://www.ascom-standards.org\">ASCOM</a> Switch device '";
  htmlForm += myHostname;
  htmlForm += "' which uses the <a href=\"https://www.ascom-standards.org/api\">ALPACA</a> v1.0 API</b>";
  htmlForm += "</div>";

  if( errMsg != NULL && errMsg.length() > 0 ) 
  {
    htmlForm += "<div class=\"row\" id=\"errorbar\" bgcolor='lightred'>";
    htmlForm += "<b>Error Message</b>";
    htmlForm += "</div>";
    htmlForm += "<hr>";
  }
 
/*
<body>
<div class="container">
<div class="row" id="topbar" bgcolor='A02222'><p> This is the setup page for the Skybadger <a href="https://www.ascom-standards.org">ASCOM</a> Switch device 'espASW01' which uses the <a href="https://www.ascom-standards.org/api">ALPACA</a> v1.0 API</b></div>

<!--<div class="row" id="udpDiscoveryPort" bgcolor='lightblue'>-->
<p> This device supports the <a href="placeholder">ALPACA UDP discovery API</a> on port: 32227 </p>
<p> It is not yet configurable.</p>
<!-- </div> -->
*/

  //UDP Discovery port 
  htmlForm += "<p> This device supports the <a href=\"placeholder\">ALPACA UDP discovery API</a> on port: ";
  htmlForm += udpPort;
  htmlForm += "</p> <p> It is not yet configurable.</p>";

  //Device instance number
  
//<div class="row">
//<h2> Enter new hostname for device</h2><br>
//<p>Changing the hostname will cause the device to reboot and may change the IP address!</p>
//</div>
//<div class="row float-left" id="deviceAttrib" bgcolor='blue'>
//<form method="POST" id="hostname" action="http://espASW01/sethostname">
//<label for="hostname" > Hostname </label>
//<input type="text" name="hostname" maxlength="25" value="espASW01"/>
//<input type="submit" value="Update" />
//</form>
//</div>

  //Device settings hostname and number of switches on this device
  htmlForm += "<div class=\"row\">";
  htmlForm += "<div class=\"col-sm-12\"><h2> Enter new hostname for device</h2><br/></div>";
  htmlForm += "<p>Changing the hostname will cause the device to reboot and may change the IP address!</p></div>";
  htmlForm += "<div class=\"row float-left\" id=\"deviceAttrib\" bgcolor='blue'>\n";
  htmlForm += "<form method=\"POST\" id=\"hostname\" action=\"http://";
  htmlForm.concat( myHostname );
  htmlForm += "/setup/hostname\">";
  htmlForm += "<input type=\"text\" name=\"hostname\" maxlength=\"25\" value=\"";
  htmlForm.concat( myHostname );
  htmlForm += "\"/>";
  htmlForm += "<label for=\"hostname\" > Hostname </label>";
  htmlForm += "<input type=\"submit\" value=\"Update\" />";
  htmlForm += "</form></div>";

//<div class="row float-left">
//<h2>Configure switches</h2><br>
//<p>Editing this to add switch components ('upscaling') will copy the existing setup to the new setup but you will need to edit the added switches. </p><p>Editing this to reduce the number of switch components ('downscaling') will delete the configuration for the switches dropped but retain those lower number switch configurations for further editing</p><br>
//</div>
//<div class="row float-left">
//<form action="http://espASW01/setswitchcount" method="POST" id="switchcount" ><label for="numSwitches" >Number of switch components</label>
//<input type="number" name="numSwitches" min="1" max="16" value="8">
//<input type="submit" value="Update"> </form> </div>

  htmlForm += "<div class=\"row float-left\"> ";
  htmlForm += "<div class=\"col-sm-12\">";
  htmlForm += "<h2>Configure switches</h2><br/>";
  htmlForm += "<p>Editing this to add switch components ('upscaling') will copy the existing setup to the new setup but you will need to edit the added switches. </p>";
  htmlForm += "<p>Editing this to reduce the number of switch components ('downscaling') will delete the configuration for the switches dropped but retain those lower number switch configurations for further editing</p><br></div></div>";

  htmlForm += "<div class=\"row float-left\"><div class=\"col-sm-12\">";
  htmlForm += "<form action=\"http://";
  htmlForm.concat( myHostname );
  htmlForm += "/setup/numswitches\" method=\"POST\" id=\"switchcount\" >";
  htmlForm += "<label for=\"numSwitches\" >Number of switch components</label>";
  htmlForm += "<input type=\"number\" name=\"numSwitches\" min=\"1\" max=\"16\" value=\"";
  htmlForm += numSwitches;
  htmlForm += "\">";
  htmlForm += "<input type=\"submit\" value=\"Update\"> </form> </div></div>";

//<div class="row float-left"> 
//<h2>Switch configuration </h2><br>
//<p>To configure the switch types and limits, select the switch you need below.</p>
//</div>

  htmlForm += "<div class=\"row float-left\"> ";
  htmlForm += "<h2>Switch configuration </h2>";
  htmlForm += "<br><p>To configure the switche types and limits, select the switch you need below.</p></div>";
  
//<div class="row float-left"> 
//<form action="/api/v1/switch/0/setupswitch" Method="PUT">
//<input type="hidden" value="0" name="switchID" />
//<legend>Settings Switch 0</legend>
//<label for="fname0"><span>Switch Name</span></label><input type="text" id="fname0" name="fname0" value="Switch_0" maxlength="25"><br>
//<label for="lname0"><span>Description</span></label><input type="text" id="lname0" name="lname0" value="Default description" maxlength="25"><br>
//<label for="type0"><span>Type</span></label><select id="type0" name="type0" onChange="setTypes( 0 )">
//<option value="Relay_NC">Relay (NC)</option>
//<option value="Relay_NO">Relay (NO)</option>
//<option value="PWM">PWM</option>
//<option value="DAC">DAC</option></select> <br>
//<label for="pin0"><span>Hardware pin</span></label><input disabled type="number" id="pin0" name="pin0" value="0" min="0" max="16"><br>
//<label for="min0"><span>Min value</span></label><input type="number" id="min0" name="min0" value="0.00" min="0.0" max="1.0" disabled><br>
//<label for="max0"><span>Max value</span></label><input type="number" id="max0" name="max0" value="1.00" min="0.0" max="1.0" disabled><br>
//<label for="step0"><span>Steps in range</span></label><input type="number" id="step0" name="step0" value="1.00" min="0" max="1024" disabled ><br>
//<label for="writeable0"><span>Writeable</span></label><input type="radio" id="writeable0" name="writeable0" value="0" >
//<br>
//<input type="submit" value="Update" align="center">

  //List the switches
  for ( int i=0; i< numSwitches; i++ )
  {
  htmlForm += "<div class=\"row float-left\">";
  htmlForm += "<form action=\"/setup/switches\" Method=\"PUT\">";
  htmlForm += "<input type=\"hidden\" value=\"";
  htmlForm += i;
  htmlForm += "\" name=\"switchId\" />";

  htmlForm += "<legend>Settings Switch ";
  htmlForm += i;
  htmlForm += "</legend>";

  //Name
  htmlForm += "<label for=\"name";
  htmlForm += i;
  htmlForm += "\"><span>Switch Name<span></label>";
  htmlForm += "<input type=\"text\" id=\"fname";
  htmlForm += i;
  htmlForm += "\" name=\"name";
  htmlForm += i;
  htmlForm += "\" value=\"";
  htmlForm += switchEntry[i]->switchName;
  htmlForm += "\" maxlength=\"25\"><br>";
     
  //Description
  htmlForm += "<label for=\"description";
  htmlForm += i;
  htmlForm += "\"><span>Description<span></label>";
  htmlForm += "<input type=\"text\" id=\"lname";
  htmlForm += i;
  htmlForm += "\" name=\"description";
  htmlForm += i;
  htmlForm += "\" value=\"";
  htmlForm += switchEntry[i]->description;
  htmlForm += "\" maxlength=\"25\"><br>";
  
  //Type - Hardware implementation detail exposed for configuration
  htmlForm += "<label for=\"type";
  htmlForm += i;
  htmlForm += "\"><span>Switch type</span></label>";
  htmlForm += "<select id=\"type";
  htmlForm += i;
  htmlForm += "\" name=\"type";
  htmlForm += i;
  htmlForm += "\" onChange=\"setTypes( ";
  htmlForm += i;
  htmlForm += " )\">";
  for( int k=0; k < 4; k++ ) 
  {
    htmlForm += "<option value=\"";
    htmlForm += switchTypes[k].c_str();
    htmlForm += "\" ";
    if ( ( (int) switchEntry[i]->type ) == k )
      htmlForm += "selected ";
    htmlForm += ">";
    htmlForm += switchTypes[k].c_str();
    htmlForm += "</option>";
  }
  htmlForm += "\"</select> <br>";

  //Pin - Hardware implementation detail exposed for configuration
  htmlForm += "<label for=\"pin";
  htmlForm += i;
  htmlForm += "\"><span>Hardware pin</span></label>";
  htmlForm += "<input ";
  if( switchEntry[i]->type == SWITCH_RELAY_NC || switchEntry[i]->type == SWITCH_RELAY_NO ) 
  {
    htmlForm += "disabled" ;
  }
  htmlForm += " type=\"number\" id=\"pin";
  htmlForm += i;
  htmlForm += "\" name=\"pin";
  htmlForm += i;
  htmlForm += "\" value=\"";
  htmlForm += switchEntry[i]->pin;
  htmlForm += "\" min=\"";
  htmlForm += MINPIN;  
  htmlForm += "\" max=\"";
  htmlForm += MAXPIN;
  htmlForm += "\" ><br>";  
  
  //Min value for switch 
  htmlForm += "<label for=\"min";
  htmlForm += i;
  htmlForm += "\"><span>Switch min value</span></label>";
  htmlForm += "<input type=\"number\" id=\"min";
  htmlForm += i;
  htmlForm += "\" name=\"min";
  htmlForm += i;
  htmlForm += "\" value=\"";
  htmlForm += switchEntry[i]->min;
  htmlForm += "\" min=\"";
  if( switchEntry[i]->type == SWITCH_RELAY_NC || switchEntry[i]->type == SWITCH_RELAY_NO ) 
  {
    htmlForm += MINVAL;  
    htmlForm += "\" max=\"";
    htmlForm += MAXBINARYVAL;
    htmlForm += "\" disabled><br>";  
  }
  else
  {
    htmlForm += MINVAL;  
    htmlForm += "\" max=\"";
    htmlForm += MAXDIGITALVAL;
    htmlForm += "\"><br>";  
  }

  //Max value for switch
  htmlForm += "<label for=\"max";
  htmlForm += i;
  htmlForm += "\"><span>Max value</span></label>";
  htmlForm += "<input type=\"number\" id=\"max";
  htmlForm += i;
  htmlForm += "\" name=\"max";
  htmlForm += i;
  htmlForm += "\" value=\"";
  htmlForm += switchEntry[i]->max;
  htmlForm += "\" min=\"";
  if( switchEntry[i]->type == SWITCH_RELAY_NC || switchEntry[i]->type == SWITCH_RELAY_NO ) 
  {
    htmlForm += MINVAL;  
    htmlForm += "\" max=\"";
    htmlForm += MAXBINARYVAL;
    htmlForm += "\" disabled><br>";  
  }
  else
  {
    htmlForm += MINVAL;  
    htmlForm += "\" max=\"";
    htmlForm += MAXDIGITALVAL;
    htmlForm += "\" ><br>";  
  }
  
  //Step - number of steps in range for switch
  htmlForm += "<label for=\"step";
  htmlForm += i;
  htmlForm += "\"><span>Steps in range</span></label>";
  htmlForm += "<input type=\"number\" id=\"step";
  htmlForm += i;
  htmlForm += "\" name=\"step";
  htmlForm += i;
  htmlForm += "\" value=\"";
  htmlForm += switchEntry[i]->step;
  htmlForm += "\" min=\"";
  if( switchEntry[i]->type == SWITCH_RELAY_NC || switchEntry[i]->type == SWITCH_RELAY_NO ) 
  {
    htmlForm += MINVAL;  
    htmlForm += "\" max=\"";
    htmlForm += MAXBINARYVAL;
    htmlForm += "\" disabled ><br>";  
  }
  else
  {
    htmlForm += MINVAL;  
    htmlForm += "\" max=\"";
    htmlForm += MAXDIGITALVAL;
    htmlForm += "\" ><br>";  
  }

  //Writeable
  htmlForm += "<label for=\"writeable";
  htmlForm += i;
  htmlForm += "\"><span>Writeable</span></label>";
  htmlForm += " &nbsp; <input type=\"radio\" value=\"on\" id=\"writeable";
  htmlForm += i;
  htmlForm += "\" name=\"writeable";
  htmlForm += i;
  htmlForm += "\""; 
  if( switchEntry[i]->writeable )
    htmlForm += " checked";
  htmlForm += ">";
  htmlForm += "<label for=\"writeableB";
  htmlForm += i;
  htmlForm += "\"><span>ReadOnly</span></label>";
  htmlForm += " &nbsp; <input type=\"radio\" value=\"off\" id=\"writeableB";
  htmlForm += i;
  htmlForm += "\" name=\"writeable";
  htmlForm += i;
  htmlForm += "\""; 
  if( !switchEntry[i]->writeable )
    htmlForm += " checked";
  htmlForm += "> <br>";
 
  htmlForm += "<input type=\"submit\" value=\"Update\">";
  htmlForm += "</fieldset> "; 
  htmlForm += "</form></div>";
  } //endfor
  
/*
</form>
</div>
</body></html>
*/

  htmlForm += "</div></body></html>";
  return htmlForm;
}
#endif
