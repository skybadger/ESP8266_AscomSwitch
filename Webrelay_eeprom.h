/*
File to define the eeprom variable save/restor operations for the ASCOM switch web driver
*/
#ifndef _WEBRELAY_EEPROM_H_
#define _WEBRELAY_EEPROM_H_

#include "Webrelay_common.h"
#include "DebugSerial.h"
#include "eeprom.h"
#include "EEPROMAnything.h"

static const byte magic = '*';

//definitions
void setDefaults(void );
void saveToEeprom(void);
void setupFromEeprom(void);

/*
 * Write default values into variables - don't save yet. 
 */
void setDefaults( void )
{
  int i=0;
  DEBUGSL1( "Eeprom setDefaults: entered");

  if ( myHostname != nullptr ) 
     free ( myHostname );
  myHostname = (char* )calloc( sizeof (char), MAX_NAME_LENGTH );
  strcpy( myHostname, defaultHostname);

  //MQTT thisID copied from hostname
  if ( thisID != nullptr ) 
     free ( thisID );
  thisID = (char*) calloc( MAX_NAME_LENGTH, sizeof( char)  );       
  strcpy ( thisID, myHostname );

  udpPort = ALPACA_DISCOVERY_PORT;
  
  //Allocate storage for Number of Switch settings
  numSwitches = defaultNumSwitches;
  
  //Create array of pointers to switchEntry descriptor blocks
  switchEntry = (SwitchEntry**) calloc( sizeof( SwitchEntry* ), numSwitches );
  
  //Create individual descriptor blocks
  for ( i=0; i< numSwitches ; i++ )
  {
    switchEntry[i] = (SwitchEntry*) calloc( sizeof( SwitchEntry ), 1 );
  }
  
  for ( i=0; i< numSwitches ; i++ )
  {
    String tempName = "Switch_";

    switchEntry[i]->description = (char*) calloc( MAX_NAME_LENGTH, sizeof(char) );
    strcpy( switchEntry[i]->description, "Default description" );

    tempName.concat( i );
    switchEntry[i]->switchName = (char*) calloc( MAX_NAME_LENGTH, sizeof(char) );
    strcpy( switchEntry[i]->switchName, tempName.c_str() );
    switchEntry[i]->writeable = true;    
    switchEntry[i]->type = SWITCH_RELAY_NO;
    switchEntry[i]->pin = 0;
    switchEntry[i]->min = 0.0F;
    switchEntry[i]->max = 1.0F;
    switchEntry[i]->step = 1.0F;
    switchEntry[i]->value = 0.0F;
  }

//#if defined DEBUG
  //Read them back for checking  - also available via status command.
  Serial.printf( "Switches: %i \n" , numSwitches );
  Serial.printf( "Hostname: %s \n" , myHostname );
  Serial.printf( "Discovery port: %i \n" , udpPort );
  for ( i=0;i < numSwitches; i++ )
  {
    String output;
    Serial.printf( "Switch %i: \n" , i) ;
    Serial.printf( "Desc:      %s \n" , switchEntry[i]->description );
    Serial.printf( "Name:      %s \n" , switchEntry[i]->switchName );  
    Serial.printf( "Type:      %i \n", switchEntry[i]->type );
    Serial.printf( "Pin:       %i \n", switchEntry[i]->pin );
    Serial.printf( "Min:       %2.2f \n", switchEntry[i]->min );
    Serial.printf( "Max:       %2.2f \n", switchEntry[i]->max );
    Serial.printf( "Step:      %2.2f \n", switchEntry[i]->step );
    Serial.printf( "Value:     %2.2f \n", switchEntry[i]->value );
    Serial.printf( "Writeable: %i \n", switchEntry[i]->writeable );     
  }
//#endif
  DEBUGSL1( "setDefaults: exiting" );
}

/*
 * Save current variables of interest into EEPROM
 */
void saveToEeprom( void )
{
  int eepromAddr = 0;
  DEBUGSL1( "savetoEeprom: Entered ");
   
  //Num Switches
  EEPROMWriteAnything( eepromAddr = 4, numSwitches );
  eepromAddr += sizeof(int);  
  DEBUGS1( "Written numSwitches: ");DEBUGSL1( numSwitches );
  
  //UDP Port
  EEPROMWriteAnything( eepromAddr, udpPort );
  eepromAddr += sizeof(int);  
  DEBUGS1( "Written udpPort: ");DEBUGSL1( udpPort );

  //hostname
  EEPROMWriteString( eepromAddr, myHostname, MAX_NAME_LENGTH );
  eepromAddr += MAX_NAME_LENGTH;   
  DEBUGS1( "Written hostname: ");DEBUGSL1( myHostname );

  //Switch state
  for ( int i = 0; i< numSwitches; i++ )
  {
    EEPROMWriteAnything( eepromAddr, (int) switchEntry[i]->type );
    eepromAddr += sizeof( (int) switchEntry[i]->type );    
    
    EEPROMWriteAnything( eepromAddr, (int) switchEntry[i]->pin );
    eepromAddr += sizeof( (int) switchEntry[i]->pin );    
    
    EEPROMWriteAnything( eepromAddr, (bool) switchEntry[i]->writeable );
    eepromAddr += sizeof( (bool) switchEntry[i]->writeable );    
    
    EEPROMWriteAnything( eepromAddr, (float) switchEntry[i]->min );
    eepromAddr += sizeof( (float) switchEntry[i]->min );
    
    EEPROMWriteAnything( eepromAddr, (float) switchEntry[i]->max );
    eepromAddr += sizeof( (float) switchEntry[i]->max );
    
    EEPROMWriteAnything( eepromAddr, (float) switchEntry[i]->step );
    eepromAddr += sizeof( (float) switchEntry[i]->step );
    
    EEPROMWriteAnything( eepromAddr, (float) switchEntry[i]->value );
    eepromAddr += sizeof( (float) switchEntry[i]->value );
    
    //Name
    EEPROMWriteString( eepromAddr, switchEntry[i]->switchName, MAX_NAME_LENGTH );
    eepromAddr += (MAX_NAME_LENGTH * sizeof( char));    
    
    //Description
    EEPROMWriteString( eepromAddr, switchEntry[i]->description, MAX_NAME_LENGTH );
    eepromAddr += (MAX_NAME_LENGTH * sizeof( char));    
  }

  //Magic number write for data write complete. 
  EEPROM.put( 0, magic );
  EEPROM.commit();
  DEBUGS1( "Wrote ");DEBUGS1(eepromAddr);DEBUGSL1( " bytes ");
   
  //Test readback of contents
  String input = "";
  char ch;
  for ( int i = 0; i < 600 ; i++ )
  {
    ch = (char) EEPROM.read( i );
    if ( ch == '\0' )
      ch = '~';
    if ( (i % 32 ) == 0 )
      input.concat( "\n\r" );
    input.concat( ch );
  }
  
  Serial.printf( "EEPROM contents after: \n %s \n", input.c_str() );
  DEBUGSL1( "saveToEeprom: exiting ");
}

void setupFromEeprom( void )
{
  int eepromAddr = 0;
  int i = 0;
    
  DEBUGSL1( "setUpFromEeprom: Entering ");
  byte myMagic = '\0';
  //Setup internal variables - read from EEPROM.
  myMagic = EEPROM.read( 0 );
  DEBUGS1( "Read magic: ");DEBUGSL1( (char) myMagic );
  
  if ( (byte) myMagic != magic ) //initialise eeprom for first time use. 
  {
    setDefaults();
    saveToEeprom();
    DEBUGSL1( "Failed to find init magic byte - wrote defaults & restarted.");
    device.restart();
  }    
    
  //Num Switches 
  EEPROMReadAnything( eepromAddr = 4, numSwitches );
  eepromAddr  += sizeof(int);
  DEBUGS1( "Read numSwitches: ");DEBUGSL1( numSwitches );
  
  //UDP port 
  EEPROMReadAnything( eepromAddr, udpPort );
  eepromAddr  += sizeof(int);  
  DEBUGS1( "Read UDPport: ");DEBUGSL1( udpPort );

  //hostname - directly into variable array 
  if( myHostname != nullptr )
    free( myHostname );
  myHostname = (char*) calloc( MAX_NAME_LENGTH, sizeof( char ) );  
  EEPROMReadString( eepromAddr, myHostname, MAX_NAME_LENGTH );
  eepromAddr  += MAX_NAME_LENGTH * sizeof(char);  
  DEBUGS1( "Read hostname: ");DEBUGSL1( myHostname );

  //Setup MQTT client id based on hostname
  if ( thisID != nullptr ) 
     free ( thisID );
  thisID = (char*) calloc( MAX_NAME_LENGTH, sizeof( char)  );       
  strcpy ( thisID, myHostname );
  DEBUGS1( "Read MQTT ID: ");DEBUGSL1( thisID );
  
  //free old switch entries
  if ( switchEntry != nullptr )
  {
     for ( i= 0; i < numSwitches; i++ )
        if( switchEntry[i] != nullptr ) 
          free ( switchEntry[i] );
     free( switchEntry);
  }
  
  //Create new
  switchEntry = (SwitchEntry**) calloc ( sizeof (SwitchEntry*), numSwitches );
  for ( i=0; i< numSwitches; i++ )
  {
    switchEntry[i] = (SwitchEntry*) calloc( sizeof( SwitchEntry ), 1 );
    if( switchEntry[i] == nullptr )
    {
      //something has gone wrong - probably run out of memory space. 
      //Alert somehow .       
      break;
    }
    EEPROMReadAnything( eepromAddr, switchEntry[i]->type );
    eepromAddr += sizeof( switchEntry[i]->type );
    
    EEPROMReadAnything( eepromAddr, switchEntry[i]->pin );
    eepromAddr += sizeof( switchEntry[i]->pin );
    
    EEPROMReadAnything( eepromAddr, switchEntry[i]->writeable );
    eepromAddr += sizeof( switchEntry[i]->writeable );    
    
    EEPROMReadAnything( eepromAddr, switchEntry[i]->min );
    eepromAddr += sizeof( switchEntry[i]->min );
    
    EEPROMReadAnything( eepromAddr, switchEntry[i]->max );
    eepromAddr += sizeof( switchEntry[i]->max );
    
    EEPROMReadAnything( eepromAddr, switchEntry[i]->step );
    eepromAddr += sizeof( switchEntry[i]->step );
    
    EEPROMReadAnything( eepromAddr, switchEntry[i]->value );
    eepromAddr += sizeof( switchEntry[i]->value );
    
    switchEntry[i]->switchName = (char*) calloc( MAX_NAME_LENGTH, sizeof( char ) );  
    EEPROMReadString( eepromAddr, switchEntry[i]->switchName, MAX_NAME_LENGTH );
    eepromAddr += MAX_NAME_LENGTH * sizeof( char);    

    switchEntry[i]->description = (char*) calloc( MAX_NAME_LENGTH, sizeof( char ) );  
    EEPROMReadString( eepromAddr, switchEntry[i]->description, MAX_NAME_LENGTH );
    eepromAddr += MAX_NAME_LENGTH * sizeof( char);    
  }  

  DEBUGSL1( "setupFromEeprom: exiting" );
}
#endif
