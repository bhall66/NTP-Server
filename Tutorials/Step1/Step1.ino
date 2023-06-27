/**************************************************************************
      Author:   Bruce E. Hall, w8bh.net
        Date:   26 June 2023
    Hardware:   uBlox GPS module, ESP3266 dev board
    Software:   Arduino IDE 2.1.0
       Legal:   Copyright (c) 2023  Bruce E. Hall.
                Open Source under the terms of the MIT License. 
                    
 Description:   Echoes GSP ASCII output to Serial Monitor

                Connect GPS Tx pin to ESP8266 "D5" 
                Connect GPS Rx pin to ESP8266 "D6"   (not used)
                Connect GPS PPS pin to ESP8266 "D7"  (not used)

                Must use SoftwareSerial since Serial0 is used by
                USB uploader/serial monitor.
                                   
 **************************************************************************/

#include <SoftwareSerial.h>

SoftwareSerial gpsSerial(D5,D6);              // set up UART for GPS data

void setup() {
  gpsSerial.begin(9600);                      // GPS Data @ 9600 baud
  Serial.begin(115200);                       // Serial monitor @ 115200 baud 
}

void loop() {
  if (gpsSerial.available()) {                // any data from GPS?
    char c = gpsSerial.read();                // yes, so get the next character
    Serial.print(c);                          // and echo it to serial monitor 
  }
}
