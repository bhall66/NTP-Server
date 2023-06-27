/**************************************************************************
       Title:   NTP Server tutorial - Step 4
      Author:   Bruce E. Hall, w8bh.net
        Date:   26 June 2023
    Hardware:   uBlox GPS module, ESP3266 (Wemos D1 mini)
    Software:   Arduino IDE 2.1.0
       Legal:   Copyright (c) 2023  Bruce E. Hall.
                Open Source under the terms of the MIT License. 
                    
 Description:   Display UTC time on an OLED display
                Modifies printTime() from step 3, so that the time
                is shown on display, rather than the Serial Monitor
                           
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
  led.setTextColor(WHITE);                    // white on black text
  led.setTextSize(2);                         // size 2 is easily readable
}

void loop() {
  if (gpsSerial.available()) {                // any data from GPS?
    char c = gpsSerial.read();                // yes, so get the next character
    if (gps.encode(c)) displayTime();         // when NMEA string finished, print the time. 
  }
}

void displayTime() {
  led.clearDisplay();                         // erase previous screen
  led.setCursor(0,0);                         // start at top-left
  int h  = gps.time.hour();                   // get the hour
  if (h<10) led.print("0");                   // make it 2 digits: '6'->'06'
  led.print(h); led.print(":");               // print the hour  
  int m = gps.time.minute();                  // get the minutes
  if (m<10) led.print("0");                   // make it 2 digits: '6'->'06'
  led.print(m); led.print(":");               // print the minutes
  int s = gps.time.second();                  // get the seconds
  if (s<10) led.print("0");                   // make it 2 digits: '6'->'06'
  led.print(s);                               // print the seconds
  led.display();                              // show the result on screen
}
