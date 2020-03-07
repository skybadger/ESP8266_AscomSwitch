/*
File to define the eeprom variable save/restor operations for the ASCOM switch web driver
*/
#ifndef _WEBRELAY_EEPROM_H_
#define _WEBRELAY_EEPROM_H_

#include "Webrelay_common.h"
#include "DebugSerial.h"
//#include "eeprom.h"
//#include "EEPROMAnything.h"

const byte magic = '*';

//definitions
void setDefaults(void );
void saveToEeprom(void);
void setupFromEeprom(void);

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
  switchEntry = (SwitchEntry**) calloc( sizeof( SwitchEntry* ), numSwitches );
  for ( i=0; i< numSwitches ; i++ )
  {
    switchEntry[i]= (SwitchEntry*) calloc( sizeof( SwitchEntry ), 1 );
  }
  
  for ( i=0; i< numSwitches ; i++ )
  {
    String tempName = "Switch_";

    switchEntry[i]->description = (char*) calloc( MAX_NAME_LENGTH, sizeof(char) );
    strcpy( switchEntry[i]->description, "Default description" );

    tempName.concat( i );
    switchEntry[i]->switchName = (char*) calloc( MAX_NAME_LENGTH, sizeof(char) );
    strcpy( switchEntry[i]->switchName, tempName.c_str() );
    
    switchEntry[i]->type = SWITCH_RELAY_NO;
    switchEntry[i]->min = 0.0F;
    switchEntry[i]->max = 1.0F;
    switchEntry[i]->step = 1.0F;
    switchEntry[i]->value = 0.0F;
    switchEntry[i]->writeable = true; 
  }

  DEBUGSL1( "setDefaults: exiting" );
}

void saveToEeprom( void )
{
  int eepromAddr = 0;
  const char magic = '*';

  DEBUGSL1( "savetoEeprom: Entered ");
   
  //Num Switches
  EEPROMWriteAnything( eepromAddr = 1, numSwitches );
  eepromAddr += sizeof(int);  
  DEBUGS1( "Written numSwitches: ");DEBUGSL1( numSwitches );
  
  //Switch state
  for ( int i = 0; i< numSwitches; i++ )
  {
    EEPROMWriteAnything( eepromAddr, switchEntry[i]->type );
    eepromAddr += sizeof( switchEntry[i]->type );    
    EEPROMWriteAnything( eepromAddr, switchEntry[i]->writeable );
    eepromAddr += sizeof( switchEntry[i]->writeable );    
    EEPROMWriteAnything( eepromAddr, switchEntry[i]->min );
    eepromAddr += sizeof( switchEntry[i]->min );
    EEPROMWriteAnything( eepromAddr, switchEntry[i]->max );
    eepromAddr += sizeof( switchEntry[i]->max );
    EEPROMWriteAnything( eepromAddr, switchEntry[i]->step );
    eepromAddr += sizeof( switchEntry[i]->step );
    EEPROMWriteAnything( eepromAddr, switchEntry[i]->value );
    eepromAddr += sizeof( switchEntry[i]->value );
    
    EEPROMWriteString( eepromAddr, switchEntry[i]->switchName, MAX_NAME_LENGTH );
    eepromAddr += MAX_NAME_LENGTH * sizeof( char);    

    EEPROMWriteString( eepromAddr, switchEntry[i]->description, MAX_NAME_LENGTH );
    eepromAddr += MAX_NAME_LENGTH * sizeof( char);    
  }
  EEPROMWriteAnything( eepromAddr, udpPort );
  eepromAddr += sizeof( udpPort );    
  
  //hostname
  EEPROMWriteString( eepromAddr, myHostname, MAX_NAME_LENGTH );
  eepromAddr += MAX_NAME_LENGTH;  
  
  DEBUGS1( "Written hostname: ");DEBUGSL1( myHostname );

  //Magic number write for first time. 
  EEPROM.put( 0, magic );

  EEPROM.commit();

  //Test readback of contents
  String input = "";
  char ch;
  for ( int i = 0; i < 500 ; i++ )
  {
    ch = (char) EEPROM.read( i );
    if ( ch == '\0' )
      ch = '~';
    if ( (i % 50 ) == 0 )
      input.concat( "\n\r" );
    input.concat( ch );
  }
  
  Serial.printf( "EEPROM contents after: \n %s \n", input.c_str() );
  DEBUGSL1( "saveToEeprom: exiting ");
}

void setupFromEeprom( void )
{
  int eepromAddr = 0;
    
  DEBUGSL1( "setUpFromEeprom: Entering ");
  
  //Setup internal variables - read from EEPROM.
  EEPROM.get( eepromAddr=0, magic );
  DEBUGS1( "Read magic: ");DEBUGSL1( magic );
  
  if ( (byte) magic != '*' ) //initialise
  {
    setDefaults();
    saveToEeprom();
    DEBUGSL1( "Failed to find init magic byte - wrote defaults ");
    return;
  }    
    
  //Num Switches 
  EEPROM.get( eepromAddr = 1, numSwitches );
  eepromAddr  += sizeof(int);
  
  //switch entries
  for ( int i=0; i< numSwitches; i++ )
  {
    EEPROMReadAnything( eepromAddr, switchEntry[i]->type );
    eepromAddr += sizeof( switchEntry[i]->type );
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
    
    if( switchEntry[i]->switchName != nullptr )
      free( switchEntry[i]->switchName );
    switchEntry[i]->switchName = (char*) calloc( MAX_NAME_LENGTH, sizeof( char ) );  
    EEPROMReadString( eepromAddr, switchEntry[i]->switchName, MAX_NAME_LENGTH );
    eepromAddr += MAX_NAME_LENGTH * sizeof( char);    

    if( switchEntry[i]->description != nullptr )
      free( switchEntry[i]->description );
    switchEntry[i]->description = (char*) calloc( MAX_NAME_LENGTH, sizeof( char ) );  
    EEPROMReadString( eepromAddr, switchEntry[i]->description, MAX_NAME_LENGTH );
    eepromAddr += MAX_NAME_LENGTH * sizeof( char);    
  }  

  EEPROMReadAnything( eepromAddr, udpPort );
  eepromAddr += sizeof( udpPort );    

  //hostname - directly into variable array 
  if( myHostname != nullptr )
    free( myHostname );
  myHostname = (char*) calloc( MAX_NAME_LENGTH, sizeof( char ) );  
  EEPROMReadString( eepromAddr, myHostname, MAX_NAME_LENGTH );
  DEBUGS1( "Read hostname: ");DEBUGSL1( myHostname );

  if ( thisID != nullptr ) 
     free ( thisID );
  thisID = (char*) calloc( MAX_NAME_LENGTH, sizeof( char)  );       
  strcpy ( thisID, myHostname );

  DEBUGSL1( "setupFromEeprom: exiting" );
}
#endif
