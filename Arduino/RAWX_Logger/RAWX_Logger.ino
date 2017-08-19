// Logs GNSS RAWX data from u-blox NEO-M8T to SD card

// This code is written for the Adalogger M0 Feather
// https://www.adafruit.com/products/2796
// https://learn.adafruit.com/adafruit-feather-m0-adalogger

// GNSS data is provided by Paul's u-blox NEO-M8T FeatherWing
// https://github.com/PaulZC/NEO-M8T_GNSS_FeatherWing

// Solder the GRESET link so that pressing the reset button
// resets the NEO-M8T as well as the Adalogger.

// Choose a good quality SD card. Some cheap cards can't handle the write rate.
// Ensure the card is formatted as FAT32.

// Connect a normally-open push-to-close switch between swPin and GND.
// Press it to stop logging and close the log file.
#define swPin 15 // Digital Pin 15 (0.2" away from the GND pin on the Adalogger)

// Include the Adafruit GPS Library
// https://github.com/adafruit/Adafruit_GPS
// This is used at the start of the code to establish a fix and
// provide the date and time for the RAWX log file filename
#include <Adafruit_GPS.h>
Adafruit_GPS GPS(&Serial1); // M0 hardware serial
     
// Set GPSECHO to 'false' to turn off echoing the GPS data to the Serial console
// Set to 'true' if you want to debug and listen to the raw GPS sentences
#define GPSECHO false
     
// this keeps track of whether we're using the interrupt
// off by default!
boolean usingInterrupt = false;

// Run tests on the MAX-M8Q or SAM-M8Q using NAV-PVT messages
//#define M8Qtest // Comment out for use on the M8T

// Send serial debug messages
//#define DEBUG

// Define 'packet' size for SD card writes
#define SDpacket 512
byte serBuffer[SDpacket];
int bufferPointer = 0;

// Battery voltage
float vbat;

// SD card logging
#include <SPI.h>
#include <SD.h>
#define cardSelect 4
File rawx_dataFile;
char rawx_filename[] = "20000000/000000.bin";
char dirname[] = "20000000";
boolean fileReady = false;

// Count number of valid fixes before starting to log (to avoid possible corrupt file names)
int valfix = 0;
#define maxvalfix 10 // Collect at least this many valid fixes before logging starts

// Definitions for u-blox M8 UBX-format (binary) messages

// Set Nav Mode to Portable
static const uint8_t setNavPortable[] = {
  0xB5, 0x62, 0x06, 0x24, 0x24, 0x00, 0xFF, 0xFF, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 
  0x05, 0x00, 0xFA, 0x00, 0xFA, 0x00, 0x64, 0x00, 0x2C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10 };
static const int len_setNav = 44;

// Set Nav Mode to Pedestrian
static const uint8_t setNavPedestrian[] = {
  0xB5, 0x62, 0x06, 0x24, 0x24, 0x00, 0xFF, 0xFF, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 
  0x05, 0x00, 0xFA, 0x00, 0xFA, 0x00, 0x64, 0x00, 0x2C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x76 };

// Set Nav Mode to Automotive
static const uint8_t setNavAutomotive[] = {
  0xB5, 0x62, 0x06, 0x24, 0x24, 0x00, 0xFF, 0xFF, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 
  0x05, 0x00, 0xFA, 0x00, 0xFA, 0x00, 0x64, 0x00, 0x2C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x98 };

// Set Nav Mode to Sea
static const uint8_t setNavSea[] = {
  0xB5, 0x62, 0x06, 0x24, 0x24, 0x00, 0xFF, 0xFF, 0x05, 0x03, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 
  0x05, 0x00, 0xFA, 0x00, 0xFA, 0x00, 0x64, 0x00, 0x2C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15, 0xBA };

// Set Nav Mode to Airborne <1G
static const uint8_t setNavAir[] = {
  0xB5, 0x62, 0x06, 0x24, 0x24, 0x00, 0xFF, 0xFF, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 
  0x05, 0x00, 0xFA, 0x00, 0xFA, 0x00, 0x64, 0x00, 0x2C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0xDC };

// Set NMEA Config
// Set trackFilt to 1 to ensure course (COG) is always output
// Set Main Talker ID to 'GP' instead of 'GN'
static const uint8_t setNMEA[] = {
  0xb5, 0x62, 0x06, 0x17, 0x14, 0x00, 0x20, 0x40, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0xd9 };
static const int len_setNMEA = 28;

// Set Navigation/Measurement Rate (CFG-RATE): set measRate to 200msec; align to UTC time
static const uint8_t setRATE[] = { 0xb5, 0x62, 0x06, 0x08, 0x06, 0x00, 0xc8, 0x00, 0x01, 0x00, 0x00, 0x00, 0xdd, 0x68 };
// Set Navigation/Measurement Rate (CFG-RATE): set measRate to 200msec; align to GPS time
//static const uint8_t setRATE[] = { 0xb5, 0x62, 0x06, 0x08, 0x06, 0x00, 0xc8, 0x00, 0x01, 0x00, 0x01, 0x00, 0xde, 0x6a };
// Set Navigation/Measurement Rate (CFG-RATE): set measRate to 1000msec; align to UTC time
static const uint8_t setRATE_1Hz[] = { 0xb5, 0x62, 0x06, 0x08, 0x06, 0x00, 0xe8, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x37 };
static const uint8_t len_setRATE = 14;

#ifdef M8Qtest
// For testing purposes only on non-M8T chipsets: replace RXM-RAWX messages with NAV-PVT
// Set NAV-PVT Message Rate: once per measRate
static const uint8_t setRAWX[] = { 0xb5, 0x62, 0x06, 0x01, 0x03, 0x00, 0x01, 0x07, 0x01, 0x13, 0x51 };
// Set NAV-PVT Message Rate: off
static const uint8_t setRAWXoff[] = { 0xb5, 0x62, 0x06, 0x01, 0x03, 0x00, 0x01, 0x07, 0x00, 0x12, 0x50 };
static const int len_setRAWX = 11;
#else
// Set RXM-RAWX Message Rate: once per measRate
static const uint8_t setRAWX[] = { 0xb5, 0x62, 0x06, 0x01, 0x03, 0x00, 0x02, 0x15, 0x01, 0x22, 0x70 };
// Set RXM-RAWX Message Rate: off
static const uint8_t setRAWXoff[] = { 0xb5, 0x62, 0x06, 0x01, 0x03, 0x00, 0x02, 0x15, 0x00, 0x21, 0x6f };
static const int len_setRAWX = 11;
#endif

void setup()
{
  // initialize digital pins 13 and 8 as outputs.
  pinMode(13, OUTPUT); // Red LED
  pinMode(8, OUTPUT); // Green LED
  // flash red and green LEDs on reset
  for (int i=0; i <= 4; i++) {
    digitalWrite(13, HIGH);
    delay(200);
    digitalWrite(13, LOW);
    digitalWrite(8, HIGH);
    delay(200);
    digitalWrite(8, LOW);
  }

  // initialize swPin as an input for the stop switch
  pinMode(swPin, INPUT_PULLUP);

  delay(10000); // Allow 10 sec for user to open serial monitor
  //while (!Serial); // Wait for user to run python script or open serial monitor

  Serial1.begin(9600);
  Serial.begin(115200);

  Serial.println("Log GNSS RAWX data to SD card");
  Serial.println("Green LED = Initial GPS Fix");
  Serial.println("Red LED Flash = SD Write");
  Serial.println("Continuous Red indicates a problem or that logging has been stopped");

  Serial.println("Initializing GPS...");

  // u-blox M8 Init
  // 9600 is the default baud rate for u-blox M8
  GPS.begin(9600);
  // Change the NEO-M8T UART Baud rate to 115200
  // (DDC (I2C) and SPI rates can be set independantly if required)
  // Allow both NMEA and UBX protocols (but not RTCM)
  // Disable autobauding
  GPS.sendCommand("$PUBX,41,1,0003,0003,115200,0*1C");
  // Allow time for Baud rate change
  delay(1100);
  // Restart serial communications at 115200 Baud
  GPS.begin(115200);
  // Disable all messages except GGA and RMC
  GPS.sendCommand("$PUBX,40,GLL,0,0,0,0*5C"); // Disable GLL
  delay(100);
  GPS.sendCommand("$PUBX,40,ZDA,0,0,0,0*44"); // Disable ZDA
  delay(100);
  GPS.sendCommand("$PUBX,40,VTG,0,0,0,0*5E"); // Disable VTG
  delay(100);
  GPS.sendCommand("$PUBX,40,GSV,0,0,0,0*59"); // Disable GSV
  delay(100);
  GPS.sendCommand("$PUBX,40,GSA,0,0,0,0*4E"); // Disable GSA
  delay(100);
  GPS.sendCommand("$PUBX,40,GGA,0,1,0,0*5B");  // Enable GGA
  delay(100);
  GPS.sendCommand("$PUBX,40,RMC,0,1,0,0*46");  // Enable RMC
  delay(100); // Wait
  Serial1.write(setNavPortable, len_setNav); // Set Portable Navigation Mode
  //Serial1.write(setNavPedestrian, len_setNav); // Set Pedestrian Navigation Mode
  //Serial1.write(setNavAutomotive, len_setNav); // Set Automotive Navigation Mode
  //Serial1.write(setNavSea, len_setNav); // Set Sea Navigation Mode
  //Serial1.write(setNavAir, len_setNav); // Set Airborne <1G Navigation Mode
  delay(100);
  Serial1.write(setNMEA, len_setNMEA); // Set NMEA configuration (use GP prefix instead of GN otherwise parsing will fail)
  delay(100);
  Serial1.write(setRAWXoff, len_setRAWX); // Disable RAWX messages
  delay(100);
  Serial1.write(setRATE_1Hz, len_setRATE); // Set Navigation/Measurement Rate to 1Hz
  delay(1100);
  while(Serial1.available()){Serial1.read();} // Flush RX buffer

  Serial.println("GPS initialized!");

  // flash the red LED during SD initialisation
  digitalWrite(13, HIGH);

  // Initialise SD card
  Serial.println("Initializing SD card...");
  // See if the SD card is present and can be initialized:
  if (!SD.begin(cardSelect)) {
    Serial.println("Panic!! SD Card Init failed, or not present!");
    // don't do anything more:
    while(1);
  }
  Serial.println("SD Card initialized!");

  // turn red LED off
  digitalWrite(13, LOW);
  
}

// Loop Steps
#define init        0
#define write_file  1
#define close_file  2
#define loop_end    3
int loop_step = init;

void loop() // run over and over again
{
  switch(loop_step) {
    case init: {
      // read data from the GPS
      char c = GPS.read();
      // if you want to debug, this is a good time to do it!
      if (GPSECHO)
        if (c) Serial.print(c);
      // if a sentence is received, we can check the checksum, parse it...
      if (GPS.newNMEAreceived()) {
        if (!GPS.parse(GPS.lastNMEA())) // this also sets the newNMEAreceived() flag to false
          break; // we can fail to parse a sentence in which case we should just wait for another
    
#ifdef DEBUG
        Serial.print("\nTime: ");
        Serial.print(GPS.hour, DEC); Serial.print(':');
        Serial.print(GPS.minute, DEC); Serial.print(':');
        Serial.print(GPS.seconds, DEC); Serial.print('.');
        Serial.println(GPS.milliseconds);
        Serial.print("Date: ");
        Serial.print(GPS.day, DEC); Serial.print('/');
        Serial.print(GPS.month, DEC); Serial.print("/20");
        Serial.println(GPS.year, DEC);
        Serial.print("Fix: "); Serial.print((int)GPS.fix);
        Serial.print(" Quality: "); Serial.println((int)GPS.fixquality);
        if (GPS.fix) {
          Serial.print("Location: ");
          Serial.print(GPS.latitude, 4); Serial.print(GPS.lat);
          Serial.print(", ");
          Serial.print(GPS.longitude, 4); Serial.println(GPS.lon);
          Serial.print("Speed (knots): "); Serial.println(GPS.speed);
          Serial.print("Angle: "); Serial.println(GPS.angle);
          Serial.print("Altitude: "); Serial.println(GPS.altitude);
          Serial.print("Satellites: "); Serial.println((int)GPS.satellites);
          Serial.print("HDOP: "); Serial.println(GPS.HDOP);
        }
#endif
  
        // read battery voltage
        vbat = analogRead(A7) * (2.0 * 3.3 / 1023.0);
#ifdef DEBUG
        Serial.print("Battery(V): ");
        Serial.println(vbat, 2);
#endif
      
        // turn green LED on to indicate GPS fix
        if (GPS.fix) {
          digitalWrite(8, HIGH);
          // increment valfix and cap at maxvalfix
          // don't do anything fancy in terms of decrementing valfix as we want to keep logging even if the fix is lost
          valfix += 1;
          if (valfix > maxvalfix) valfix = maxvalfix;
        }
        else {
          digitalWrite(8, LOW);
        }
  
        if (valfix == maxvalfix) { // wait until we have enough valid fixes to avoid possible filename corruption
  
          // do the divides
          char GPShourT = GPS.hour/10 + '0';
          char GPShourU = GPS.hour%10 + '0';
          char GPSminT = GPS.minute/10 + '0';
          char GPSminU = GPS.minute%10 + '0';
          char GPSsecT = GPS.seconds/10 + '0';
          char GPSsecU = GPS.seconds%10 + '0';
          char GPSyearT = GPS.year/10 +'0';
          char GPSyearU = GPS.year%10 +'0';
          char GPSmonT = GPS.month/10 +'0';
          char GPSmonU = GPS.month%10 +'0';
          char GPSdayT = GPS.day/10 +'0';
          char GPSdayU = GPS.day%10 +'0';
  
          // flash red LED to indicate SD write (leave on if an error occurs)
          digitalWrite(13, HIGH);
  
          // syntax checking:
          // check if voltage is > 3.55V, if not then don't try to write!
          if (vbat < 3.55) {
            fileReady = false; // Start new file when battery is recharged
            Serial.println("Low Battery!");
            break;
          }
          // check year
          if ((GPS.year < 16) || (GPS.year > 26)) break;
          // check month
          if (GPS.month > 12) break;
           // check day
          if (GPS.day > 31) break;
    
          // filename is limited to 8.3 characters so use format: YYYYMMDD/HHMMSS.bin
          rawx_filename[2] = GPSyearT;
          rawx_filename[3] = GPSyearU;
          rawx_filename[4] = GPSmonT;
          rawx_filename[5] = GPSmonU;
          rawx_filename[6] = GPSdayT;
          rawx_filename[7] = GPSdayU;
          rawx_filename[9] = GPShourT;
          rawx_filename[10] = GPShourU;
          rawx_filename[11] = GPSminT;
          rawx_filename[12] = GPSminU;
          rawx_filename[13] = GPSsecT;
          rawx_filename[14] = GPSsecU;
          
          dirname[2] = GPSyearT;
          dirname[3] = GPSyearU;
          dirname[4] = GPSmonT;
          dirname[5] = GPSmonU;
          dirname[6] = GPSdayT;
          dirname[7] = GPSdayU;
  
          // try to create subdirectory (even if it exists already)
          SD.mkdir(dirname);
          
          // Open the rawx file for fast writing
          // See http://forum.arduino.cc/index.php?topic=49649.msg354701#msg354701
          rawx_dataFile = SD.open(rawx_filename, O_CREAT | O_WRITE);
          
          // check if the file has opened successfully
          if (rawx_dataFile) {
            Serial.print("Logging to ");
            Serial.println(rawx_filename);
          }
          // if the file isn't open, pop up an error:
          else {
            Serial.println("Error opening RAWX file!");
            break;
          }
          
          GPS.sendCommand("$PUBX,40,GGA,0,0,0,0*5A");  // Disable GGA
          delay(100);
          GPS.sendCommand("$PUBX,40,RMC,0,0,0,0*47");  // Disable RMC
          delay(100);
          Serial1.write(setRATE, len_setRATE); // Set Navigation/Measurement Rate
          delay(1100); // Wait
          while(Serial1.available()){Serial1.read();} // Flush RX buffer
          Serial1.write(setRAWX, len_setRAWX); // Set RXM-RAWX message rate
          while (Serial1.available() < 10) ; // Wait for UBX acknowledgement (10 bytes)
          for (int x=0;x<10;x++) {
            Serial1.read(); // Clear out the acknowledgement
          }
          
          digitalWrite(13, LOW); // turn red LED off
          loop_step = write_file; // start logging rawx data
        }
      }
    }
    break;

    // Stuff bytes into serBuffer and write when we have reached SDpacket
    case write_file: {
      if (Serial1.available()) {
        serBuffer[bufferPointer] = Serial1.read();
        bufferPointer++;
        if (bufferPointer == SDpacket) {
          bufferPointer = 0;
          digitalWrite(13, HIGH); // flash red LED
          rawx_dataFile.write(serBuffer, SDpacket);
          digitalWrite(13, LOW);
        }
      }
      else {
        // read battery voltage
        vbat = analogRead(A7) * (2.0 * 3.3 / 1023.0);
        // check if the stop button has been pressed or battery is low
        if ((digitalRead(swPin) == LOW) or (vbat < 3.55)) {
          if (bufferPointer > 0) {
            rawx_dataFile.write(serBuffer, bufferPointer); // Write remaining data
          }
          loop_step = close_file; // now close the file
          break;
        }
      }
    }
    break;

    // Save any remaining serial data and close the file
    case close_file: {
      Serial1.write(setRAWXoff, len_setRAWX); // Disable messages
      int waitcount = 0;
      // leave 10 bytes in the buffer as this should be the message acknowledgement
      while (waitcount < 1000) {
        if (Serial1.available() > 10) {
          byte c = Serial1.read();
          digitalWrite(13, HIGH); // flash red LED
          rawx_dataFile.write(c);
          digitalWrite(13, LOW);
        }
        else {
          waitcount++;
          delay(1);
        }
      }
      rawx_dataFile.close(); // close the file
      digitalWrite(13, HIGH); // leave the red led on
      loop_step = loop_end;
      Serial.print("File closed!");
    }
    break;
    
    case loop_end: {
      ; // Wait for reset
    }
    break;
  }
}
