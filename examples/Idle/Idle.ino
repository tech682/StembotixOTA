/*
  StembotixOTA example - Idle
  Does nothing. Only the OTA feature is in this firmware.

  Hold BOOT 3 s -> OTA mode (board appears on https://ota.blockzieai.com/dashboard)
  Hold BOOT 3 s again in OTA mode -> WiFi setup hotspot
*/
#include <StembotixOTA.h>

void setup() {
  Serial.begin(115200);
  OTA.begin("Idle");
}

void loop() {
  OTA.loop();
}
