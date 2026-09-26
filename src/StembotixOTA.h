/*
  StembotixOTA - OTA firmware updates for STEMbotix ESP32 kits
  ------------------------------------------------------------
  Usage:

    #include <StembotixOTA.h>

    void setup() {
      Serial.begin(115200);
      OTA.begin("My Project");      // initialise OTA (first line after Serial.begin)
      // ... your setup ...
    }

    void loop() {
      OTA.loop();                   // first line of loop()
      // ... your code ...
    }

  - Normal mode: only your code runs (no WiFi, no OTA, no delay).
  - Hold BOOT 3 s -> OTA mode: your code stops, the board connects to WiFi
    and waits for new firmware from the server (https://ota.blockzieai.com).
  - Hold BOOT 3 s again in OTA mode -> WiFi setup hotspot "STEMbotix-Setup-XXXX".
  - Any reset / power-up starts in normal mode again.
*/
#pragma once
#include <Arduino.h>

#define STEMBOTIX_OTA_DEFAULT_SERVER "https://ota.blockzieai.com"

class StembotixOTAClass {
public:
  // Initialise OTA. Call once at the start of setup(), after Serial.begin().
  //   firmwareName : shown on the dashboard
  //   serverUrl    : OTA server, no trailing slash ("" = find app.py on the local WiFi)
  void begin(const char* firmwareName = "ESP32",
             const char* serverUrl = STEMBOTIX_OTA_DEFAULT_SERVER);

  // Call as the FIRST line of loop(). Does nothing in normal mode.
  // When BOOT was held 3 s it switches to OTA mode and never returns
  // (the board restarts after a new firmware is flashed).
  void loop();

  // ---- optional settings (call BEFORE begin) ----
  void setButton(int pin);                       // default 0 (BOOT). ESP32-C3: 9
  void setLed(int pin);                          // LED that blinks in the WiFi setup hotspot, default 2, -1 = none
  void setHoldTime(unsigned long ms);            // default 3000
  void setHotspotPassword(const char* pass);     // default "12345678" (min 8 chars)
  void setCheckInterval(unsigned long ms);       // OTA mode: ask the server every ... ms, default 1000
  void setWiFi(const char* ssid, const char* pass);  // default WiFi if none was saved on the hotspot

  // Called once when OTA mode starts - switch off motors, LEDs, buzzers here.
  void onOtaMode(void (*callback)());

  // ---- optional control ----
  void startOtaMode();                           // enter OTA mode from your own code (never returns)
  void startWiFiSetup();                         // restart into the WiFi setup hotspot
  void forgetWiFi();                             // erase the saved WiFi
  bool isOtaMode() const { return _otaMode; }
  String version();                              // MD5 of the firmware flashed by OTA ("unknown" after USB upload)

private:
  bool _otaMode = false;
};

extern StembotixOTAClass OTA;
