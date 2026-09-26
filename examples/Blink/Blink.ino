/*
  StembotixOTA example - LED Blink
  Blinks the on-board LED (GPIO2) every 500 ms.

  Hold BOOT 3 s -> OTA mode (board appears on https://ota.blockzieai.com/dashboard)
  Hold BOOT 3 s again in OTA mode -> WiFi setup hotspot
*/
#include <StembotixOTA.h>

const int LED_PIN = 2;

void ledOff() {                      // runs once when OTA mode starts
  digitalWrite(LED_PIN, LOW);
}

void setup() {
  Serial.begin(115200);
  OTA.onOtaMode(ledOff);
  OTA.begin("LED Blink");

  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  OTA.loop();

  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
  delay(500);
}
