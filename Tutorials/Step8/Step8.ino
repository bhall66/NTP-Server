/**************************************************************************
       Title:   NTP Server tutorial - Step 8
      Author:   Bruce E. Hall, w8bh.net
        Date:   29 June 2023
    Hardware:   ESP3266 (Wemos D1 mini), uBlox GPS module, 128x64 OLED module
    Software:   Arduino IDE 2.1.0
       Legal:   Copyright (c) 2023  Bruce E. Hall.
                Open Source under the terms of the MIT License.
                    
 Description:   Basic NTP Time Server
                           
 **************************************************************************/

#include <SoftwareSerial.h>                      // built-in
#include <TinyGPS++.h>                           // tinyGPSPlus v1.0.3
#include <Adafruit_SSD1306.h>                    // Adafruit OLED library v2.5.7
#include <ESP8266WiFi.h>                         // Wi-Fi support
#include <WiFiUdp.h>                             // UDP packet support
#include <TimeLib.h>                             // Arduino time functions

#define WIFI_SSID "Your SSID"                    // replace with your ID
#define WIFI_PASS "Your Password"                // replace with your password
#define UDP_PORT 123

SoftwareSerial gpsSerial(D5,D6);
TinyGPSPlus gps;
Adafruit_SSD1306 led(128,64,&Wire);              // OLED display object
volatile int syncFlag = 0;
WiFiUDP UDP;   
char packet[255];                                // incoming UDP buffer 


struct ntpTime_t {                               // NTP timestamp is a 64-bit structure:
  uint32_t seconds;                              // # seconds since start of epoch (1/1/1900)   
  uint32_t fraction;                             // 0x00000001 = 2^-32 second ~ 0.2 nS.
} ntpTime;

struct ntpPacket {                               // full packet is 48 bytes in length
  uint8_t    control;
  uint8_t    stratum;
  uint8_t    polling;
  int8_t     precision;
  int32_t    rootDelay;
  uint32_t   rootDispersion;
  char       refID[4];
  ntpTime_t  referenceTime;
  ntpTime_t  originateTime;
  ntpTime_t  receiveTime;
  ntpTime_t  transmitTime;
} ntp;

IRAM_ATTR void ppsHandler() {                    // 1pps interrupt handler
  syncFlag = 1;                                  // flag the need to sync 
}

ntpTime_t unixToNTPTime(time_t t) {
  ntpTime_t response;
  response.seconds = htonl(t+2208988800L);       // ntp starts 1/1/1900; unix starts 1/1/1970
  response.fraction = 0;                         // no fractional seconds in this sketch
  return response;
}

void prepareResponse() {                         // create NTP packet with current time
  ntp.control        = (ntp.control&0x38) + 4;   // change to leap 0 and mode 4(server)
  ntp.stratum        = 0x01;                     // we are calling it stratum-1
  ntp.precision      = 0x00;                     // one second precision: 2^0 = 1 S
  strncpy(ntp.refID,"GPS",4);                    // reference clock is the GPS service
  ntp.originateTime  = ntp.transmitTime;         // time that client transmitted request
  ntp.referenceTime  = unixToNTPTime(now());     // time of sync with reference clock 
  ntp.receiveTime    = ntp.referenceTime;        // time that server received request
  ntp.transmitTime   = ntp.referenceTime;        // time that server responded
}

void answerQuery() {
  memcpy(&ntp,&packet,sizeof(ntp));              // copy UDP buffer to NTP struct  
  prepareResponse();                             // add time data to NTP struct                               
  memcpy(&packet,&ntp,sizeof(ntp));              // copy NTP stuct back to UDP buffer
  UDP.beginPacket(UDP.remoteIP(),UDP.remotePort());   
  UDP.write(packet,sizeof(ntp));                 // send NTP response over UDP
  UDP.endPacket(); 
}

void connectToWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);              // start Wi-Fi connection attempt
  led.clearDisplay();                            // erase previous screen
  led.setCursor(0,0);                            // start at top-left
  led.setTextSize(2);                            // size 2 is easily readable
  led.print("WiFi");                             // show Wi-Fi connnection progress:
  while (WiFi.status() != WL_CONNECTED) {        // while trying to connect,
    delay(500);                                  // print a dot every 0.5 sec.
    led.print(".");  
    led.display();                    
  }
}

void getGPSTime() {                              // set Arduino clock from GPS  
  int h = gps.time.hour();                       // get the hour
  int m = gps.time.minute();                     // get the minutes
  int s = gps.time.second();                     // get the seconds
  int d = gps.date.day();                        // get day
  int mo= gps.date.month();                      // get month
  int y = gps.date.year();                       // get year
  setTime(h,m,s,d,mo,y);                         // set the system time
  adjustTime(1);                                 // and adjust forward 1 second
}

void displayTime() {
  int h=hour();                                  // get current time 
  int m=minute(); 
  int s=second();
  led.clearDisplay();                            // erase previous screen
  led.setCursor(0,0);                            // start at top-left
  led.setTextSize(2);                            // size 2 is easily readable
  if (h<10) led.print("0");                      // make it 2 digits: '6'->'06'
  led.print(h); led.print(":");                  // print the hour  
  if (m<10) led.print("0");                      // make it 2 digits: '6'->'06'
  led.print(m); led.print(":");                  // print the minutes
  if (s<10) led.print("0");                      // make it 2 digits: '6'->'06'
  led.println(s);                                // print the seconds
  led.setTextSize(1);                            // smaller font for IP addr
  led.println();
  led.println(WiFi.localIP());                   // show IP of this server
  led.display();                                 // show the result on screen
}

void setup() {
  gpsSerial.begin(9600);                         // GPS Data @ 9600 baud
  led.begin(SSD1306_SWITCHCAPVCC,0x3C);          // turn on OLED display
  led.setTextColor(WHITE);                       // white on black text
  attachInterrupt(digitalPinToInterrupt(         // enable 1pps GPS time sync
    D7), ppsHandler, RISING);
  connectToWiFi();                               // establish WiFi connection
  UDP.begin(UDP_PORT);                           // start listening for UDP packets
}

void feedGPS() {                                 // feed GPS data into GPS parser
  if (gpsSerial.available()) {                   // if a character is ready to read...
    char c = gpsSerial.read();                   // read the character
    gps.encode(c);                               // and feed the char to the GPS parser
 }
}

void syncWithGPS() {  
  if (syncFlag) {                                // is it time to synchonize time?
    syncFlag = 0;                                // reset the flag, and
    getGPSTime();                                // set system time from GPS
    displayTime();                               // and show it on OLED display
  }
}

void checkForNTP() {
  if (UDP.parsePacket()) {                       // look for a received packet
    int len = UDP.read(packet, 255);             // packet found, so read it into buffer
    if (len) answerQuery();                      // send NTP response
  }
}

void loop() {
  feedGPS();                                     // read data from GPS
  syncWithGPS();                                 // sync clock every second
  checkForNTP();                                 // answer any incoming NTP requests
}
