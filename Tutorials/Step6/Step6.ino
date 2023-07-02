/**************************************************************************
       Title:   NTP Server tutorial - Step 6
      Author:   Bruce E. Hall, w8bh.net
        Date:   26 June 2023
    Hardware:   ESP3266 (Wemos D1 mini)
    Software:   Arduino IDE 2.1.0
       Legal:   Copyright (c) 2023  Bruce E. Hall.
                Open Source under the terms of the MIT License.
                    
 Description:   Listen for a UDP message, print it, and reply back.

 **************************************************************************/
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
 
#define WIFI_SSID "Your SSID"               // replace with your ID
#define WIFI_PASS "Your Password"           // replace with your password
#define UDP_PORT 1234

WiFiUDP UDP;   
char packet[255];                           // UDP packet buffer

void connectToWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to ");
  Serial.print(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {   // while trying to connect
    delay(500);                             // print a dot every 0.5 sec
    Serial.print(".");                      
  }
  Serial.println();
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());  
}

void setup() {
  Serial.begin(115200);
  connectToWiFi();
  UDP.begin(UDP_PORT);
  Serial.print("Listening on UDP port ");
  Serial.println(UDP_PORT);
}

void printPacket() {
  int len = UDP.read(packet, 255);          // read UDP data into buffer
  packet[len] = '\0';                       // terminate the incoming string
  Serial.print("Packet contents: ");
  Serial.println(packet);                   // print the packet contents  
}

void acknowledgePacket() {
  UDP.beginPacket(UDP.remoteIP(),UDP.remotePort());
  UDP.write("Roger that!");
  UDP.endPacket();
}

void loop() {
  int packetSize = UDP.parsePacket();        // look for packet data
  if (packetSize) {                          // data was received, so...
    printPacket();                           // print it out
    acknowledgePacket();                     // and send a reply
  }
}
