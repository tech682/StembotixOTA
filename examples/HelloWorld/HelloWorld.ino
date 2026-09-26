/*
  StembotixOTA example - Hello World
  Prints "Hello World" on the Serial Monitor every second.

  Hold BOOT 3 s -> OTA mode (board appears on https://ota.blockzieai.com/dashboard)
  Hold BOOT 3 s again in OTA mode -> WiFi setup hotspot
*/
#include <StembotixOTA.h>

void setup() {
  Serial.begin(115200);
  OTA.begin("Hello World");
}

void loop() {
  OTA.loop();

  Serial.println("Hello World");
  delay(1000);
}
