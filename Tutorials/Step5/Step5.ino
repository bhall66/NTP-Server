/**************************************************************************
       Title:   NTP Server tutorial - Step 5
      Author:   Bruce E. Hall, w8bh.net
        Date:   26 June 2023
    Hardware:   uBlox GPS module, ESP3266 (Wemos D1 mini)
    Software:   Arduino IDE 2.1.0
       Legal:   Copyright (c) 2023  Bruce E. Hall.
                Open Source under the terms of the MIT License. 
                    
 Description:   Step 5 - Use an interrupt on the 1pps GPS line
                Connect the GPS "1pps" pin to ESP8266 "D7"
                           
 **************************************************************************/

#include <SoftwareSerial.h>                   // built-in
#include <TinyGPS++.h>                        // tinyGPSPlus v1.0.3
#include <Adafruit_SSD1306.h>                 // Adafruit OLED library v2.5.7

SoftwareSerial gpsSerial(D5,D6);
TinyGPSPlus gps;
Adafruit_SSD1306 led(128,64,&Wire);           // OLED display object
volatile int syncFlag = 0;

IRAM_ATTR void ppsHandler() {                 // 1pps interrupt handler
  syncFlag = 1;                               // flag the need to sync 
}

void setup() {
  gpsSerial.begin(9600);                      // GPS Data @ 9600 baud
  Serial.begin(115200);                       // Serial monitor @ 115200 baud 
  led.begin(SSD1306_SWITCHCAPVCC,0x3C);       // turn on OLED display
  led.setTextColor(WHITE);                    // white on black text
  led.setTextSize(2);                         // size 2 is easily readable
  led.print("Waiting."); led.display();       // write something on startup
  attachInterrupt(digitalPinToInterrupt(      // enable 1pps GPS time sync
    D7), ppsHandler, RISING);
}

void loop() {
  if (gpsSerial.available()) {                // any data from GPS?
    char c = gpsSerial.read();                // yes, so get the next character
    gps.encode(c);                            // and feed it to GPS parser
  }
  if (syncFlag) {                             // 1pps signal received from GPS?
    syncFlag = 0;                             // yes, so reset the flag
    displayTime();                            // and display the time
  }
}

void displayTime() {
  led.clearDisplay();                         // erase previous screen
  led.setCursor(0,0);                         // start at top-left
  int h = gps.time.hour();                    // get the hour
  int m = gps.time.minute();                  // get the minutes
  int s = gps.time.second();                  // get the seconds

// to add a second, uncomment the next 4 lines
//  s++;                                        // advance to the next second
//  if (s>59) {s=0; m++;}                       // second overflow! Add a minute
//  if (m>59) {m=0; h++;}                       // minute overflow! Add an hour
//  if (h>23) {h=0;}                            // midnight! Reset to zero hour
  
  if (h<10) led.print("0");                   // make it 2 digits: '6'->'06'
  led.print(h); led.print(":");               // print the hour  
  if (m<10) led.print("0");                   // make it 2 digits: '6'->'06'
  led.print(m); led.print(":");               // print the minutes
  if (s<10) led.print("0");                   // make it 2 digits: '6'->'06'
  led.print(s);                               // print the seconds
  led.display();                              // show the result on screen
}
