/**************************************************************************
       Title:   NTP Server tutorial - Step 7
      Author:   Bruce E. Hall, w8bh.net
        Date:   26 June 2023
    Hardware:   ESP3266 (Wemos D1 mini)
    Software:   Arduino IDE 2.1.0
       Legal:   Copyright (c) 2023  Bruce E. Hall.
                Open Source under the terms of the MIT License.
                    
 Description:   Step 7 - Sent time messages via UDP
                           
 **************************************************************************/

#include <SoftwareSerial.h>                   // built-in
#include <TinyGPS++.h>                        // tinyGPSPlus v1.0.3
#include <Adafruit_SSD1306.h>                 // Adafruit OLED library v2.5.7
#include <ESP8266WiFi.h>                      // Wi-Fi support
#include <WiFiUdp.h>                          // UDP packet support

#define WIFI_SSID "Your SSID"                 // replace with your ID
#define WIFI_PASS "Your Password"             // replace with your password
#define UDP_PORT 1234

SoftwareSerial gpsSerial(D5,D6);
TinyGPSPlus gps;
Adafruit_SSD1306 led(128,64,&Wire);           // OLED display object
volatile int syncFlag = 0;
WiFiUDP UDP;   
char packet[255];                             // incoming UDP buffer 
char reply[255];                              // outgoing UDP buffer

IRAM_ATTR void ppsHandler() {                 // 1pps interrupt handler
  syncFlag = 1;                               // flag the need to sync 
}

void printPacket() {
  int len = UDP.read(packet, 255);            // read UDP data into buffer
  packet[len] = '\0';                         // terminate the incoming string
  Serial.print("Packet received: ");
  Serial.println(packet);                     // print the packet received
}

void acknowledgePacket() {
  int h = gps.time.hour();                    // get the hour
  int m = gps.time.minute();                  // get the minutes
  int s = gps.time.second();                  // get the seconds
  sprintf(reply,"%02d:%02d:%02d UTC",h,m,s);  // copy formatted time to reply buffer
  Serial.print("Packet sent:     ");
  Serial.println(reply);                      // print the packet sent 
  
  UDP.beginPacket(UDP.remoteIP(),UDP.remotePort());
  UDP.write(reply);                           // send time via UDP
  UDP.endPacket();
}

void connectToWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to ");
  Serial.print(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {     // while trying to connect
    delay(500);                               // print a dot every 0.5 sec
    Serial.print(".");                      
  }
  Serial.println();
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());  
}

void displayTime() {
  led.clearDisplay();                         // erase previous screen
  led.setCursor(0,0);                         // start at top-left
  int h = gps.time.hour();                    // get the hour
  int m = gps.time.minute();                  // get the minutes
  int s = gps.time.second();                  // get the seconds

  s++;                                        // advance to the next second
  if (s>59) {s=0; m++;}                       // second overflow! Add a minute
  if (m>59) {m=0; h++;}                       // minute overflow! Add an hour
  if (h>23) {h=0;}                            // midnight! Reset to zero
  
  if (h<10) led.print("0");                   // make it 2 digits: '6'->'06'
  led.print(h); led.print(":");               // print the hour  
  if (m<10) led.print("0");                   // make it 2 digits: '6'->'06'
  led.print(m); led.print(":");               // print the minutes
  if (s<10) led.print("0");                   // make it 2 digits: '6'->'06'
  led.print(s);                               // print the seconds
  led.display();                              // show the result on screen
}

void setup() {
  gpsSerial.begin(9600);                      // GPS Data @ 9600 baud
  Serial.begin(115200);                       // Serial monitor @ 115200 baud 
  led.begin(SSD1306_SWITCHCAPVCC,0x3C);       // turn on OLED display
  led.setTextColor(WHITE);                    // white on black text
  led.setTextSize(2);                         // size 2 is easily readable
  attachInterrupt(digitalPinToInterrupt(      // enable 1pps GPS time sync
    D7), ppsHandler, RISING);
  connectToWiFi();
  UDP.begin(UDP_PORT);
  Serial.print("Listening on UDP port ");
  Serial.println(UDP_PORT);
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
  int packetSize = UDP.parsePacket();        // look for packet data
  if (packetSize) {                          // data was received, so...
    printPacket();                           // print it out
    acknowledgePacket();                     // and send a reply
  }
}
