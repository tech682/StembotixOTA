# StembotixOTA

OTA firmware updates for STEMbotix ESP32 kits, from https://ota.blockzieai.com/dashboard.

## Install
Arduino IDE -> Sketch -> Include Library -> Add .ZIP Library... -> choose `StembotixOTA.zip`.
No other libraries needed.

## Use
```cpp
#include <StembotixOTA.h>

void setup() {
  Serial.begin(115200);
  OTA.begin("My Project");   // name shown on the dashboard
  // your setup
}

void loop() {
  OTA.loop();                // first line of loop()
  // your code
}
```

- Normal mode: only your code runs.
- Hold BOOT 3 s -> OTA mode: your code stops, the board joins WiFi and waits for firmware.
- Hold BOOT 3 s again in OTA mode -> WiFi setup hotspot `STEMbotix-Setup-XXXX` (password `12345678`).
- Reset / power-up -> normal mode.

Examples: File -> Examples -> StembotixOTA -> Idle / Blink / HelloWorld.

## Optional (before OTA.begin)
| Call | Default |
|---|---|
| `OTA.onOtaMode(fn)` | function run when OTA mode starts (turn outputs off) |
| `OTA.setButton(pin)` | 0 (BOOT) |
| `OTA.setLed(pin)` | 2, -1 = none (blinks in the setup hotspot) |
| `OTA.setHoldTime(ms)` | 3000 |
| `OTA.setHotspotPassword("...")` | "12345678" |
| `OTA.setCheckInterval(ms)` | 1000 |
| `OTA.setWiFi("ssid","pass")` | used if no WiFi was saved on the hotspot |

`OTA.begin("Name", "https://your-server")` uses another server; `""` finds app.py on the local WiFi.

## Build a .bin for the dashboard
Sketch -> Export Compiled Binary -> upload `<sketch>.ino.bin`.
Every firmware you flash must include this library, or the board can no longer be updated over OTA.
