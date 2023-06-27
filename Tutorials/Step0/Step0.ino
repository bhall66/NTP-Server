/**************************************************************************
       Title:   NTP Server Tutorial - Step 0
      Author:   Bruce E. Hall, w8bh.net
        Date:   26 Jun 2023
    Hardware:   ESP8266 Wemos D1 mini
    Software:   Arduino IDE 2.1.0
       Legal:   Copyright (c) 2023  Bruce E. Hall.
                Open Source under the terms of the MIT License. 
                    
 Description:   Double-flash on the built-in LED
                
 **************************************************************************/

void blink(int count=1) {                          // W8BH's famous LED blinker!
  pinMode(LED_BUILTIN,OUTPUT);                     // make sure pin is an output
  for (int i=0; i<count; i++) {                    // blink counter
    digitalWrite(LED_BUILTIN,0);                   // turn LED on 
    delay(200);                                    // for 0.2s
    digitalWrite(LED_BUILTIN,1);                   // and then turn LED off
    delay(200);                                    // for 0.2s
  } 
  pinMode(LED_BUILTIN,INPUT);                      // works for both Vcc & Gnd LEDs.
}

void setup() {                                     // nothing to set up
}

void loop() {                                      // do forever:
  blink(2);                                        // double-flash
  delay(1000);                                     // every second
}