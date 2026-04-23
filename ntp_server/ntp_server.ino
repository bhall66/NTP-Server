/**************************************************************************
       Title:   ntp_server
      Author:   Bruce E. Hall, w8bh.net
        Date:   21 Apr 2026
    Hardware:   uBlox GPS module, ESP3266 (Wemos D1 mini), 128x64 OLED,
                DS3231 RTC I2C module
    Software:   Arduino IDE 2.3.8
       Legal:   Copyright (c) 2026  Bruce E. Hall.
                Open Source under the terms of the MIT License. 
                    
 Description:   NTP Server, featuring -- 
                Microsecond clock using 1pps GPS interrupt
                OLED display.
                Unix 64-bit timestamp
                GPS lock status display
                satellite count display
                NTP packet count display
                WiFi setup via mobile phone
                WiFi modes: AP/STA-DHCP/STA-fixedIP
                Time + events via SERIAL output
                RTC fallback when GPS unavailable
 **************************************************************************/
 
#include <ESP8266WiFi.h>                         // WiFI support            
#include <WiFiManager.h>                         // https://github.com/tzapu/WiFiManager
#include <WiFiUdp.h>                             // UDP packet support
#include <SoftwareSerial.h>                      // receiving GPS ASCII data
#include <TimeLib.h>                             // setting system time
#include <TinyGPS++.h>                           // decoding GPS NMEA strings
#include <Adafruit_SSD1306.h>                    // Adafruit OLED library
#include <RTClib.h>                              // Adafruit RTC library
#include <Fonts/FreeSansBold12pt7b.h>            // Time Display font
#include <ESP8266WebServer.h>
#include <EEPROM.h>

#define YOUR_CALL      "W8BH"                    // displayed on the splash screen
#define TIME_FORMAT    "hh:mm:ss UTC"            // serial output timestamp format
#define AP_SSID        "NTP-server"
#define AP_PASS        "hamradio"
#define IP1              192                     // default 192 correct for small networks
#define IP2              168                     // default 168 correct for small networks
#define IP3_AP           73                      // IP = <IP1>.<IP2>.<IP3_AP>.1
#define IP3_STA          86                      // Your router's network segment
#define IP4_STA          222                     // fixedIP <IP1>.<IP2>.<IP3_STA>.<IP4_STA>
#define IP3_MASK         255                     // default 255 correct for small networks
#define AP_MAXCLIENTS    6                       // must be 0-8, default 4
#define AP_CHANNEL       5                       // must be be 1-13, default 1
#define STARTUP_MODE     1                       // 1=AP, 2=STA/DHCP, 3=STA/fixedIP, 4=STA/config
#define ALLOW_SLEEP      false                   // sleep saves power but increases network latency  
#define GPS_BAUD         9600                    // serial data rate of GPS module, usually 9600
#define SERIAL_BAUD      115200                  // data rate of ESP8266 output, usually 115200
#define UDP_PORT         123                     // port accepting NTP requests, usually 123
#define OLED_WIDTH       128                     // OLED display width, in pixels
#define OLED_HEIGHT      64                      // OLED display height, in pixels
#define OLED_ADDRESS     0x3C                    // OLED display I2C address, in hex
#define OLED_ROTATION    2                       // 0 = landscape; 2 = reverse landscape
#define OLED_TIMEOUT     28800                   // screen-saver timeout.  28800s = 8 hours
#define WIFI_RESET       A0                      // take high on power-up to set Wi-Fi mode
#define OLED_SCL         D1                      // hardware-defined I2C pin
#define OLED_SDA         D2                      // hardware-defined I2C pin
#define LED              D4                      // D4 = GPIO2
#define GPS_TX           D5                      // D5 = GPIO14
#define GPS_RX           -1                      // D6 = GPIO12 not used
#define GPS_PPS          D7                      // 1pps GPS signal to D7 = GPIO13
#define SYNC_INTERVAL    1                       // seconds between GPS sync attempts
#define SYNC_LOST        60                      // secs till loss declared. MUST BE > SYNC_INTERVAL
#define RTC_INTERVAL     600                     // seconds between RTC updates.
#define NTP_LENGTH       48                      // Size of NTP packet in bytes
#define PRINT_TIME       false                   // if true, copy timestamps to serial port
#define PRINT_EVENTS     false                   // if true, copy NTP/RTC/WiFi events to serial port
#define FLASH_DURATION   150                     // duration of GPS lock LED flash, in mS

struct unixTime_t {                              // unix timestamp is a 64 bit structure:
  uint32_t seconds;                              // # seconds since 1/1/1970
  uint32_t uSeconds;                             // # microseconds.  0x00000001 = 1 uS
};

struct ntpTime_t {                               // NTP timestamp is a 64-bit structure:
  uint32_t seconds;                              // # seconds since start of epoch (1/1/1900)   
  uint32_t fraction;                             // 0x00000001 = 2^-32 second ~ 0.2 nS.
};

struct ntpPacket {                               // full packet is 48 bytes in length:
  uint8_t    control;                            // control byte contains leap, version, and mode
  uint8_t    stratum;                            // 0=undefined, 1=primary clock, 2-15 secondary
  uint8_t    polling;                            // how often can be polled, in 2^x seconds
  int8_t     precision;                          // time precision, in fraction of second
  int32_t    rootDelay;                          // round-trip delay to/from reference
  uint32_t   rootDispersion;                     // maximum accumulated error
  char       refID[4];                           // reference ID
  ntpTime_t  referenceTime;                      // timestamp of latest reference clock update
  ntpTime_t  originateTime;                      // server returns time of client transmission
  ntpTime_t  receiveTime;                        // time that server received NTP request
  ntpTime_t  transmitTime;                       // time that server/client sent this message
} ntp;

TinyGPSPlus gps;                                 // GPS data parsing object
SoftwareSerial gpsSerial(GPS_TX,GPS_RX);         // serial GPS data
Adafruit_SSD1306 
  led(OLED_WIDTH,OLED_HEIGHT,&Wire);             // OLED display object
WiFiUDP UDP;                                     // NTP packet transport via UDP
RTC_DS3231 rtc;                                  // DS3231 precision real-time clock
WiFiManager wm;                                  // WiFi configuration object
ESP8266WebServer  server(80);                    // HTTP server

uint32_t t = 0;                                  // temporary time variable
uint32_t lastRTC = 0;                            // system time of last RTC update 
volatile byte pps = 0;                           // GPS pulse-per-second counter
volatile uint32_t lastTick = 0;                  // microsecond counter at last GPS sync
volatile uint32_t lastSync = 0;                  // unix time of last GPS sync
volatile bool syncFlag = 0;                      // true after GPS syncronization occurs                   
char packet[255];                                // UDP raw packet buffer
int inPackets = 0;                               // count of packets received
int outPackets = 0;                              // count of packets transmitted
int screenTimer = 0;                             // OLED screen time on, in seconds
bool rtcPresent = false;                         // if true, RTS module found
int mode   = 0;                                  // WiFi mode (0=none, 1=AP, 2=STA/DCHP, 3=STA/FixedIP)
IPAddress ip(IP1,IP2,IP3_STA,IP4_STA);           // last saved local IP address


//----------------------------------------------------------------------------------------
//     PUSHBUTTON (BATTERY & SWITCH) ROUTINES
//----------------------------------------------------------------------------------------

bool switchPressed() {
 return analogRead(WIFI_RESET)>800;
}

int readVoltage() {
  int raw = analogRead(A0);               
  return raw*360/512;                             // should be 33 instead of 36;  returns 100*V: 3.3V = 330
}


//----------------------------------------------------------------------------------------
//     EEPROM ROUTINES
//----------------------------------------------------------------------------------------

#define EEPROM_FLAG  42

void readEEPROM() {
  if (EEPROM.read(0)!=EEPROM_FLAG) {             // first EEPROM use?
    mode = STARTUP_MODE;                         // yes, use default mode
  } else {                                       // data already saved, so
    mode = EEPROM.read(1);                       // get saved mode
    for (int i=0; i<4; i++)                      // get saved IP address
     ip[i] = EEPROM.read(i+2);
  }
  if ((mode<1)||(mode>3))                        // mode out of bounds?
    mode = STARTUP_MODE;                         // dont let that happen
  printEEPROM();
}

void writeEEPROM() {
  EEPROM.write(0,EEPROM_FLAG);                   // save flag
  EEPROM.write(1,mode);                          // save WiFi mode
  for (int i=0; i<4; i++)                        // save full IP address
    EEPROM.write(i+2,ip[i]);                       
  EEPROM.commit();                               // make it so
  printEEPROM();
}

//----------------------------------------------------------------------------------------
//     WiFi/NTP/UDP ROUTINES
//----------------------------------------------------------------------------------------

bool inAPmode() {
  return (WiFi.getMode()==WIFI_AP);
}

String webpage() {
  String htmlPage,timeStr;
  htmlPage.reserve(2048);               // prevent ram fragmentation
  htmlPage = F("<html><head>"
    "<meta http-equiv=\"refresh\" content=\"60\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<style>table {border-collapse: collapse;width: 100%;}"
    "th, td {padding: 8px; text-align: left; border-bottom: 1px solid #ddd;}"
    "</style></head><body>"
    "<h1>NTP server</h1>"
    "<table>"
    "<tr><th>Time</th><th>%T</th></tr>"
    "<tr><td>Wi-Fi Mode</td><td>%W</td></tr>"
    "<tr><td>RTC status</td><td>%R</td></tr>"
    "<tr><td>Battery Voltage</td><td>%A</td></tr>"
    "<tr><td>GPS Satellites</td><td>%S</td></tr>"
    "<tr><td>GPS Locked</td><td>%G</td></tr>"
    "<tr><td>NTP clients</td><td>%C</td></tr>"
    "<tr><td>NTP requests</td><td>%P</td></tr>"
    "</table></body></html>");

  char buf[30] = TIME_FORMAT;                    // change time format in #define to suit
  DateTime dt(now());
  float v = readVoltage()/100.0;
  htmlPage.replace("%T",dt.toString(buf));
  htmlPage.replace("%A",String(v));
  htmlPage.replace("%S",String(gps.satellites.value()));
  htmlPage.replace("%P",String(outPackets));
  if (GPSlocked()) htmlPage.replace("%G","Yes");
  else htmlPage.replace("%G","<b>No</b>");
  if (!rtcPresent) htmlPage.replace("%R","None");
  else if (rtc.lostPower()) htmlPage.replace("%R","Check RTC battery");
  else htmlPage.replace("%R","OK");
  if (inAPmode()) {
    htmlPage.replace("%W","Access Point");
    htmlPage.replace("%C",String(WiFi.softAPgetStationNum()));
  } else {
    htmlPage.replace("%W","Station");
    htmlPage.replace("%C","n/a");
  }
  return htmlPage;
}

void handleRoot() {
  server.send(200, "text/html", webpage());      // go to webpage() for all html req.
}

void cbNoWiFi (WiFiManager *myWiFiManager) {     // callback function: no WiFi
  displayNoNetwork();                            // tell user to open AP on phone
}

void cbGotCreds() {                              // callback function: new credentials
  IPAddress newIP = WiFi.localIP();              // get local IP address
  int j = (mode==3)?4:3;                         // last octet only saved for fixedIP
  for (int i=0; i<j; i++)                        // copy it
    ip[i] = newIP[i];
  writeEEPROM();                                 // and save it for future use
}

void connectWiFi_AP() {                          // establish Access point (AP) WiFi
  IPAddress myIP(ip[0],ip[1],IP3_AP,1);          // establish fixed local IP address
  IPAddress mask(255,255,255,0);                 // establish size of local subnet
  WiFi.mode(WIFI_AP);                            
  WiFi.softAPConfig(myIP,myIP,mask);             // establish static IP address config.
  WiFi.softAP(AP_SSID, AP_PASS,                  // set access point SSID & password
    AP_CHANNEL, false, AP_MAXCLIENTS);           // set WiFi channel & maximum clients
}

void connectWiFi_STA(int mode) {                 // establish Station (STA) WiFi
  IPAddress gway(ip[0],ip[1],ip[2],1);
  IPAddress mask(255,255,IP3_MASK,0);        
  if (mode==3)                                   // user wants static IP mode
    wm.setSTAStaticIPConfig(ip,gway,mask);
  wm.setAPCallback(cbNoWiFi);                    // routine called for failed connection
  wm.setSaveConfigCallback(cbGotCreds);          // routine called after creds updated
  wm.setConfigPortalTimeout(180);                // AP open 3 minutes to enter Wifi info
  wm.setDebugOutput(false);                      // dont print WiFi debug info
  if(!wm.autoConnect(AP_SSID)) {                 // create an access point for creds.
    printConnectTimeout();                       // user didn't enter WiFi credentials
    delay(3000);                                 // allow time to close access point
    ESP.restart();                               // let's start again, shall we?
    delay(5000);
  } 
  displayIP();                                   // display IP for this station  
}

void connectWiFi() {                             // establish the WiFi connection       
  WiFi.disconnect(true);  
  if (mode<2) connectWiFi_AP();                  // establish AP mode
  else connectWiFi_STA(mode);                    // establish STA mode
  WiFi.setSleep(ALLOW_SLEEP);                    // no power-saving = faster WiFi
  printWiFiConnection();                         // print IP information
}

ntpTime_t unixToNTPTime(unixTime_t unixTime) {
  ntpTime_t response;
  response.seconds = htonl(unixTime.seconds+2208988800L); 
  response.fraction = htonl(unixTime.uSeconds*4295); 
  return response;
}

unixTime_t ntpToUnixTime(ntpTime_t ntpTime) {
  unixTime_t response;
  response.seconds = ntohl(ntpTime.seconds)-2208988800L;
  response.uSeconds = ntohl(ntpTime.fraction)/4295;
  return response;                 
}

unixTime_t packUnixTime(uint32_t s, uint32_t uS) {
  unixTime_t response = {s, uS};
  return response;
}

bool packetIsNTPQuery(int len) {                 // verify that NTP query is valid
  bool correctSize = (len==sizeof(ntp));         // UDP packet must be correct size
  int cb   = packet[0];                          // get NTP control byte from packet
  int leap = (cb & 0b11000000) >> 6;             // take leap indicator from bits 6-7
  int ver  = (cb & 0b00111000) >> 3;             // take NTP version from bits 3-5
  int mode = (cb & 0b00000111);                  // take NTP mode from bits 0-2
  bool leapOK = ((leap==0)||(leap==3));          // accept leap 0 or 3(unknown)
  bool verOK  = ((ver>=1)&&(ver<=4));            // accept NTP versions 1-4
  bool modeOK = (mode==3);                       // accept only clients (mode 3)
  return correctSize && leapOK && verOK && modeOK;
}

void prepareResponse() {                         // create NTP packet with current time
  ntp.control        = (ntp.control&0x38)+4;     // change to leap 0 and mode 4(server)
  ntp.polling        = 0x09;                     // 2^9 = 512 seconds
  ntp.rootDelay      = 0x04000000;               // 4 x 2^-16s = 60 uS
  if (GPSlocked()) {                             // for GPS time...
    ntp.stratum        = 0x01;                   // we are calling it stratum-1
    ntp.precision      = 0xF4;                   // microsecond precision: 2^-20 = 1 uS
    strncpy(ntp.refID,"GPS",4);                  // reference clock is the GPS service
    ntp.rootDispersion = 0x0D000000;             // 13 x 2^-16s = 200 uS 
    ntp.referenceTime
    = unixToNTPTime(packUnixTime(lastSync,0));    
  } else {                                       // for RTC/system time...
    ntp.stratum        = 0x02;                   // cant call it stratum-1
    ntp.precision      = 0x00;                   // RTC has precision of 2^0 = 1 second.
    strncpy(ntp.refID,"RTC",4);                  // reference clock is the RTC
    ntp.rootDispersion = 0x00000100;             // 1 second 
    ntp.referenceTime
    = unixToNTPTime(packUnixTime(now(),0));      // reference is current system time
  }
  ntp.originateTime  = ntp.transmitTime;         // use client transmit time
  ntp.transmitTime   = unixToNTPTime(unixNow()); // use time just before transmit
}

void answerQuery() {
  memcpy(&ntp,&packet,sizeof(ntp));              // copy UDP data to NTP packet  
  ntp.receiveTime= unixToNTPTime(unixNow());     // stamp time of packet receipt
  prepareResponse();                             // add time data to NTP packet                               
  memcpy(&packet,&ntp,sizeof(ntp));              // copy NTP packet --> UDP buffer
  UDP.beginPacket(UDP.remoteIP(),UDP.remotePort());   
  UDP.write(packet,sizeof(ntp));                 // send NTP response over UDP
  UDP.endPacket(); 
  printPacketSent();                             // record packet sent
}

void checkForNTP() {
  int packetSize = UDP.parsePacket();            // look for a received packet
  if (!packetSize) return;                       // leave if no packets yet
  printPacketReceived();                         // incoming packet notice
  inPackets++;                                   // count all incoming UDP packets
  int len = UDP.read(packet, 255);               // read the data into packet buffer
  if (!len) return;                              // UDP read error
  if (packetIsNTPQuery(len)) {                   // is this a valid NTP query?
    answerQuery();                               // send NTP response
    outPackets++;                                // count outgoing NTP packets
  }
}


//----------------------------------------------------------------------------------------
//     GPS ROUTINES
//----------------------------------------------------------------------------------------

IRAM_ATTR void ppsHandler() {                    // 1pps interrupt handler, completes in 56uS
  pps++;                                         // count interrupt pulses
  if ((!lastSync)||(pps >= SYNC_INTERVAL)) {     // is it time to synchonize time?
    if (!gps.time.isValid()) return;             // continue only if valid data
    if (gps.time.age()>1000) return;             // dont use stale data 
    lastTick = micros();                         // save microsecond counter
    pps = 0;                                     // reset pulse counter 
    tmElements_t tm = {                          // pack tm with GPS current time/date
      gps.time.second(),                         // get second value
      gps.time.minute(),                         // get minute value
      gps.time.hour(), 0,                        // get hour value; weekday not used
      gps.date.day(),                            // get day
      gps.date.month(),                          // get month
      CalendarYrToTm(gps.date.year())            // get years since 1970
    };                                           // convert tm structure to unix time
    lastSync = makeTime(tm)+1;                   // timing pulse starts NEXT second, so +1
    syncFlag = 1;                                // flag successful sync 
  }
}

void syncWithGPS() {  
  if (syncFlag) {                                // is it time to synchonize time?
    syncFlag = 0;                                // reset the flag, and
    setTime(lastSync);                           // set system time from GPS
  }
}

bool GPSlocked() {                               // check if we have recent GPS sync
  if (!lastSync) return false;                   // must have synched at least once
  uint32_t diff = now()-lastSync;                // diff = seconds since last sync
  return diff <= SYNC_LOST;                      // it should be less than SYNC_LOST            
}

unixTime_t unixNow() {                           // return the current 64-bit unix time
  uint32_t delta=0, s=lastSync;                  // count whole seconds, starting with lastSync
  if (!GPSlocked()) s = now();                   // if no GPS, grab the system time
  else {                                         // GPS, so calculate time from last sync:
    delta = micros() - lastTick;                 // unsigned subtraction is wraparound safe
    while (delta>999999) {                       // lastSync more than 1 second ago? 
      delta-=1000000;                            // subtract off all full seconds
      s++;                                       // and add them to total
    } 
  }                
  return packUnixTime(s,delta);                  // pack result w/ seconds & uSeconds                                              
}


//----------------------------------------------------------------------------------------
//     DS3231 RTC ROUTINES
//----------------------------------------------------------------------------------------

void flash(int count=1) {                        // Flash the LED, <count> times
  if (count<1) return;                           // do nothing for negative counts
  for (int i=0; i<count; i++) {
    digitalWrite(LED,1); delay(250);             // briefly turn on LED
    digitalWrite(LED,0); delay(250);             // then turn off LED
  }
}

void initRTC() {
  delay(1000);                                   // wait before starting RTC
  rtcPresent = rtc.begin();                      // attempt RTC initialization                                  
  if(!rtcPresent) Serial.println("No RTC");      // RTC failed to initialze!
  else if(rtc.lostPower())                       // initialized, so check RTC oscillator status
    Serial.println("ERROR: no RTC battery");     // RTC oscillator stopped, must be dead battery
}

void setRTC() {                                  // set RTC from current time
  rtc.adjust(DateTime(now()));
  printSettingRTC();            
}

uint32_t getRTC() {                              // get unix time from RTC
  if (!rtc.now().isValid()) return 0;            // return 0 if RTC not working
  else return rtc.now().unixtime();              // return RTC time
}

void syncWithRTC() {                             // set system time from RTC
  int rtcTime = getRTC();                        // get time from real-time clock
  if (!rtcTime) return;                          // exit if RTC not installed
  setTime(rtcTime);                              // update system time
  printGettingRTC();                             // announce it on serial port
}

void updateRTC() {                               // keep RTC time current
  if (t > (lastRTC + RTC_INTERVAL)) {            // been longer than RTC_INTERVAL?
    lastRTC = t;                                 // yes, remember the time
    if (GPSlocked()) setRTC();                   // if GPS locked, set RTC from GPS
    else syncWithRTC();                          // no GPS, so set system time by RTC
  }
}


//----------------------------------------------------------------------------------------
//     SERIAL MONITOR ROUTINES
//----------------------------------------------------------------------------------------

void printTime(uint32_t t) {                     // write specified time to serial monitor
  char buf[30] = TIME_FORMAT;                    // change time format in #define to suit
  DateTime dt(t);
  Serial.print(dt.toString(buf));
}

void printCurrentTime() {
  if (!PRINT_TIME) return;                       // user doesn't want serial timestamps
  printTime(now());                              // write current time to serial monitor
  if (GPSlocked()) Serial.print('.');            // add "." if GPS locked
  Serial.println();
}

void printConnectTimeout() {
  if (!PRINT_EVENTS) return;                     // user doesn't want event messages
  Serial.println("WiFi connect timed out.");
}

void printSettingRTC() {
  if (!PRINT_EVENTS) return;                     // user doesn't want event messages
  printTime(t); 
  Serial.println(": Setting RTC");
}

void printGettingRTC() {
  if (!PRINT_EVENTS) return;                     // user doesn't want event messages
  printTime(t); 
  Serial.println(": Getting RTC"); 
} 

void printPacketSent() {
  if (!PRINT_EVENTS) return;                     // user doesn't want event messages
  printTime(t); 
  Serial.print(": Sent NTP packet to ");
  Serial.println(UDP.remoteIP());
}

void printPacketReceived() {
  if (!PRINT_EVENTS) return;                     // user doesn't want event messages
  printTime(t);
  Serial.printf(": Received UDP packet from ");
  Serial.println(UDP.remoteIP());                // show where packet came from
}

void printWiFiConnection() {
  if (!PRINT_EVENTS) return;                     // user doesn't want event messages
  Serial.println();
  Serial.print("MAC address:   ");
  Serial.println(WiFi.macAddress());
  Serial.print("Soft-AP addr:  ");               // print local IP addr (AP mode)
  Serial.println(WiFi.softAPIP());
  Serial.print("Local IP addr: ");               // print local IP addr (STA mode)
  Serial.println(WiFi.localIP());
  Serial.print("Gateway addr:  ");               // print the gateway IP
  Serial.println(WiFi.gatewayIP());
  Serial.print("Listening on UDP port: ");       // print UDP port, should be 123
  Serial.println(UDP_PORT);
}

void printEEPROM() {
  if (!PRINT_EVENTS) return;
  Serial.printf("EEPROM: mode %d, IP %d.%d.%d.%d\r\n",
    mode,ip[0],ip[1],ip[2],ip[3]);
}


//----------------------------------------------------------------------------------------
//     OLED DISPLAY ROUTINES
//----------------------------------------------------------------------------------------

void initDisplay() {
  led.begin(SSD1306_SWITCHCAPVCC, 
    OLED_ADDRESS);                               // initialize the OLED display
  led.setRotation(OLED_ROTATION);                // display orientation (landscape vs. portrait)
}
  
void displayNoNetwork() {                        // cant connect to Wifi,
  led.clearDisplay();                            // so alert user
  led.setTextSize(2);
  led.fillRect(0,0,128,22,WHITE);
  led.setCursor(20,4);
  led.setTextColor(BLACK);
  led.println("NO WIFI");
  led.setCursor(0,30); 
  led.setTextSize(1);                           
  led.setTextColor(WHITE);
  led.println("On your mobile, pick");           // tell user how to
  led.println("WiFi \"NTP SERVER\"");            // enter WiFi credentials
  led.println("to configure.");                  // on their mobile phone
  led.display();
}

void displayConfigChoices(int choice) {
  const int x=14,y=24,w=100,h=31;
  led.fillRect(x,y,w,h,BLACK);
  for (int i=1; i<=4; i++) {
    int j=y+(i-1)*10;
    led.setTextColor((i==choice)?BLACK:WHITE);
    led.fillRect(x,j,w,10,(i==choice)?WHITE:BLACK);
    led.setCursor(x+2,j+1);
    switch (i) {
      case 1: led.print("Field (AP) mode"); break;
      case 2: led.print("Home (STA) DHCP"); break;
      case 3: led.print("Home (STA) fixed"); break;
      case 4: led.print("STA router setup");
    }
  }
  led.display();
}

void getStartupMode() {
  if (!switchPressed()) return;                  // return default mode
  led.clearDisplay();
  led.setCursor(0,0);
  led.println("Server Configuration");
  led.drawFastHLine(0,8,120,WHITE);
  led.setCursor(0,10);
  led.println("Press & Hold to pick");
  int i=mode;
  displayConfigChoices(i);                       // highlight current choice
  while (switchPressed()) delay(100);            // wait for release
  delay(100);                                    // debounce release
  while (true) {
    delay(800);                                  // set selection speed          
    if (switchPressed()) break;                  // exit on press
    i++; if (i>4) i=1;                           // go to next choice
    displayConfigChoices(i);                     // and highlight it
  }  
  led.setCursor(6,16+i*10);
  led.setTextColor(WHITE);
  led.print("*");                                // show selected choice
  led.display();
  if (i==4) wm.resetSettings();                  // erase current WiFi credentials   
  else {mode=i; writeEEPROM();}                  // save mode in EEPROM
  while (switchPressed()) delay(100);            // wait until release
  delay(1000);                                   // then wait a sec                 
}

void displaySplash() {
  led.clearDisplay();
  led.setFont(&FreeSansBold12pt7b);
  led.fillRect(0,0,128,22,WHITE);
  led.setCursor(10,19);
  led.setTextColor(BLACK);
  led.println(YOUR_CALL);                        // display callsign on boot
  led.setTextColor(WHITE);
  led.setCursor(10,42);
  led.println("NTP");                            // display NTP SERVER on boot
  led.setCursor(10,62);
  led.println("SERVER");
  led.setFont();
  led.display();
}

void displayTwoDigits(int i) {                   // helper function for showing time 
  if (i<10) led.print("0");                      // display '6' as '06'
  led.print(i);
}

void displayTime(uint32_t t) {                   // show current time on OLED display
  if (GPSlocked()) {                             // when time locked to GPS
    led.fillRect(0,0,128,22,WHITE);              // show time on white background
    led.setTextColor(BLACK);
  } else {                                       // when time is NOT locked to GPS
    led.fillRect(0,0,128,22,BLACK);              // show time on black background  
    led.setTextColor(WHITE);
  }                                              // here is where the displayed time
  led.setCursor(8,12);                            // is formatted.  Change to suit!
  led.setFont(&FreeSansBold12pt7b);
  displayTwoDigits(hour(t));                     // time format: hh:mm:ss
  led.print(":");
  displayTwoDigits(minute(t));
  led.print(":");
  displayTwoDigits(second(t));
  led.setFont();
  led.setTextSize(1);                            // minimize size of displayed date
  led.setCursor(112,2);                          // move it to the right of time
  displayTwoDigits(month(t));                    // with month on top
  led.setCursor(112,12);                         // and day on bottom                  
  displayTwoDigits(day(t));                      
}

void displayIP() {                               // display WiFi IP address
  const int x=0,y=32,w=15,h=14;
  led.fillRect(x,y,w,h,WHITE);
  led.setCursor(x+2,y+3);
  led.setTextColor(BLACK);
  led.setTextSize(1);
  if (inAPmode()) led.print("AP");
  else led.print("ST");
  led.setCursor(x+w+2,y+3);
  led.setTextColor(WHITE);
  if (inAPmode()) led.print(WiFi.softAPIP());
  else led.print(WiFi.localIP());                     
}

void displayRSSI() {
  const int x=107,y=26,w=3,h=4,sp=1;             // each bar unit 3px wide, 4px tall
  int rssi = WiFi.RSSI();                        // get signal strength in dBm
  int bars = 0;                                  // convert to bars:
  if (rssi>-55) bars = 5;                        // 5 = excellent signal
  else if (rssi>-65) bars = 4;                   // 4 = good signal
  else if (rssi>-70) bars = 3;                   // 3 = average signal
  else if (rssi>-78) bars = 2;                   // 2 = fair signal
  else if (rssi>-82) bars = 1;                   // 1 = poor signal  (0=unreliable)
  for (int i=0; i<5; i++) {                      // generate bar graph:
    int x1=x+(w+sp)*i;                           // x1 determined by bar position
    int y1=y+(5*h);                              // move y from top/left to bottom/left
    int ht=h*(i+1);                              // ht is height for this bar position 
    if (bars>i)                                  // is signal stronger than this level?
      led.fillRect(x1,y1-ht,w,ht,WHITE);         // if it is, draw a bar 
    else                                         // otherwise
      led.drawLine(x1,y1,x1+w-1,y1,WHITE);       // draw a baseline
  }
}

void displayVoltage() {
  int v=readVoltage();
  const int x=0,y=50,w=10,h=14;
  led.fillRect(x,y,w,h,WHITE);
  led.setCursor(x+2,y+3);
  led.setTextColor(BLACK);
  led.setTextSize(1);
  led.print('V');
  led.setCursor(x+w+2,y+3);
  led.setTextColor(WHITE);
  led.print(v/100);
  led.print('.');
  led.print(v%100/10); 
}

void displaySats() {                             // display # of GPS satellites
  const int x=35,y=50,w=10,h=14;
  int satCount = gps.satellites.value();
  led.fillRect(x,y,w,h,WHITE);
  led.setCursor(x+2,y+3);
  led.setTextColor(BLACK);
  led.setTextSize(1);
  led.print('S');
  led.setCursor(x+w+2,y+3);
  led.setTextColor(WHITE);
  led.print(satCount);
}

void displayClients() {
  int clients = WiFi.softAPgetStationNum();      // get client count
  const int x=62,y=50,w=10,h=14;
  led.fillRect(x,y,w,h,WHITE);
  led.setCursor(x+2,y+3);
  led.setTextColor(BLACK);
  led.setTextSize(1);
  led.print('C');
  led.setCursor(x+w+2,y+3);
  led.setTextColor(WHITE);
  if (inAPmode()) led.print(clients);
  else led.print('-');
}

void displayPackets() {                          // display NTP packet counters
  const int x=85,y=50,w=10,h=14;
  led.fillRect(x,y,w,h,WHITE);
  led.setCursor(x+2,y+3);
  led.setTextColor(BLACK);
  led.setTextSize(1);
  led.print('R');
  led.setCursor(x+w+2,y+3);
  led.setTextColor(WHITE);
  led.print(outPackets%100000);                  // display NTP requests (last 5 digits) 
}

void displayRTCstatus() {
  if (!rtcPresent) return;                       // ignore if no RTC
  if (rtc.lostPower())
    led.drawRect(122,62,2,2,WHITE);              // put a dot on rt/bottom screen
}

void displayEverything() {
  displayTime(t);                                // add the time,
  displayIP();                                   // WiFi IP address,
  displayRSSI();                                 // WiFi signal strength,
  displayVoltage();                              // battery voltage,
  displaySats();                                 // satellite count,
  displayClients();                              // number of clients,
  displayPackets();                              // NTP requests,
  displayRTCstatus();                            // and RTC status
}

void displayScreenSaver() {
  int x=random(50); int y=random(50);
  led.setCursor(x,y);
  displayTwoDigits(hour(t));                     // time format: hh:mm
  led.print(":");
  displayTwoDigits(minute(t));
  led.setCursor(x,y+12);
  led.print("Press button");
}

void updateDisplay() {                           // OLED display during normal operation:
  led.clearDisplay();                            // start with a blank screen
  if (screenTimer < OLED_TIMEOUT)
    displayEverything();
  else displayScreenSaver();
  led.display();                                 // update the physical screen
}

//----------------------------------------------------------------------------------------
//     MAIN PROGRAM
//----------------------------------------------------------------------------------------

void showStatus() {                              // turn on LED if unit is operational:
  bool status = 
    GPSlocked() && (                             // if GPS is locked and either
    (inAPmode()) ||                              // a) unit is in AP mode, or
    (WiFi.status()==WL_CONNECTED));              // b) is in STA mode & connected
  digitalWrite(LED,status);                      // show status on LED                   
}


void feedGPS() {                                 // feed GPS data into GPS parser
  if (gpsSerial.available()) {                   // if a character is ready to read...
    char c = gpsSerial.read();                   // read the character
    gps.encode(c);                               // and feed the char to the GPS parser
 }
}

void handlePerSecondEvents() {                   // handler for once-per-seconds events
  static int timer;                              // flash duration timer, in mS
  if ((millis()-timer) > FLASH_DURATION)         // has LED been on long enough?
    digitalWrite(LED,0);                         // yes, so turn it off
  if (t!=now()){                                 // has sys time advanced to next second? 
    timer = millis();                            // restart flash duration timer
    t=now();                                     // keep current system time in t
    screenTimer++;                               // keep track of screen time
    printCurrentTime();                          // print time to serial monitor
    updateDisplay();                             // update display every second
    updateRTC();                                 // keep the RTC updated
    showStatus();                                // update LED with unit status: LIT = all good
    server.handleClient();                       // handle incoming HTML requests
    if (switchPressed()) screenTimer=0;          // pressing switch will reset screen saver
  }  
}

void initIO() {
  pinMode(GPS_PPS,INPUT);                        
  pinMode(GPS_TX, INPUT);
  pinMode(GPS_RX, OUTPUT);
  pinMode(LED, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(         // enable 1pps GPS time sync
    GPS_PPS), ppsHandler, RISING);
}

void setup() {
  initIO();                                      // configure GPIO pins
  Serial.begin(SERIAL_BAUD);                     // enable serial monitor output
  Serial.println(AP_SSID);                       // and indicate (re)start
  initDisplay();                                 // turn on the OLED display
  displaySplash();                               // turn on Splashscreen during init
  EEPROM.begin(6);                               // 6 bytes of nonvolatile storage
  readEEPROM();                                  // get last mode & subnet used
  gpsSerial.begin(GPS_BAUD);                     // enable GPS data input
  initRTC();                                     // start the real-time clock
  syncWithRTC();                                 // grab RTC time while waiting for GPS
  getStartupMode();                              // allow user to change mode on startup
  connectWiFi();                                 // establish WiFi connection
  UDP.begin(UDP_PORT);                           // start listening for NTP requests
  server.on("/", handleRoot);                    // point to html request handler
  server.begin();                                // spin up web server
}

void loop() {                                    // each loop runs in 16uS, on average.
  feedGPS();                                     // accept incoming ASCII data from GPS module
  syncWithGPS();                                 // check for 1pps signal & sync per schedule
  checkForNTP();                                 // handle incoming NTP messages
  handlePerSecondEvents();                       // update display & other per-second events
}
