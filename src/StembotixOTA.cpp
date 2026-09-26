#include "StembotixOTA.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <DNSServer.h>

StembotixOTAClass OTA;

// ---------------- settings ----------------
static const char* s_name        = "ESP32";
static String      s_serverUrl   = STEMBOTIX_OTA_DEFAULT_SERVER;
static int         s_button      = 0;
static int         s_led         = 2;
static unsigned long s_holdMs    = 3000;
static String      s_apPass      = "12345678";
static unsigned long s_checkMs   = 1000;
static String      s_defSsid, s_defPass;
static void (*s_onOta)()         = nullptr;

static const char* SERVER_NAME = "stembotix-ota";     // app.py on the local WiFi (when serverUrl is "")
static const uint16_t SERVER_PORT = 5000;
static const uint16_t BEACON_PORT = 47800;
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;
static const unsigned long SETUP_TIMEOUT_MS = 5UL * 60UL * 1000UL;

// ---------------- state ----------------
static Preferences prefs;
static String serverBase;
static String macAddr;
static String fwVersion = "unknown";
static volatile bool bootRequest = false;             // set by the button task when BOOT is held
static bool started = false;

#define LOG(...) Serial.printf(__VA_ARGS__)


// =====================================================================
// BOOT button watcher: runs in the background, so holding BOOT works
// even if your loop() uses delay().
// =====================================================================
static void buttonTask(void*) {
  unsigned long since = 0;
  bool waitRelease = false;
  for (;;) {
    bool down = digitalRead(s_button) == LOW;
    if (waitRelease) {
      if (!down) waitRelease = false;
    } else if (down) {
      if (!since) since = millis();
      if (millis() - since >= s_holdMs) {
        bootRequest = true;
        since = 0;
        waitRelease = true;
      }
    } else {
      since = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}


// =====================================================================
// WiFi setup hotspot
// =====================================================================
static WebServer* portalServer = nullptr;
static DNSServer* portalDns = nullptr;
static String portalOptions, portalNote;

static String htmlEscape(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;"; else if (c == '\'') o += "&#39;"; else o += c;
  }
  return o;
}

static void scanForPortal() {
  LOG("[setup] scanning WiFi networks...\n");
  int n = WiFi.scanNetworks();
  portalOptions = "";
  String seen = "\n";
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0 || seen.indexOf("\n" + s + "\n") >= 0) continue;
    seen += s + "\n";
    int q = constrain(2 * (WiFi.RSSI(i) + 100), 0, 100);
    bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    portalOptions += "<option value=\"" + htmlEscape(s) + "\">" + htmlEscape(s) +
                     "  (" + String(q) + "%" + (open ? ", open" : "") + ")</option>";
  }
  WiFi.scanDelete();
  if (portalOptions.length() == 0) portalOptions = "<option value=\"\">No networks found - use Rescan or type it below</option>";
}

static String portalPage() {
  String p = F("<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>STEMbotix WiFi Setup</title><style>"
    "body{margin:0;font-family:system-ui,Segoe UI,Roboto,sans-serif;background:#F7F3F0;color:#1A1A2E}"
    ".top{background:linear-gradient(135deg,#2D2F8F,#4547B0);color:#fff;padding:22px 20px}"
    ".top b{color:#F5C518}.top small{display:block;opacity:.7;margin-top:4px;font-family:monospace}"
    ".card{background:#fff;margin:16px;padding:18px;border-radius:16px;border:1px solid #E8DDD8;box-shadow:0 4px 20px rgba(232,84,26,.1)}"
    "label{display:block;font-size:12px;font-weight:700;color:#7A6F6A;text-transform:uppercase;letter-spacing:1px;margin:14px 0 6px}"
    "select,input{width:100%;box-sizing:border-box;padding:12px;border:2px solid #E8DDD8;border-radius:10px;font-size:16px;background:#F7F3F0}"
    "button{width:100%;margin-top:18px;padding:14px;border:0;border-radius:12px;background:#E8541A;color:#fff;font-size:17px;font-weight:800}"
    ".note{background:#FFF0EB;border:1px solid #FFD4B0;color:#E8541A;padding:10px 12px;border-radius:10px;font-size:14px}"
    ".row{display:flex;justify-content:space-between;align-items:center}.row a{font-size:13px;color:#2D2F8F}"
    ".small{font-size:12px;color:#7A6F6A;margin-top:14px}"
    "</style></head><body><div class='top'><div style='font-size:20px;font-weight:800'>STEM<b>botix</b> WiFi Setup</div><small>");
  p += "Board " + macAddr + "</small></div><div class='card'>";
  if (portalNote.length()) p += "<div class='note'>" + htmlEscape(portalNote) + "</div>";
  p += F("<form method='POST' action='/save'><div class='row'><label>WiFi network</label><a href='/rescan'>&#8635; Rescan</a></div><select name='ssid'>");
  p += portalOptions;
  p += F("</select><label>Or type the network name</label><input name='ssid_manual' placeholder='only if not in the list' autocapitalize='none'>"
         "<label>WiFi password</label><input name='pass' type='password' id='pw' autocapitalize='none'>"
         "<div class='small'><input type='checkbox' style='width:auto' onclick=\"pw.type=this.checked?'text':'password'\"> show password</div>"
         "<button type='submit'>Save &amp; connect</button></form>"
         "<div class='small'>The board restarts and joins this WiFi. If the password is wrong, this hotspot opens again.</div></div></body></html>");
  return p;
}

static void redirectHome() {
  portalServer->sendHeader("Location", "/", true);
  portalServer->send(302, "text/plain", "");
}

// Runs the setup hotspot until a network is saved (then restarts). Never returns.
static void runSetupPortal(const String& note) {
  portalNote = note;
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP_STA);
  macAddr = WiFi.macAddress();
  scanForPortal();
  String apName = "STEMbotix-Setup-" + macAddr.substring(12, 14) + macAddr.substring(15, 17);
  WiFi.softAP(apName.c_str(), s_apPass.c_str());
  delay(200);
  IPAddress apIP = WiFi.softAPIP();

  portalDns = new DNSServer();
  portalDns->start(53, "*", apIP);
  portalServer = new WebServer(80);

  portalServer->on("/", HTTP_GET, []() { portalServer->send(200, "text/html", portalPage()); });
  portalServer->on("/rescan", HTTP_GET, []() { scanForPortal(); redirectHome(); });
  portalServer->on("/save", HTTP_POST, []() {
    String ssid = portalServer->arg("ssid_manual");
    ssid.trim();
    if (ssid.length() == 0) ssid = portalServer->arg("ssid");
    String pass = portalServer->arg("pass");
    if (ssid.length() == 0) {
      portalNote = "Please choose or type a WiFi network.";
      redirectHome();
      return;
    }
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.putBool("otanext", true);                  // next boot goes to OTA mode once
    portalServer->send(200, "text/html",
      "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'></head>"
      "<body style='font-family:sans-serif;padding:30px;text-align:center'><h2>Saved &#10003;</h2>"
      "<p>The board is restarting and connecting to <b>" + htmlEscape(ssid) + "</b>.</p>"
      "<p>You can disconnect from the setup hotspot now.</p></body></html>");
    LOG("[setup] saved WiFi '%s', restarting\n", ssid.c_str());
    delay(1500);
    ESP.restart();
  });
  portalServer->onNotFound([]() {
    portalServer->sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    portalServer->send(302, "text/plain", "");
  });
  portalServer->begin();

  LOG("[setup] hotspot '%s' (password %s) - connect and open http://%s\n",
      apName.c_str(), s_apPass.c_str(), apIP.toString().c_str());
  if (s_led >= 0) pinMode(s_led, OUTPUT);
  unsigned long start = millis(), blink = 0;
  for (;;) {
    portalDns->processNextRequest();
    portalServer->handleClient();
    if (s_led >= 0 && millis() - blink > 150) { blink = millis(); digitalWrite(s_led, !digitalRead(s_led)); }
    if (millis() - start > SETUP_TIMEOUT_MS && WiFi.softAPgetStationNum() == 0 &&
        prefs.getString("ssid", "").length()) {
      LOG("[setup] timeout, retrying saved WiFi\n");
      ESP.restart();
    }
    delay(2);
  }
}

static bool connectWiFi() {
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  if (ssid.length() == 0 && s_defSsid.length()) { ssid = s_defSsid; pass = s_defPass; }
  if (ssid.length() == 0) return false;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.c_str());
  LOG("Connecting to WiFi '%s'", ssid.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    LOG(".");
  }
  LOG("\n");
  if (WiFi.status() != WL_CONNECTED) {
    LOG("Could not connect to '%s'\n", ssid.c_str());
    return false;
  }
  LOG("Connected to %s\nIP address: %s\n", ssid.c_str(), WiFi.localIP().toString().c_str());
  return true;
}


// =====================================================================
// Finding the server
// =====================================================================
static bool findServer() {
  if (s_serverUrl.length() > 0) {
    serverBase = s_serverUrl;
    return true;
  }
  // local mode: app.py on the same WiFi
  for (int attempt = 0; attempt < 2; attempt++) {
    IPAddress ip = MDNS.queryHost((char*)SERVER_NAME, 2000);
    if ((uint32_t)ip != 0) {
      serverBase = "http://" + ip.toString() + ":" + String(SERVER_PORT);
      prefs.putString("srv", serverBase);
      return true;
    }
  }
  WiFiUDP udp;
  if (udp.begin(BEACON_PORT)) {
    unsigned long start = millis();
    while (millis() - start < 3000) {
      if (udp.parsePacket() > 0) {
        char buf[32] = {0};
        udp.read(buf, sizeof(buf) - 1);
        if (strncmp(buf, "STEMBOTIX-OTA ", 14) == 0) {
          int port = atoi(buf + 14);
          if (port <= 0) port = SERVER_PORT;
          serverBase = "http://" + udp.remoteIP().toString() + ":" + String(port);
          prefs.putString("srv", serverBase);
          udp.stop();
          return true;
        }
      }
      delay(20);
    }
    udp.stop();
  }
  serverBase = prefs.getString("srv", "");
  LOG("[OTA] server not found on the local WiFi%s\n", serverBase.length() ? ", trying last known address" : "");
  return serverBase.length() > 0;
}


// =====================================================================
// Talking to the server
// =====================================================================
static WiFiClient       checkPlain;          // kept open between check-ins so each one is fast
static WiFiClientSecure checkSecure;

static bool useHttps() { return serverBase.startsWith("https://"); }

static String urlEncode(const String& s) {
  String out;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') out += c;
    else { out += '%'; out += hex[(c >> 4) & 0xF]; out += hex[c & 0xF]; }
  }
  return out;
}

static bool jsonBool(const String& body, const char* key) {
  return body.indexOf(String("\"") + key + "\":true") >= 0;
}

static String jsonString(const String& body, const char* key) {
  String k = String("\"") + key + "\":\"";
  int i = body.indexOf(k);
  if (i < 0) return "";
  i += k.length();
  int j = body.indexOf('"', i);
  return (j < 0) ? "" : body.substring(i, j);
}

static void getOnce(const String& url) {
  WiFiClient plain;
  WiFiClientSecure secure;
  secure.setInsecure();
  WiFiClient& client = url.startsWith("https://") ? (WiFiClient&)secure : plain;
  HTTPClient http;
  if (http.begin(client, url)) {
    http.setTimeout(10000);
    http.GET();
    http.end();
  }
}

// Download the .bin and write it to flash; returns "" on success or an error.
static String flashFromUrl(const String& url, const String& md5) {
  WiFiClient plain;
  WiFiClientSecure secure;
  secure.setInsecure();                          // file is MD5-checked
  WiFiClient& client = url.startsWith("https://") ? (WiFiClient&)secure : plain;
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(client, url)) return "could not start the download";

  int code = http.GET();
  if (code != 200) {
    String body = http.getString();
    http.end();
    return "server answered HTTP " + String(code) + " " + body.substring(0, 80);
  }
  int total = http.getSize();
  if (total <= 0) { http.end(); return "server did not send the file size"; }
  LOG("[OTA] file size %d bytes\n", total);

  if (!Update.begin(total, U_FLASH)) {
    String e = Update.errorString();
    http.end();
    return "cannot start flash update: " + e + " (partition too small?)";
  }
  Update.setMD5(md5.c_str());

  auto* stream = http.getStreamPtr();
  static uint8_t buf[2048];
  int done = 0;
  bool first = true;
  unsigned long lastData = millis(), lastPrint = 0;

  while (done < total) {
    size_t avail = stream->available();
    if (avail) {
      int n = stream->readBytes(buf, avail < sizeof(buf) ? avail : sizeof(buf));
      if (n <= 0) continue;
      if (first) {
        first = false;
        if (buf[0] != 0xE9) {
          String got;
          for (int i = 0; i < n && i < 60; i++) got += (buf[i] >= 32 && buf[i] < 127) ? (char)buf[i] : '.';
          Update.abort();
          http.end();
          return "not an ESP32 firmware file (starts with 0x" + String(buf[0], HEX) + ": " + got + ")";
        }
      }
      if (Update.write(buf, n) != (size_t)n) {
        String e = Update.errorString();
        Update.abort();
        http.end();
        return "flash write failed: " + e;
      }
      done += n;
      lastData = millis();
      if (millis() - lastPrint > 1000) {
        lastPrint = millis();
        LOG("[OTA] %d%%  (%d / %d bytes)\n", (int)((int64_t)done * 100 / total), done, total);
      }
    } else {
      if (millis() - lastData > 20000) {
        Update.abort();
        http.end();
        return "download stalled at " + String(done) + " of " + String(total) + " bytes";
      }
      delay(2);
    }
  }
  http.end();

  if (!Update.end()) return String("verify failed: ") + Update.errorString();   // also checks the MD5
  LOG("[OTA] %d bytes written and verified\n", done);
  return "";
}

static void doUpdate(const String& newVersion) {
  LOG("[OTA] downloading version %s\n", newVersion.c_str());
  checkSecure.stop();                            // free the check-in connection first
  checkPlain.stop();
  String err = flashFromUrl(serverBase + "/firmware?mac=" + macAddr, newVersion);
  if (err.length() == 0) {
    LOG("[OTA] flashed OK, restarting\n");
    prefs.putString("fw", newVersion);
    getOnce(serverBase + "/confirm?mac=" + macAddr + "&version=" + newVersion);
    Serial.flush();
    ESP.restart();
  } else {
    LOG("[OTA] FAILED: %s\n", err.c_str());
    getOnce(serverBase + "/confirm?mac=" + macAddr + "&failed=1&err=" + urlEncode(err));
  }
}

static int checkOnce() {
  checkSecure.setInsecure();
  WiFiClient& client = useHttps() ? (WiFiClient&)checkSecure : checkPlain;
  HTTPClient http;
  http.setReuse(true);                           // keep the connection open for the next check-in
  String url = serverBase + "/check?mac=" + macAddr + "&version=" + fwVersion +
               "&ip=" + WiFi.localIP().toString() + "&app=" + urlEncode(s_name);
  if (!http.begin(client, url)) return -1;
  http.setTimeout(10000);
  int code = http.GET();
  if (code != 200) { http.end(); return code; }
  String body = http.getString();
  http.end();
  body.replace(" ", "");

  static bool saidWaiting = false;
  if (jsonBool(body, "update")) {
    saidWaiting = false;
    String v = jsonString(body, "version");
    doUpdate(v.length() ? v : String("unknown"));
  } else if (!saidWaiting) {
    saidWaiting = true;
    LOG("[OTA] up to date - waiting for new firmware from the server\n");
  }
  return 200;
}

static void checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) { LOG("[OTA] WiFi down, skipping check\n"); return; }
  if (serverBase.length() == 0 && !findServer()) return;
  int code = checkOnce();
  if (code < 0) {
    String old = serverBase;
    if (findServer() && serverBase != old) code = checkOnce();
  }
  if (code != 200) LOG("[OTA] check failed: %d (%s)\n", code, serverBase.c_str());
}


// =====================================================================
// Public API
// =====================================================================
void StembotixOTAClass::setButton(int pin)                 { s_button = pin; }
void StembotixOTAClass::setLed(int pin)                    { s_led = pin; }
void StembotixOTAClass::setHoldTime(unsigned long ms)      { s_holdMs = ms; }
void StembotixOTAClass::setHotspotPassword(const char* p)  { s_apPass = p; }
void StembotixOTAClass::setCheckInterval(unsigned long ms) { s_checkMs = ms; }
void StembotixOTAClass::setWiFi(const char* ssid, const char* pass) { s_defSsid = ssid; s_defPass = pass; }
void StembotixOTAClass::onOtaMode(void (*cb)())            { s_onOta = cb; }
String StembotixOTAClass::version()                        { return fwVersion; }

void StembotixOTAClass::begin(const char* firmwareName, const char* serverUrl) {
  if (started) return;
  started = true;
  s_name = firmwareName;
  s_serverUrl = serverUrl ? serverUrl : "";
  while (s_serverUrl.endsWith("/")) s_serverUrl.remove(s_serverUrl.length() - 1);

  prefs.begin("ota", false);
  fwVersion = prefs.getString("fw", "unknown");
  pinMode(s_button, INPUT_PULLUP);

  if (prefs.getBool("setup", false)) {             // BOOT was held in OTA mode -> WiFi setup
    prefs.putBool("setup", false);
    runSetupPortal("Setup mode - choose the WiFi this board should use.");
  }
  xTaskCreate(buttonTask, "sbx_ota_btn", 2048, nullptr, 1, nullptr);   // watches BOOT in the background

  if (prefs.getBool("otanext", false)) {           // WiFi was just saved -> OTA mode once
    prefs.putBool("otanext", false);
    startOtaMode();
  }
  LOG("[OTA] %s  firmware %s - hold BOOT %lu s for OTA mode\n", s_name, fwVersion.c_str(), s_holdMs / 1000);
}

void StembotixOTAClass::loop() {
  if (bootRequest) {
    bootRequest = false;
    startOtaMode();
  }
}

void StembotixOTAClass::startWiFiSetup() {
  prefs.putBool("setup", true);
  delay(100);
  ESP.restart();
}

void StembotixOTAClass::forgetWiFi() {
  prefs.remove("ssid");
  prefs.remove("pass");
}

void StembotixOTAClass::startOtaMode() {
  _otaMode = true;
  LOG("[OTA] OTA mode starting - your code is stopped\n");
  if (s_onOta) s_onOta();

  if (!connectWiFi()) {
    String saved = prefs.getString("ssid", "");
    runSetupPortal(saved.length()
                   ? String("Could not connect to '") + saved + "'. Check the password or choose another network."
                   : String("Welcome! Choose the WiFi this board should use."));
  }
  macAddr = WiFi.macAddress();
  findServer();
  LOG("[OTA] server %s   app %s   MAC %s   firmware %s\n",
      serverBase.c_str(), s_name, macAddr.c_str(), fwVersion.c_str());
  LOG("[OTA] OTA mode ON - waiting for firmware (hold BOOT %lu s for WiFi setup)\n", s_holdMs / 1000);

  bootRequest = false;
  checkForUpdate();
  unsigned long lastCheck = millis();
  for (;;) {                                        // only waits for firmware - never returns
    if (bootRequest) {
      LOG("[setup] BOOT held - restarting into WiFi setup\n");
      startWiFiSetup();
    }
    if (millis() - lastCheck >= s_checkMs) {
      lastCheck = millis();
      checkForUpdate();
    }
    delay(1);
  }
}
