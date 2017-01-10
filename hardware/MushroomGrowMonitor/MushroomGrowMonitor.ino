/**
 * MushroomGrowMonitor
 * For more information goto: https://github.com/funvill/MushroomGrowMonitor/ 
 * 
 * 
 * Last updated: 2017 Jan 09  
 *
 * Requirments 
 * - ESP8266 
 * - DS1820b sensor connected to D2 
 * 
 * Steps 
 * - Connect to wifi. 
 * - Read the sensors 
 * - Post the sensors values to an online data store
 *
 * ToDo: 
 * - Control fan, heater, mister.    
 *
 */ 
 
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <OneWire.h>


// WiFiManager library
// https://github.com/tzapu/WiFiManager
#include <EEPROM.h>             // Allowes for write to the EPROM
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>    


// Version 
static const unsigned short VERSION_MAJOR = 1 ; 
static const unsigned short VERSION_MINOR = 0 ;
static const unsigned short VERSION_PATCH = 2 ;

// Settings 
static const unsigned int  SETTING_BAUDRATE       = 115200 ;
static const unsigned char SETTING_ONEWIRE_BUS    = D2 ; // D2 
static const bool SETTING_ONLINE_ENABLE           = true ;
static const char SETTING_ONLINE_POLLING_DELAY    = 60 ; 
static const char SETTING_ONLINE_PRIVATE_KEY[]    = "XXXXXXXXXXXXXXX" ;
static const char SETTING_ONLINE_HOST[]           = "data.abluestar.com" ;

/**
 * Busy LED, this LED is brought out to the front of the device and is also on the 
 * top of the huzzah board. This is used to give error codes and status messages. 
 * under normal working procedure it should blink once a second.  
 */ 
static const char HARDWARE_PIN_BUSY             = BUILTIN_LED  ;
static const char HARDWARE_BUSY_ON              = HIGH ;
static const char HARDWARE_BUSY_OFF             = LOW ;

// Consts 
static const unsigned char ONE_WIRE_SERIAL_LENGTH = 8 ; 
#define USE_SERIAL Serial


OneWire  ds( SETTING_ONEWIRE_BUS ); 
HTTPClient httpClient;

void setup() {
  // start serial port:
  USE_SERIAL.begin(SETTING_BAUDRATE);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // Give some time for the chip and the sensors to start up. 
  delay(1000);
  USE_SERIAL.println("FYI: Starting up....");

  
  pinMode(HARDWARE_PIN_BUSY, OUTPUT);  
  digitalWrite(HARDWARE_PIN_BUSY, HARDWARE_BUSY_ON);

  // Print gateway version. 
  USE_SERIAL.print("FYI: Version: ");
  USE_SERIAL.print(VERSION_MAJOR);
  USE_SERIAL.print(".");
  USE_SERIAL.print(VERSION_MINOR);
  USE_SERIAL.print(".");
  USE_SERIAL.print(VERSION_PATCH);
  USE_SERIAL.println();

  // Generate the device name 
  char deviceName[32] ; 
  sprintf( deviceName, "esp%d", ESP.getChipId() ); 
  
  USE_SERIAL.print("Chip: ");
  USE_SERIAL.print( deviceName); 
  USE_SERIAL.println();


    // WiFiManager
  // Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  wifiManager.autoConnect(deviceName);
  
  // Print the wifi connection details. 
  USE_SERIAL.println("[WiFi] WiFi connected");

  // All done. 
  USE_SERIAL.println("FYI: Done start up.");   
  USE_SERIAL.println(""); 
  digitalWrite(HARDWARE_PIN_BUSY, HARDWARE_BUSY_OFF);
  

}



bool httpGetRequest( String serial, String value ) {
  if( ! SETTING_ONLINE_ENABLE ) {
    USE_SERIAL.println("FYI: Posting to online server, disabled.");
    return false;   
  }



  // USE_SERIAL.println("==============================");

  // Construct the URL 
  String url = "";
  url += "/PHPDataLogger/?method=post&format=json&value=";
  url += value ; 
  url += "&name=";
  url += serial ; 
  url += "&private_key=";
  url += String( SETTING_ONLINE_PRIVATE_KEY ) ; 

  httpClient.begin(SETTING_ONLINE_HOST, 80, url);
  int httpCode = httpClient.GET();
  if(httpCode > 0) {
      USE_SERIAL.printf("[HTTP] GET... code: %d\n", httpCode);

      // file found at server
      if(httpCode == 200) {
          USE_SERIAL.print("[HTTP] "); 
          httpClient.writeToStream(&USE_SERIAL);
      }
  } else {
      USE_SERIAL.printf("[HTTP] GET... failed, error: %d\n", httpCode);
  }
  httpClient.end();
  USE_SERIAL.println("");
}


void GetOneWireValues() {
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte addr[8];
  float celsius, fahrenheit;

  // Count how many 1Wire sensors on on the bus 
  // While searching for new ones. 
  static unsigned short oneWireSensorsFound = 0; 
  if ( !ds.search(addr)) {
    if( oneWireSensorsFound <= 0 ) {
      USE_SERIAL.println("Error: No 1Wire devices found on the bus.");
    } else {
      USE_SERIAL.print("FYI: Total sensors found = ");
      USE_SERIAL.println(oneWireSensorsFound);
    }
    oneWireSensorsFound = 0 ; 
    ds.reset_search();
    delay(250);
    GetOneWireValues(); 
    return;
  }

  // We found a new 1Wire Sensor
  oneWireSensorsFound++;   
  USE_SERIAL.print("FYI: Address = ");
  for( i = 0; i < 8; i++) {
    if( addr[i] < 16) { 
      USE_SERIAL.print("0");
    }
    USE_SERIAL.print(addr[i], HEX);
  }
   
  if (OneWire::crc8(addr, 7) != addr[7]) {
      USE_SERIAL.println("Error: CRC is not valid!");
      return;
  }
  
  
  // the first ROM byte indicates which chip
  switch (addr[0]) {
    case 0x10:
      // USE_SERIAL.print(", Chip = DS18S20");  // or old DS1820
      type_s = 1;
      break;
    case 0x28:
      // USE_SERIAL.print(", Chip = DS18B20");
      type_s = 0;
      break;
    case 0x22:
      // USE_SERIAL.print(", Chip = DS1822");
      type_s = 0;
      break;
    default:
      USE_SERIAL.print("Error: Device is not a DS18x20 family device.");
      return;
  } 
  
  ds.reset();
  ds.select(addr);
  // start conversion, with parasite power on at the end
  ds.write(0x44, 1); 
  
  // maybe 750ms is enough, maybe not we might do a ds.depower() here, but the reset will take care of it.
  delay(1000);     
  
  present = ds.reset();
  ds.select(addr);    
  ds.write(0xBE); // Read Scratchpad
  for ( i = 0; i < 9; i++) {  // we need 9 bytes
    data[i] = ds.read();
  }
  
  
  // Print the data 
  /*
  USE_SERIAL.print(", Data = ");
  for ( i = 0; i < 9; i++) {  
    if( data[i] < 16) { 
      USE_SERIAL.print("0");  
    }
    USE_SERIAL.print(data[i], HEX);
  }
  USE_SERIAL.print(", CRC=");
  USE_SERIAL.print(OneWire::crc8(data, 8), HEX);
  */
  
  // Convert the data to actual temperature
  // because the result is a 16 bit signed integer, it should
  // be stored to an "int16_t" type, which is always 16 bits
  // even when compiled on a 32 bit processor.
  int16_t raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
  }
  celsius = (float)raw / 16.0;  
  USE_SERIAL.print(", ");
  USE_SERIAL.print(celsius);
  USE_SERIAL.print(" Celsius, ");
  // fahrenheit = celsius * 1.8 + 32.0;
  // USE_SERIAL.print(fahrenheit);
  // USE_SERIAL.println(" Fahrenheit");
  USE_SERIAL.println("");

   // Build the serial string. 
  String serialString ;
  for( int serialOffset = 0 ; serialOffset < ONE_WIRE_SERIAL_LENGTH ; serialOffset++ ) {
    if( addr[serialOffset] < 16 ) {
      serialString += String("0");
    }
    serialString += String(addr[serialOffset], HEX );
  }  

  httpGetRequest( serialString, String( celsius ) ) ;
}


void TogleLED() {  
  static int ledState = HARDWARE_BUSY_OFF;
  if (ledState == HARDWARE_BUSY_ON) {
    ledState = HARDWARE_BUSY_OFF; 
  } else {
    ledState = HARDWARE_BUSY_ON; 
  }
}

void loop() {
  digitalWrite(HARDWARE_PIN_BUSY, HARDWARE_BUSY_ON);
  GetOneWireValues(); 
  digitalWrite(HARDWARE_PIN_BUSY, HARDWARE_BUSY_OFF);

  USE_SERIAL.printf("Waiting %d seconds... \n", SETTING_ONLINE_POLLING_DELAY);
  delay(1000 * SETTING_ONLINE_POLLING_DELAY );
}
