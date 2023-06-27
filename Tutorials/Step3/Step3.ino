/**************************************************************************
       Title:   NTP Server tutorial - Step 3
      Author:   Bruce E. Hall, w8bh.net
        Date:   26 June 2023
    Hardware:   uBlox GPS module, ESP3266 (Wemos D1 mini)
    Software:   Arduino IDE 2.1.0
       Legal:   Copyright (c) 2023  Bruce E. Hall.
                Open Source under the terms of the MIT License. 
                    
 Description:   Adds an OLED display to Step2 code.
                OLED will display Adafruit logo only.

                Connect the ESP8266 "D1" pin to OLED "SCL"
                Connect the ESP8266 "D2" pin to OLED "SDA"
                
                As in Step2, UTC time echoed to serial monitor 
                several times per second.
                           
 **************************************************************************/
 
#include <SoftwareSerial.h>                   // built-in
#include <TinyGPS++.h>                        // tinyGPSPlus v1.0.3
#include <Adafruit_SSD1306.h>                 // Adafruit OLED library v2.5.7

SoftwareSerial gpsSerial(D5,D6);
TinyGPSPlus gps;
Adafruit_SSD1306 led(128,64,&Wire);           // OLED display object

void setup() {
  gpsSerial.begin(9600);                      // GPS Data @ 9600 baud
  Serial.begin(115200);                       // Serial monitor @ 115200 baud 
  led.begin(SSD1306_SWITCHCAPVCC,0x3C);       // turn on OLED display
  led.display();                              // and display Adafruit logo
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
