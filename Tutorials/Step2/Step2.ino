/**************************************************************************
       Title:   NTP Server tutorial - Step 2
      Author:   Bruce E. Hall, w8bh.net
        Date:   26 Jun 2023
    Hardware:   uBlox GPS module, ESP3266 (Wemos D1 mini)
    Software:   Arduino IDE 2.1.0
       Legal:   Copyright (c) 2023  Bruce E. Hall.
                Open Source under the terms of the MIT License. 
                    
 Description:   Simple Clock using TinyGPS++

                Connect GPS Tx pin to ESP8266 pin D5.
                
                Echoes time in UTC to the serial monitor 
                several times per second.
                           
 **************************************************************************/

#include <SoftwareSerial.h>                   // built-in
#include <TinyGPS++.h>                        // tinyGPSPlus v1.0.3

SoftwareSerial gpsSerial(D5,D6);
TinyGPSPlus gps;

void setup() {
  gpsSerial.begin(9600);                      // GPS Data @ 9600 baud
  Serial.begin(115200);                       // Serial monitor @ 115200 baud 
}


void loop() {
  if (gpsSerial.available()) {                // any data from GPS?
    char c = gpsSerial.read();                // yes, so get the next character
    if (gps.encode(c)) printTime();           // when NMEA string finished, print the time. 
  }
}

void printTime() {
  int h  = gps.time.hour();                   // get the hour
  if (h<10) Serial.print("0");                // make it 2 digits: '6'->'06'
  Serial.print(h); Serial.print(":");         // print the hour  
  int m = gps.time.minute();                  // get the minutes
  if (m<10) Serial.print("0");                // make it 2 digits: '6'->'06'
  Serial.print(m); Serial.print(":");         // print the minutes
  int s = gps.time.second();                  // get the seconds
  if (s<10) Serial.print("0");                // make it 2 digits: '6'->'06'
  Serial.print(s); Serial.println(" UTC");    // print the seconds
}
