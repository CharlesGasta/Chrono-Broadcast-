#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <time.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <esp_heap_caps.h>
#include "CARAC_TIMER_WEB_GZIP.h"

// ============================================================
// CARAC TIMER DISPLAY — ESP32-S3 MASTER
// V2.0.3 CONTROL + FLASH FIX
//
// New architecture:
// - ESP32-S3 is the timing master.
// - Clock / stopwatch / countdown / deadline keep running
//   without browser and through Wi-Fi dropouts.
// - Browser sends only commands/settings.
// - Full 64x32 frames are accepted only for animated standby.
// - Wi-Fi diagnostics + reconnect strategy.
// - Remote ESP8266 polling uses cached IP after mDNS resolution.
// - Web OTA + ArduinoOTA remain available for future versions.
// ============================================================

#define FW_VERSION "2.0.4"
#define PANEL_W 64
#define PANEL_H 32
#define PANEL_CHAIN 1

const char* HOSTNAME = "carac-timer";

// Home Wi-Fi for this USB recovery build.
const char* WIFI_SSID = "Sarah_Charles_Home";
const char* WIFI_PASSWORD = "Faussettes";

const char* RESCUE_SSID = "CARAC-TIMER-SETUP";
const char* RESCUE_PASS = "CaracTimer2026";

MatrixPanel_I2S_DMA* matrix = nullptr;
WebServer server(80);
Preferences prefs;

// ----------------------------
// Colors
// ----------------------------
uint16_t C_BLACK, C_WHITE, C_RED, C_GREEN, C_BLUE, C_YELLOW, C_ORANGE;

// ============================================================
// CARAC wordmark extracted from the validated interface SVG
// 58 x 10 RGB565 + alpha mask
// ============================================================
#define CARAC_LOGO_W 58
#define CARAC_LOGO_H 10

const uint16_t CARAC_LOGO_RGB565[580] PROGMEM = {
  0x0067, 0x1176, 0x1156, 0x0046, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1177, 0x1199,
  0x1199, 0x1155, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x18C3, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1177, 0x1199, 0x1199, 0x1155,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x7BEF, 0xE73C,
  0xFFFF, 0xFFFF, 0xE73C, 0x7BCF, 0x0000, 0x0000, 0x8410, 0xEF5D, 0xFFFF, 0xFFFF, 0xE73C, 0x6B4D,
  0x0000, 0x39E7, 0xFFFF, 0x9492, 0xDEDB, 0xFFFF, 0x9CF3, 0x2124, 0xBDF7, 0xFFDF, 0xFFFF, 0xFFFF,
  0xC618, 0x2124, 0x0000, 0x0000, 0x8430, 0xEF5D, 0xFFFF, 0xFFFF, 0xE71C, 0x738E, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0067, 0x1176, 0x1156, 0x0066, 0x0000, 0x0183,
  0x05AD, 0x060E, 0x0245, 0x0000, 0x0000, 0x0000, 0x0000, 0x8410, 0xFFFF, 0xA534, 0x528A, 0x4A69,
  0x9CD3, 0xFFFF, 0x6B6D, 0x4208, 0xCE79, 0x8410, 0x31A6, 0x4208, 0xA514, 0xFFFF, 0x2965, 0x39E7,
  0xFFFF, 0xE71C, 0x7BEF, 0x73AE, 0x4A49, 0x9CD3, 0xC618, 0x528A, 0x2965, 0x52AA, 0xE73C, 0xBDF7,
  0x0000, 0x9492, 0xFFFF, 0x9CD3, 0x4A69, 0x4A69, 0xA514, 0xFFFF, 0x5AEB, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x050C, 0x066F, 0x066F,
  0x062E, 0x0000, 0x0000, 0x0000, 0x0000, 0xE73C, 0xCE79, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x6B6D, 0xAD55, 0xC638, 0xD6BA, 0xEF5D, 0xFFFF, 0x52AA, 0x39E7, 0xFFFF, 0x6B4D,
  0x0000, 0x0000, 0x0000, 0x2945, 0x9492, 0xBDD7, 0xCE79, 0xDEFB, 0xFFDF, 0xEF5D, 0x0000, 0xF7BE,
  0xBDF7, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x04EB, 0x066F, 0x066F, 0x060E, 0x0000,
  0x0000, 0x0000, 0x0000, 0xE71C, 0xD6BA, 0x0000, 0x0000, 0x0000, 0x0000, 0x2104, 0x0000, 0x8410,
  0xFFFF, 0x9CF3, 0x7BCF, 0x6B4D, 0x8C51, 0xFFFF, 0x5ACB, 0x39E7, 0xFFFF, 0x5ACB, 0x0000, 0x0000,
  0x0000, 0xEF5D, 0xDEFB, 0x8430, 0x738E, 0x630C, 0xD69A, 0xEF5D, 0x0000, 0xF79E, 0xC638, 0x0000,
  0x0000, 0x0000, 0x0000, 0x2104, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x58E1, 0xF2A4, 0xEA84, 0x48C1, 0x0000, 0x0123, 0x054C, 0x058D, 0x0204, 0x0000, 0x0000, 0x0000,
  0x0000, 0x7BCF, 0xFFFF, 0xB596, 0x5ACB, 0x630C, 0xB596, 0xFFFF, 0x52AA, 0x9CF3, 0xFFFF, 0x632C,
  0x4228, 0x630C, 0xD69A, 0xFFFF, 0x5ACB, 0x39E7, 0xFFFF, 0x5ACB, 0x0000, 0x0000, 0x0000, 0xFFFF,
  0xC618, 0x4A49, 0x4A49, 0x8430, 0xFFDF, 0xEF5D, 0x0000, 0x8C71, 0xFFFF, 0xAD55, 0x52AA, 0x632C,
  0xB5B6, 0xFFFF, 0x528A, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xEA84, 0xFAC5,
  0xFAC5, 0xDA44, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x738E, 0xDEDB, 0xFFFF, 0xFFFF, 0xDEDB, 0x6B4D, 0x0000, 0x2104, 0xC638, 0xFFDF, 0xFFDF, 0xD69A,
  0x7BEF, 0xEF7D, 0x630C, 0x39C7, 0xEF7D, 0x52AA, 0x0000, 0x0000, 0x0000, 0x6B4D, 0xE73C, 0xFFFF,
  0xF79E, 0xA534, 0xAD55, 0xE73C, 0x0000, 0x0000, 0x7BEF, 0xDEFB, 0xFFFF, 0xFFFF, 0xD6BA, 0x630C,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xDA64, 0xFAC5, 0xFAC5, 0xD244,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x38A1, 0xCA24, 0xC223, 0x3081, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000
};

const uint8_t CARAC_LOGO_ALPHA[580] PROGMEM = {
  76, 226, 219, 62, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 230, 255,
  255, 212, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 25, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 229, 255, 255, 214,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 124, 229, 255, 255, 230, 123, 0, 0, 130, 232,
  255, 255, 231, 107, 0, 63, 255, 145, 217, 255, 156, 37, 188, 248, 255, 255, 193, 37, 0, 0,
  132, 233, 255, 255, 226, 112, 0, 0, 0, 0, 0, 0, 0, 0, 73, 222, 218, 65, 0, 61,
  225, 237, 93, 0, 0, 0, 0, 131, 255, 164, 80, 77, 153, 255, 111, 65, 204, 130, 53, 64,
  160, 255, 45, 63, 255, 224, 125, 119, 73, 154, 192, 80, 47, 84, 228, 189, 0, 146, 255, 155,
  78, 79, 162, 255, 94, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 201, 255, 255,
  244, 0, 0, 0, 0, 228, 206, 0, 0, 0, 0, 0, 0, 0, 110, 171, 198, 214, 234, 255,
  86, 63, 255, 104, 0, 0, 0, 40, 145, 184, 204, 221, 248, 232, 0, 245, 191, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 196, 255, 255, 241, 0,
  0, 0, 0, 226, 212, 0, 0, 0, 0, 34, 0, 130, 255, 158, 120, 104, 139, 255, 88, 63,
  255, 90, 0, 0, 0, 235, 222, 132, 113, 98, 211, 233, 0, 242, 198, 0, 0, 0, 0, 34,
  0, 0, 0, 0, 0, 0, 0, 0, 90, 242, 236, 74, 0, 49, 208, 221, 82, 0, 0, 0,
  0, 123, 255, 179, 88, 99, 176, 255, 85, 158, 252, 102, 68, 98, 211, 255, 88, 63, 255, 90,
  0, 0, 0, 253, 195, 75, 74, 134, 251, 234, 0, 140, 255, 170, 87, 101, 183, 255, 81, 0,
  0, 0, 0, 0, 0, 0, 234, 255, 255, 217, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  113, 216, 254, 255, 216, 107, 0, 35, 196, 250, 251, 209, 126, 238, 97, 59, 238, 84, 0, 0,
  0, 104, 230, 255, 242, 167, 170, 231, 0, 0, 124, 220, 255, 254, 212, 97, 0, 0, 0, 0,
  0, 0, 0, 0, 223, 255, 255, 208, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 60, 204, 199, 53, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};


// ----------------------------
// Master timer state
// ----------------------------
String hwMode = "clock";
bool hwRunning = false;
int64_t hwBaseMs = 0;
uint32_t hwAnchorMs = 0;
int64_t hwProgrammedMs = 300000;
bool hwOverrun = false;
bool hwContinueAfterZero = false;

uint8_t hwBrightnessPct = 80;
uint32_t hwColor = 0xFFFFFF;
uint32_t hwOverrunColor = 0xFF0000;
bool hwOverrunAuto = true;
String hwDigitSize = "medium";
String hwTimeFormat = "hms";

bool hwUnitColors = false;
uint32_t hwColorHours = 0xFFFFFF;
uint32_t hwColorMinutes = 0xFFFFFF;
uint32_t hwColorSeconds = 0xFFFFFF;
uint32_t hwColorFraction = 0xFFFFFF;
uint32_t hwColorSeparator = 0xFFFFFF;
uint32_t hwColorSign = 0xFFFFFF;

int64_t hwDeadlineEpochMs = 0;

// Browser clock options, represented as an offset from true local/NTP time.
// For a frozen manual clock, clockFrozenEpochMs is fixed.
int64_t clockOffsetMs = 0;
bool clockFrozen = false;
int64_t clockFrozenEpochMs = 0;
bool clockShowSeconds = true;
bool clock12h = false;

// ----------------------------
// Alert
// ----------------------------
bool alertActive = false;
uint32_t alertStartedMs = 0;

// ----------------------------
// Prestart
// ----------------------------
bool prestartActive = false;
String prestartStyle = "classic";
String prestartTarget = "stopwatch";
uint32_t prestartStartMs = 0;
uint32_t prestartDurationMs = 3000;
uint32_t prestartEndMs = 0;
uint32_t startBlackoutUntil = 0;

// ----------------------------
// Local standby animation engine
// Browser uploads source images/effects once; playback runs in PSRAM.
// ----------------------------
#define ANIM_MAX_ITEMS 60
#define ANIM_UPLOAD_CAPACITY 300000

struct AnimItem {
  uint8_t width;
  uint8_t height;
  uint8_t effect;      // 0 = scroll, 1 = still
  uint16_t duration10; // seconds x 10, for still images
  uint32_t pixelOffset;
};

AnimItem animItems[ANIM_MAX_ITEMS];
uint8_t animItemCount = 0;
uint8_t animSpeedPct = 12;
uint8_t animDirection = 0; // 0 left, 1 right
bool animLoaded = false;

uint8_t* animBlob = nullptr;
size_t animBlobSize = 0;

uint8_t* animUploadBuffer = nullptr;
size_t animUploadBytes = 0;
bool animUploadFailed = false;
String animUploadError = "";

uint8_t animPosition = 0;
float animElapsedUnits = 0.0f;
uint32_t animLastTickMs = 0;

// ----------------------------
// State sequencing
// ----------------------------
uint32_t stateSeq = 1;
String lastSource = "BOOT";

// ----------------------------
// Wi-Fi diagnostics
// ----------------------------
volatile uint32_t wifiDisconnectCount = 0;
volatile uint32_t wifiReconnectCount = 0;
volatile uint8_t wifiLastReason = 0;
volatile bool wifiGotIpFlag = false;
volatile bool wifiDisconnectedFlag = false;

bool rescueAP = false;
uint32_t wifiLostAt = 0;
uint32_t lastReconnectTry = 0;
uint32_t lastWifiMaintenance = 0;
bool mdnsStarted = false;
bool mdnsRestartRequested = false;

// ----------------------------
// OTA
// ----------------------------
bool otaActive = false;

// ----------------------------
// Remote ESP8266
// ----------------------------
String remoteUrl = "http://carac-remote.local";
IPAddress remoteIP;
bool remoteIPValid = false;
uint32_t lastRemoteResolve = 0;
uint32_t lastRemotePoll = 0;

bool remoteSeen = false;
uint32_t remoteCounter = 0;
String remoteLastAction = "NONE";
int remoteBattery = -1;
float remoteVoltage = 0;
bool remoteCharging = false;
int remoteRSSI = 0;
uint32_t remoteSleepTimeoutS = 0;
uint32_t remoteLastSeenMs = 0;
String remoteState = "INCONNUE";

// ----------------------------
// Diagnostics log
// ----------------------------
String diagLog[48];
uint8_t diagHead = 0;
uint8_t diagCount = 0;

// ----------------------------
// Persistence
// ----------------------------
bool prefsDirty = false;
uint32_t prefsDirtyAt = 0;

// ----------------------------
// Render scheduling
// ----------------------------
uint32_t lastDrawMs = 0;
String lastRenderKey = "";

// ============================================================
// Utilities
// ============================================================

void addLog(const String& msg) {
  Serial.println(msg);
  diagLog[diagHead] = String(millis() / 1000UL) + "s  " + msg;
  diagHead = (diagHead + 1) % 48;
  if (diagCount < 48) diagCount++;
}

void bumpState(const char* source) {
  stateSeq++;
  if (stateSeq == 0) stateSeq = 1;
  lastSource = source;
}

void markPrefsDirty() {
  prefsDirty = true;
  prefsDirtyAt = millis();
}

void addCors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
}

String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '\\' || c == '"') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c != '\r') {
      out += c;
    }
  }
  return out;
}

String i64str(int64_t v) {
  char b[32];
  snprintf(b, sizeof(b), "%lld", (long long)v);
  return String(b);
}

int64_t parseI64(const String& s) {
  return (int64_t)strtoll(s.c_str(), nullptr, 10);
}

uint32_t parseRgb(const String& s, uint32_t fallback) {
  if (s.length() == 0) return fallback;
  return strtoul(s.c_str(), nullptr, 16) & 0xFFFFFF;
}

uint16_t rgb565(uint32_t rgb) {
  return matrix->color565((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

// ============================================================
// Exact V1.7 bitmap font
// ============================================================

const char GLYPH_MINUS[7][6] = {
  "00000","00000","00000","11111","00000","00000","00000"
};
const char GLYPH_0[7][6] = {
  "01110","11011","11011","11011","11011","11011","01110"
};
const char GLYPH_1[7][6] = {
  "00100","01100","00100","00100","00100","00100","01110"
};
const char GLYPH_2[7][6] = {
  "01110","11011","00011","00110","01100","11000","11111"
};
const char GLYPH_3[7][6] = {
  "11110","00011","00011","01110","00011","00011","11110"
};
const char GLYPH_4[7][6] = {
  "00011","00111","01111","11011","11111","00011","00011"
};
const char GLYPH_5[7][6] = {
  "11111","11000","11000","11110","00011","00011","11110"
};
const char GLYPH_6[7][6] = {
  "01110","11000","11000","11110","11011","11011","01110"
};
const char GLYPH_7[7][6] = {
  "11111","00011","00110","00110","01100","01100","01100"
};
const char GLYPH_8[7][6] = {
  "01110","11011","11011","01110","11011","11011","01110"
};
const char GLYPH_9[7][6] = {
  "01110","11011","11011","01111","00011","00011","01110"
};
const char GLYPH_COLON[7][2] = {
  "0","1","1","0","1","1","0"
};
const char GLYPH_DOT[7][2] = {
  "0","0","0","0","0","1","1"
};

const char* glyphRow(char c, int row) {
  switch (c) {
    case '-': return GLYPH_MINUS[row];
    case '0': return GLYPH_0[row];
    case '1': return GLYPH_1[row];
    case '2': return GLYPH_2[row];
    case '3': return GLYPH_3[row];
    case '4': return GLYPH_4[row];
    case '5': return GLYPH_5[row];
    case '6': return GLYPH_6[row];
    case '7': return GLYPH_7[row];
    case '8': return GLYPH_8[row];
    case '9': return GLYPH_9[row];
    case ':': return GLYPH_COLON[row];
    case '.': return GLYPH_DOT[row];
    default:  return GLYPH_0[row];
  }
}

bool isSeparator(char c) {
  return c == ':' || c == '.';
}

uint32_t timerBaseColor(int64_t value) {
  if (value >= 0) return hwColor;
  if (!hwOverrunAuto) return hwOverrunColor;
  return hwColor ^ 0xFFFFFF;
}

String unitForGroup(int group) {
  if (hwTimeFormat == "hms") {
    const char* u[] = {"hours","minutes","seconds"};
    return group < 3 ? String(u[group]) : "seconds";
  }
  if (hwTimeFormat == "hmsms") {
    const char* u[] = {"hours","minutes","seconds","fraction"};
    return group < 4 ? String(u[group]) : "fraction";
  }
  if (hwTimeFormat == "ms") {
    const char* u[] = {"minutes","seconds"};
    return group < 2 ? String(u[group]) : "seconds";
  }
  if (hwTimeFormat == "msms") {
    const char* u[] = {"minutes","seconds","fraction"};
    return group < 3 ? String(u[group]) : "fraction";
  }
  if (hwTimeFormat == "sms") {
    const char* u[] = {"seconds","fraction"};
    return group < 2 ? String(u[group]) : "fraction";
  }
  if (hwTimeFormat == "hm") {
    const char* u[] = {"hours","minutes"};
    return group < 2 ? String(u[group]) : "minutes";
  }
  if (hwTimeFormat == "s") return "seconds";
  if (hwTimeFormat == "m") return "minutes";
  if (hwTimeFormat == "millis") return "fraction";
  return "seconds";
}

uint32_t unitColorFor(const String& unit) {
  if (unit == "hours") return hwColorHours;
  if (unit == "minutes") return hwColorMinutes;
  if (unit == "seconds") return hwColorSeconds;
  if (unit == "fraction") return hwColorFraction;
  return hwColor;
}

void drawBitmapTextLayer(
  const String& text,
  int64_t logicalValue,
  bool inverted,
  bool clearBackground
) {
  int digitWidth = 5;
  int height = 14;
  int colonWidth = 1;
  int gap = 2;

  if (hwDigitSize == "small") {
    digitWidth = 5; height = 7; colonWidth = 1; gap = 1;
  } else if (hwDigitSize == "large") {
    digitWidth = 8; height = 21; colonWidth = 2; gap = 1;
  }

  auto measure = [&]() {
    int w = 0;
    for (int i = 0; i < (int)text.length(); i++) {
      w += isSeparator(text[i]) ? colonWidth : digitWidth;
    }
    if (text.length() > 1) w += ((int)text.length() - 1) * gap;
    return w;
  };

  if (measure() > 62) {
    gap = 1;
    colonWidth = 1;
  }
  while (measure() > 62 && digitWidth > 3) digitWidth--;

  int width = measure();
  int left = (64 - width) / 2;
  int top = (32 - height) / 2;

  uint32_t baseRgb = timerBaseColor(logicalValue);
  uint16_t base565 = rgb565(baseRgb);

  if (clearBackground) {
    if (inverted) matrix->fillScreen(base565);
    else matrix->clearScreen();
  }

  int group = 0;

  for (int ci = 0; ci < (int)text.length(); ci++) {
    char ch = text[ci];
    int charWidth = isSeparator(ch) ? colonWidth : digitWidth;

    uint32_t charRgb = baseRgb;
    if (hwUnitColors && logicalValue >= 0) {
      if (ch == ':' || ch == '.') charRgb = hwColorSeparator;
      else if (ch == '-') charRgb = hwColorSign;
      else charRgb = unitColorFor(unitForGroup(group));
    }

    uint16_t onColor = inverted ? C_BLACK : rgb565(charRgb);

    for (int y = 0; y < height; y++) {
      int gy = (y * 7) / height;
      const char* row = glyphRow(ch, gy);
      int sourceWidth = isSeparator(ch) ? 1 : 5;

      for (int x = 0; x < charWidth; x++) {
        int gx = (x * sourceWidth) / charWidth;
        if (row[gx] == '1') {
          int px = left + x;
          int py = top + y;
          if (px >= 0 && px < 64 && py >= 0 && py < 32) {
            matrix->drawPixel(px, py, onColor);
          }
        }
      }
    }

    if (isSeparator(ch)) group++;
    left += charWidth + gap;
  }
}

void drawBitmapText(const String& text, int64_t logicalValue, bool inverted = false) {
  drawBitmapTextLayer(text, logicalValue, inverted, true);
  presentFrame();
}

void drawBitmapTextOverlay(const String& text, int64_t logicalValue, bool inverted = false) {
  drawBitmapTextLayer(text, logicalValue, inverted, false);
}

// ============================================================
// Matrix
// ============================================================

void setupMatrix() {
  HUB75_I2S_CFG cfg(PANEL_W, PANEL_H, PANEL_CHAIN);

  cfg.gpio.r1 = 4;
  cfg.gpio.g1 = 5;
  cfg.gpio.b1 = 6;
  cfg.gpio.r2 = 7;
  cfg.gpio.g2 = 15;
  cfg.gpio.b2 = 16;
  cfg.gpio.a = 18;
  cfg.gpio.b = 8;
  cfg.gpio.c = 3;
  cfg.gpio.d = 42;
  cfg.gpio.e = 9;
  cfg.gpio.lat = 40;
  cfg.gpio.oe = 2;
  cfg.gpio.clk = 41;

  cfg.clkphase = false;
  cfg.driver = HUB75_I2S_CFG::SHIFTREG;
  cfg.double_buff = true;
  cfg.i2sspeed = HUB75_I2S_CFG::HZ_16M;
  cfg.min_refresh_rate = 240;

  matrix = new MatrixPanel_I2S_DMA(cfg);

  if (!matrix->begin()) {
    Serial.println("ERREUR MATRIX");
    while (true) delay(1000);
  }

  matrix->setBrightness8((uint8_t)map(hwBrightnessPct, 0, 100, 0, 255));

  C_BLACK = matrix->color565(0, 0, 0);
  C_WHITE = matrix->color565(255, 255, 255);
  C_RED = matrix->color565(255, 0, 0);
  C_GREEN = matrix->color565(0, 255, 0);
  C_BLUE = matrix->color565(0, 0, 255);
  C_YELLOW = matrix->color565(255, 255, 0);
  C_ORANGE = matrix->color565(255, 105, 56);

  matrix->clearScreen();
  presentFrame();
}

void textCenter(const String& s, int y, uint16_t c, uint8_t size = 1) {
  matrix->setTextWrap(false);
  matrix->setTextSize(size);

  int16_t x1, y1;
  uint16_t w, h;
  matrix->getTextBounds(s, 0, y, &x1, &y1, &w, &h);

  int x = (PANEL_W - (int)w) / 2;
  matrix->setCursor(max(0, x), y);
  matrix->setTextColor(c);
  matrix->print(s);
}

inline void presentFrame() {
  matrix->flipDMABuffer();
}

void showPanelMessage(const String& text, uint16_t color, uint8_t size = 1, int y = 10) {
  matrix->clearScreen();
  textCenter(text, y, color, size);
  presentFrame();
}


void hsvWheel(uint16_t hue, uint8_t& r, uint8_t& g, uint8_t& b) {
  hue %= 1536;
  uint8_t region = hue / 256;
  uint8_t x = hue & 255;

  switch (region) {
    case 0: r = 255;     g = x;       b = 0;       break;
    case 1: r = 255 - x; g = 255;     b = 0;       break;
    case 2: r = 0;       g = 255;     b = x;       break;
    case 3: r = 0;       g = 255 - x; b = 255;     break;
    case 4: r = x;       g = 0;       b = 255;     break;
    default:r = 255;     g = 0;       b = 255 - x; break;
  }
}

void drawCaracLogoWhite(uint8_t level) {
  matrix->clearScreen();
  const int x0 = (64 - CARAC_LOGO_W) / 2;
  const int y0 = (32 - CARAC_LOGO_H) / 2;

  for (int y = 0; y < CARAC_LOGO_H; y++) {
    for (int x = 0; x < CARAC_LOGO_W; x++) {
      int i = y * CARAC_LOGO_W + x;
      uint8_t alpha = pgm_read_byte(&CARAC_LOGO_ALPHA[i]);
      if (alpha == 0) continue;

      uint16_t v = ((uint16_t)level * alpha) / 255U;
      matrix->drawPixel(x0 + x, y0 + y, matrix->color565(v, v, v));
    }
  }
}

void drawCaracLogoColor(uint8_t level) {
  matrix->clearScreen();
  const int x0 = (64 - CARAC_LOGO_W) / 2;
  const int y0 = (32 - CARAC_LOGO_H) / 2;

  for (int y = 0; y < CARAC_LOGO_H; y++) {
    for (int x = 0; x < CARAC_LOGO_W; x++) {
      int i = y * CARAC_LOGO_W + x;
      uint16_t c = pgm_read_word(&CARAC_LOGO_RGB565[i]);
      uint8_t alpha = pgm_read_byte(&CARAC_LOGO_ALPHA[i]);
      if (c == 0 || alpha == 0) continue;

      uint8_t r = ((c >> 11) & 0x1F) << 3;
      uint8_t g = ((c >> 5) & 0x3F) << 2;
      uint8_t b = (c & 0x1F) << 3;

      uint16_t scale = ((uint16_t)level * alpha) / 255U;
      r = ((uint16_t)r * scale) / 255U;
      g = ((uint16_t)g * scale) / 255U;
      b = ((uint16_t)b * scale) / 255U;

      matrix->drawPixel(x0 + x, y0 + y, matrix->color565(r, g, b));
    }
  }
}

// Full-screen white flash fading away to reveal the color CARAC logo underneath.
// whiteLevel=255 -> 100% white panel, whiteLevel=0 -> final color logo on black.
void drawCaracLogoUnderWhiteFade(uint8_t whiteLevel) {
  const int x0 = (64 - CARAC_LOGO_W) / 2;
  const int y0 = (32 - CARAC_LOGO_H) / 2;

  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 64; x++) {
      uint8_t baseR = 0, baseG = 0, baseB = 0;

      const int lx = x - x0;
      const int ly = y - y0;

      if (lx >= 0 && lx < CARAC_LOGO_W && ly >= 0 && ly < CARAC_LOGO_H) {
        const int i = ly * CARAC_LOGO_W + lx;
        const uint16_t c = pgm_read_word(&CARAC_LOGO_RGB565[i]);
        const uint8_t alpha = pgm_read_byte(&CARAC_LOGO_ALPHA[i]);

        if (c != 0 && alpha != 0) {
          uint8_t r = ((c >> 11) & 0x1F) << 3;
          uint8_t g = ((c >> 5) & 0x3F) << 2;
          uint8_t b = (c & 0x1F) << 3;

          baseR = ((uint16_t)r * alpha) / 255U;
          baseG = ((uint16_t)g * alpha) / 255U;
          baseB = ((uint16_t)b * alpha) / 255U;
        }
      }

      const uint16_t inv = 255U - whiteLevel;
      const uint8_t outR = (uint8_t)(((uint16_t)baseR * inv + (uint16_t)255U * whiteLevel) / 255U);
      const uint8_t outG = (uint8_t)(((uint16_t)baseG * inv + (uint16_t)255U * whiteLevel) / 255U);
      const uint8_t outB = (uint8_t)(((uint16_t)baseB * inv + (uint16_t)255U * whiteLevel) / 255U);

      matrix->drawPixel(x, y, matrix->color565(outR, outG, outB));
    }
  }
}

void drawLogoBurst(float radius, uint8_t intensity) {
  matrix->clearScreen();
  drawCaracLogoWhite(intensity);

  const int cx = 32;
  const int cy = 16;
  uint16_t c = matrix->color565(intensity, intensity, intensity);

  for (int a = 0; a < 24; a++) {
    float angle = a * 6.2831853f / 24.0f;
    float inner = max(2.0f, radius * 0.28f);
    int x1 = cx + cosf(angle) * inner;
    int y1 = cy + sinf(angle) * inner * 0.55f;
    int x2 = cx + cosf(angle) * radius;
    int y2 = cy + sinf(angle) * radius * 0.55f;
    matrix->drawLine(x1, y1, x2, y2, c);
  }
}

void startupAnimation() {
  matrix->setBrightness8((uint8_t)map(hwBrightnessPct, 0, 100, 0, 255));

  // 1) Smooth rainbow sweep travelling across the full panel.
  for (int frame = 0; frame < 72; frame++) {
    for (int y = 0; y < 32; y++) {
      for (int x = 0; x < 64; x++) {
        uint16_t hue = (x * 1536 / 64 + y * 8 + frame * 24) % 1536;
        uint8_t r, g, b;
        hsvWheel(hue, r, g, b);
        matrix->drawPixel(x, y, matrix->color565(r, g, b));
      }
    }
    presentFrame();
    delay(24);
  }

  // 2) Fast RGB flashes.
  const uint16_t rgbFlashes[] = {C_RED, C_GREEN, C_BLUE};
  for (int i = 0; i < 3; i++) {
    matrix->fillScreen(rgbFlashes[i]);
    presentFrame();
    delay(75);
    matrix->clearScreen();
    presentFrame();
    delay(35);
  }

  // 3) Three white flashes.
  for (int i = 0; i < 3; i++) {
    matrix->fillScreen(C_WHITE);
    presentFrame();
    delay(90);
    matrix->clearScreen();
    presentFrame();
    delay(105);
  }

  // 4) Real CARAC logo, black -> white, smooth 0 to 100%.
  for (int level = 0; level <= 255; level += 6) {
    drawCaracLogoWhite((uint8_t)level);
    presentFrame();
    delay(32);
  }
  drawCaracLogoWhite(255);
  presentFrame();
  delay(180);

  // 5) Clean full-screen white flash, then smooth fade-down.
  // The color CARAC logo is already "under" the white layer and is progressively revealed.
  matrix->fillScreen(C_WHITE);
  presentFrame();
  delay(95);

  for (int white = 255; white >= 0; white -= 10) {
    drawCaracLogoUnderWhiteFade((uint8_t)white);
    presentFrame();
    delay(28);
  }

  // 6) Final validated CARAC logo in color.
  drawCaracLogoColor(255);
  presentFrame();
  delay(700);
}

// ============================================================
// Time master
// ============================================================

int64_t systemEpochMs() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (int64_t)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

int64_t localClockEpochMs() {
  if (clockFrozen) return clockFrozenEpochMs;
  return systemEpochMs() + clockOffsetMs;
}

int64_t localTimerValue() {
  if (hwMode == "deadline" && hwDeadlineEpochMs > 0 && hwRunning) {
    int64_t v = hwDeadlineEpochMs - systemEpochMs();
    if (!hwContinueAfterZero && v < 0) v = 0;
    return v;
  }

  int64_t v = hwBaseMs;

  if (hwRunning) {
    int64_t elapsed = (int64_t)(uint32_t)(millis() - hwAnchorMs);

    if (hwMode == "stopwatch") v += elapsed;
    else if (hwMode == "countdown" || hwMode == "deadline") v -= elapsed;
  }

  if (hwMode == "countdown" && !hwOverrun && v < 0) v = 0;
  if (hwMode == "deadline" && !hwContinueAfterZero && v < 0) v = 0;

  return v;
}

void pauseLocal(const char* source) {
  if (!hwRunning) return;

  hwBaseMs = localTimerValue();
  hwRunning = false;
  hwAnchorMs = millis();

  bumpState(source);
  addLog(String(source) + " PAUSE");
}

void playLocal(const char* source) {
  if (hwRunning) return;

  if (hwMode == "deadline" && hwDeadlineEpochMs > 0) {
    hwBaseMs = hwDeadlineEpochMs - systemEpochMs();
  }

  hwAnchorMs = millis();
  hwRunning = true;

  bumpState(source);
  addLog(String(source) + " PLAY");
}

void resetLocal(const char* source) {
  hwRunning = false;

  if (hwMode == "stopwatch") {
    hwBaseMs = 0;
  } else if (hwMode == "countdown") {
    hwBaseMs = hwProgrammedMs;
  } else if (hwMode == "deadline" && hwDeadlineEpochMs > 0) {
    hwBaseMs = hwDeadlineEpochMs - systemEpochMs();
  }

  hwAnchorMs = millis();

  bumpState(source);
  addLog(String(source) + " RESET");
}

void setSnapshotState(int64_t valueMs, bool running, const char* source) {
  hwBaseMs = valueMs;
  hwAnchorMs = millis();
  hwRunning = running;
  bumpState(source);
}

// ============================================================
// Formatting
// ============================================================

String formatTimer(int64_t value) {
  bool neg = value < 0;
  uint64_t a = neg ? (uint64_t)(-value) : (uint64_t)value;

  uint64_t centi = (a / 10ULL) % 100ULL;
  uint64_t sec = a / 1000ULL;

  unsigned h = sec / 3600ULL;
  unsigned m = (sec / 60ULL) % 60ULL;
  unsigned s = sec % 60ULL;

  char b[40];

  if (hwTimeFormat == "hmsms") {
    snprintf(b, sizeof(b), "%s%02u:%02u:%02u.%02llu",
             neg ? "-" : "", h, m, s, (unsigned long long)centi);
  } else if (hwTimeFormat == "msms") {
    snprintf(b, sizeof(b), "%s%02llu:%02u.%02llu",
             neg ? "-" : "", (unsigned long long)(sec / 60ULL), s,
             (unsigned long long)centi);
  } else if (hwTimeFormat == "ms") {
    snprintf(b, sizeof(b), "%s%02llu:%02u",
             neg ? "-" : "", (unsigned long long)(sec / 60ULL), s);
  } else if (hwTimeFormat == "sms") {
    snprintf(b, sizeof(b), "%s%llu.%02llu",
             neg ? "-" : "", (unsigned long long)sec,
             (unsigned long long)centi);
  } else if (hwTimeFormat == "hm") {
    snprintf(b, sizeof(b), "%s%02u:%02u", neg ? "-" : "", h, m);
  } else if (hwTimeFormat == "s") {
    snprintf(b, sizeof(b), "%s%llu",
             neg ? "-" : "", (unsigned long long)sec);
  } else if (hwTimeFormat == "m") {
    snprintf(b, sizeof(b), "%s%llu",
             neg ? "-" : "", (unsigned long long)(sec / 60ULL));
  } else if (hwTimeFormat == "millis") {
    snprintf(b, sizeof(b), "%s%02llu",
             neg ? "-" : "", (unsigned long long)centi);
  } else {
    snprintf(b, sizeof(b), "%s%02u:%02u:%02u",
             neg ? "-" : "", h, m, s);
  }

  return String(b);
}

String formatClock() {
  int64_t epoch = localClockEpochMs();

  time_t secEpoch = (time_t)(epoch / 1000LL);
  struct tm t;
  localtime_r(&secEpoch, &t);

  int hour = t.tm_hour;
  if (clock12h) {
    hour %= 12;
    if (hour == 0) hour = 12;
  }

  unsigned h = (unsigned)hour;
  unsigned m = (unsigned)t.tm_min;
  unsigned s = (unsigned)t.tm_sec;
  unsigned centi = (unsigned)((epoch % 1000LL + 1000LL) % 1000LL) / 10U;

  char b[32];

  if (hwTimeFormat == "hmsms") {
    snprintf(b, sizeof(b), "%02u:%02u:%02u.%02u", h, m, s, centi);
  } else if (hwTimeFormat == "msms") {
    snprintf(b, sizeof(b), "%02u:%02u.%02u", m, s, centi);
  } else if (hwTimeFormat == "ms") {
    snprintf(b, sizeof(b), "%02u:%02u", m, s);
  } else if (hwTimeFormat == "sms") {
    snprintf(b, sizeof(b), "%02u.%02u", s, centi);
  } else if (hwTimeFormat == "hm") {
    snprintf(b, sizeof(b), "%02u:%02u", h, m);
  } else if (hwTimeFormat == "s") {
    snprintf(b, sizeof(b), "%02u", s);
  } else if (hwTimeFormat == "m") {
    snprintf(b, sizeof(b), "%02u", m);
  } else if (hwTimeFormat == "millis") {
    snprintf(b, sizeof(b), "%02u", centi);
  } else {
    snprintf(b, sizeof(b), "%02u:%02u:%02u", h, m, s);
  }

  return String(b);
}

// ============================================================
// Alert / prestart
// ============================================================

bool alertInvertedNow() {
  if (!alertActive) return false;  return ((millis() - alertStartedMs) / 500UL) % 2UL == 0;
}

void drawCircleFilled(int cx, int cy, float r, uint16_t color) {
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 64; x++) {
      float dx = x - cx;
      float dy = y - cy;
      if (sqrtf(dx * dx + dy * dy) <= r) {
        matrix->drawPixel(x, y, color);
      }
    }
  }
}

void drawCircleOutline(float cx, float cy, float r, uint16_t color) {
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 64; x++) {
      float dx = x - cx;
      float dy = y - cy;
      float d = sqrtf(dx * dx + dy * dy);
      if (fabsf(d - r) < 0.65f) matrix->drawPixel(x, y, color);
    }
  }
}

void clearCenterForPrestartNumber() {
  for (int y = 7; y < 25; y++) {
    for (int x = 23; x < 41; x++) {
      matrix->drawPixel(x, y, C_BLACK);
    }
  }
}

void drawPrestart() {
  uint32_t now = millis();

  if (!prestartActive) return;

  if ((int32_t)(now - prestartEndMs) >= 0) {
    prestartActive = false;

    if (prestartStyle == "lights") {
      startBlackoutUntil = millis() + 180UL;
    }

    hwMode = prestartTarget;
    hwAnchorMs = prestartEndMs;
    hwRunning = true;

    bumpState("PRESTART");
    addLog("PRESTART -> PLAY");
    return;
  }

  uint32_t remaining = prestartEndMs - now;
  float progress = 1.0f - ((float)remaining / (float)(prestartDurationMs > 0 ? prestartDurationMs : 1));
  progress = constrain(progress, 0.0f, 1.0f);

  matrix->clearScreen();

  if (prestartStyle == "lights") {
    int count = min(5, (int)((now - prestartStartMs) / 1000UL) + 1);

    for (int col = 0; col < 5; col++) {
      for (int row = 0; row < 2; row++) {
        uint16_t c = col < count ? C_RED : matrix->color565(16, 16, 16);
        drawCircleFilled(8 + col * 12, 10 + row * 12, 4.5f, c);
      }
    }

    presentFrame();
    return;
  }

  int n = max(1, (int)ceilf(remaining / 1000.0f));
  String number = String(n);

  if (prestartStyle == "fill") {
    int h = floorf(progress * 32.0f);
    uint16_t c = rgb565(hwColor);

    for (int y = 32 - h; y < 32; y++) {
      for (int x = 0; x < 64; x++) matrix->drawPixel(x, y, c);
    }

    clearCenterForPrestartNumber();

    String oldSize = hwDigitSize;
    uint32_t oldColor = hwColor;
    hwDigitSize = "medium";
    hwColor = 0xFFFFFF;
    drawBitmapTextOverlay(number, 1, false);
    hwColor = oldColor;
    hwDigitSize = oldSize;

    presentFrame();
    return;
  }

  if (prestartStyle == "cinema") {
    float angle = (1.0f - ((remaining % 1000UL) / 1000.0f)) * 6.2831853f;

    for (int y = 0; y < 32; y++) {
      for (int x = 0; x < 64; x++) {
        float dx = x - 31.5f;
        float dy = y - 15.5f;
        float a = atan2f(dy, dx) + 1.5707963f;
        if (a < 0) a += 6.2831853f;

        if (sqrtf(dx * dx + dy * dy) < 14.0f && a < angle) {
          matrix->drawPixel(x, y, matrix->color565(86, 86, 86));
        }
      }
    }

    for (int x = 0; x < 64; x++) matrix->drawPixel(x, 15, matrix->color565(64, 64, 64));
    for (int y = 0; y < 32; y++) matrix->drawPixel(31, y, matrix->color565(64, 64, 64));

    drawCircleOutline(31.5f, 15.5f, 14.0f, C_WHITE);
    drawCircleOutline(31.5f, 15.5f, 12.0f, matrix->color565(160, 160, 160));

    clearCenterForPrestartNumber();

    String oldSize = hwDigitSize;
    uint32_t oldColor = hwColor;
    hwDigitSize = "medium";
    hwColor = 0xFFFFFF;
    drawBitmapTextOverlay(number, 1, false);
    hwColor = oldColor;
    hwDigitSize = oldSize;

    presentFrame();
    return;
  }

  if (prestartStyle == "broadcast") {
    uint16_t c = rgb565(hwColor);
    uint16_t grey = matrix->color565(70, 80, 92);

    for (int x = 2; x < 62; x++) {
      matrix->drawPixel(x, 2, grey);
      matrix->drawPixel(x, 29, grey);
    }

    for (int y = 2; y < 30; y++) {
      matrix->drawPixel(2, y, c);
      matrix->drawPixel(61, y, c);
    }

    int width = floorf(progress * 54.0f);
    for (int y = 26; y < 28; y++) {
      for (int x = 5; x < 5 + width; x++) matrix->drawPixel(x, y, c);
    }

    for (int i = 0; i < 10; i++) {
      matrix->drawPixel(5 + i * 6, 5, i < ceilf(progress * 10.0f) ? c : grey);
    }

    String oldSize = hwDigitSize;
    uint32_t oldColor = hwColor;
    hwDigitSize = "medium";
    hwColor = 0xFFFFFF;
    drawBitmapTextOverlay(number, 1, false);
    hwColor = oldColor;
    hwDigitSize = oldSize;

    presentFrame();
    return;
  }

  // Classic
  String oldSize = hwDigitSize;
  hwDigitSize = "medium";
  drawBitmapTextOverlay(number, 1, false);
  hwDigitSize = oldSize;
  presentFrame();
}

// ============================================================
// Local animation engine
// ============================================================

uint16_t readU16LE(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

void freeAnimationBlob() {
  if (animBlob) {
    free(animBlob);
    animBlob = nullptr;
  }
  animBlobSize = 0;
  animLoaded = false;
  animItemCount = 0;
}

void resetAnimationPlayback() {
  animPosition = 0;
  animElapsedUnits = 0.0f;
  animLastTickMs = millis();
  lastRenderKey = "";
}

bool installAnimation(uint8_t* buffer, size_t length, String& error) {
  if (!buffer || length < 16) {
    error = "fichier trop court";
    return false;
  }

  const char expected[8] = {'C','A','T','A','N','I','M','1'};
  for (int i = 0; i < 8; i++) {
    if (buffer[i] != (uint8_t)expected[i]) {
      error = "signature invalide";
      return false;
    }
  }

  if (buffer[8] != 1) {
    error = "version animation non supportee";
    return false;
  }

  uint8_t count = buffer[9];
  if (count == 0 || count > ANIM_MAX_ITEMS) {
    error = "nombre d'elements invalide";
    return false;
  }

  AnimItem parsed[ANIM_MAX_ITEMS];
  size_t offset = 16;

  for (uint8_t i = 0; i < count; i++) {
    if (offset + 8 > length) {
      error = "entete element tronquee";
      return false;
    }

    uint8_t w = buffer[offset];
    uint8_t h = buffer[offset + 1];
    uint8_t effect = buffer[offset + 2];
    uint16_t duration10 = readU16LE(buffer + offset + 4);
    uint16_t pixels = readU16LE(buffer + offset + 6);

    offset += 8;

    if (w == 0 || w > 64 || h == 0 || h > 32) {
      error = "dimensions invalides";
      return false;
    }

    if (pixels != (uint16_t)(w * h)) {
      error = "taille pixels invalide";
      return false;
    }

    size_t bytes = (size_t)pixels * 2U;
    if (offset + bytes > length) {
      error = "donnees pixels tronquees";
      return false;
    }

    parsed[i].width = w;
    parsed[i].height = h;
    parsed[i].effect = effect ? 1 : 0;
    parsed[i].duration10 = (duration10 < 1 ? 1 : duration10);
    parsed[i].pixelOffset = offset;

    offset += bytes;
  }

  freeAnimationBlob();

  animBlob = buffer;
  animBlobSize = length;
  animItemCount = count;
  animSpeedPct = constrain(buffer[10], 0, 100);
  animDirection = buffer[11] ? 1 : 0;

  for (uint8_t i = 0; i < count; i++) animItems[i] = parsed[i];

  animLoaded = true;
  resetAnimationPlayback();
  return true;
}

const uint16_t* animPixels(const AnimItem& item) {
  return reinterpret_cast<const uint16_t*>(animBlob + item.pixelOffset);
}

float animItemLengthUnits(const AnimItem& item) {
  if (item.effect == 0) {
    return (float)(item.width + 80) * 10.0f;
  }
  return (float)item.duration10 * 25.0f;
}

void decode565(uint16_t c, float& r, float& g, float& b) {
  if (c == 0) {
    r = g = b = 0.0f;
    return;
  }
  r = (float)(((c >> 11) & 0x1F) << 3);
  g = (float)(((c >> 5) & 0x3F) << 2);
  b = (float)((c & 0x1F) << 3);
}

void drawAnimationInterpolated() {
  if (!animLoaded || animItemCount == 0 || !animBlob) {
    matrix->clearScreen();
    textCenter("CARAC", 8, rgb565(hwColor), 2);
    presentFrame();
    return;
  }

  uint32_t now = millis();
  if (animLastTickMs == 0) animLastTickMs = now;
  uint32_t dt = now - animLastTickMs;
  animLastTickMs = now;

  const AnimItem* item = &animItems[animPosition % animItemCount];

  if (animSpeedPct > 0) {
    animElapsedUnits += (float)dt * ((float)animSpeedPct / 100.0f);

    float length = animItemLengthUnits(*item);
    while (animElapsedUnits >= length) {
      animElapsedUnits -= length;
      animPosition = (animPosition + 1) % animItemCount;
      item = &animItems[animPosition];
      length = animItemLengthUnits(*item);
    }
  }

  const uint16_t* src = animPixels(*item);
  int top = (32 - item->height) / 2;

  if (item->effect == 1) {
    // Still image.
    matrix->clearScreen();
    int left = (64 - item->width) / 2;

    for (int sy = 0; sy < item->height; sy++) {
      int dy = top + sy;
      if (dy < 0 || dy >= 32) continue;

      for (int sx = 0; sx < item->width; sx++) {
        int dx = left + sx;
        if (dx < 0 || dx >= 64) continue;

        uint16_t c = src[sy * item->width + sx];
        if (c != 0) matrix->drawPixel(dx, dy, c);
      }
    }
    presentFrame();
    return;
  }

  // Scroll effect.
  // Instead of moving only in whole LED steps, sample at a fractional
  // position and interpolate between adjacent source pixels. This produces
  // a much smoother visual sweep on the physical 4 mm pixel grid.
  float progressPixels = animElapsedUnits / 10.0f;
  float leftF = animDirection ?
    (-(float)item->width + progressPixels) :
    (64.0f - progressPixels);

  for (int y = 0; y < 32; y++) {
    int sy = y - top;

    for (int x = 0; x < 64; x++) {
      if (sy < 0 || sy >= item->height) {
        matrix->drawPixel(x, y, C_BLACK);
        continue;
      }

      float srcX = (float)x - leftF;
      int s0 = (int)floorf(srcX);
      float frac = srcX - (float)s0;
      int s1 = s0 + 1;

      uint16_t c0 = 0;
      uint16_t c1 = 0;

      if (s0 >= 0 && s0 < item->width) c0 = src[sy * item->width + s0];
      if (s1 >= 0 && s1 < item->width) c1 = src[sy * item->width + s1];

      float r0, g0, b0, r1, g1, b1;
      decode565(c0, r0, g0, b0);
      decode565(c1, r1, g1, b1);

      uint8_t r = (uint8_t)constrain((int)roundf(r0 * (1.0f - frac) + r1 * frac), 0, 255);
      uint8_t g = (uint8_t)constrain((int)roundf(g0 * (1.0f - frac) + g1 * frac), 0, 255);
      uint8_t b = (uint8_t)constrain((int)roundf(b0 * (1.0f - frac) + b1 * frac), 0, 255);

      matrix->drawPixel(x, y, matrix->color565(r, g, b));
    }
  }

  presentFrame();
}

// ============================================================
// Display
// ============================================================

void drawDisplay() {
  if (otaActive) return;

  // Local standby animation gets its own ~60 fps render loop.
  if (hwMode == "standby" && animLoaded) {
    if (millis() - lastDrawMs < 16UL) return;
    lastDrawMs = millis();
    matrix->setBrightness8((uint8_t)map(hwBrightnessPct, 0, 100, 0, 255));
    drawAnimationInterpolated();
    return;
  }

  const bool precise =
    hwTimeFormat == "hmsms" ||
    hwTimeFormat == "msms" ||
    hwTimeFormat == "sms" ||
    hwTimeFormat == "millis";

  const uint32_t interval = precise ? 30UL : 60UL;

  if (millis() - lastDrawMs < interval) return;
  lastDrawMs = millis();

  matrix->setBrightness8((uint8_t)map(hwBrightnessPct, 0, 100, 0, 255));

  if (prestartActive) {
    lastRenderKey = "";
    drawPrestart();
    return;
  }

  if ((int32_t)(millis() - startBlackoutUntil) < 0) {
    if (lastRenderKey != "BLACKOUT") {
      matrix->clearScreen();
      presentFrame();
      lastRenderKey = "BLACKOUT";
    }
    return;
  }

  if (hwMode == "off") {
    if (lastRenderKey != "OFF") {
      matrix->clearScreen();
      presentFrame();
      lastRenderKey = "OFF";
    }
    return;
  }

  if (hwMode == "standby") {
    if (lastRenderKey != "STANDBY_DEFAULT") {
      matrix->clearScreen();
      textCenter("CARAC", 8, rgb565(hwColor), 2);
      presentFrame();
      lastRenderKey = "STANDBY_DEFAULT";
    }
    return;
  }

  if (hwMode == "clock") {
    String text = formatClock();
    bool inv = alertInvertedNow();

    String key =
      "CLOCK|" + text + "|" +
      String(hwBrightnessPct) + "|" +
      String(hwColor) + "|" +
      hwDigitSize + "|" +
      (inv ? "1" : "0");

    if (key != lastRenderKey) {
      drawBitmapText(text, 0, inv);
      lastRenderKey = key;
    }
    return;
  }

  if (hwMode == "stopwatch" || hwMode == "countdown" || hwMode == "deadline") {
    int64_t value = localTimerValue();

    if (hwRunning) {
      if (hwMode == "countdown" && !hwOverrun && value <= 0) {
        hwBaseMs = 0;
        hwRunning = false;
        hwAnchorMs = millis();
        value = 0;
        bumpState("AUTO");
      }

      if (hwMode == "deadline" && !hwContinueAfterZero && value <= 0) {
        hwBaseMs = 0;
        hwRunning = false;
        hwAnchorMs = millis();
        value = 0;
        bumpState("AUTO");
      }
    }

    String text = formatTimer(value);
    bool inv = alertInvertedNow();

    String key =
      hwMode + "|" + text + "|" +
      String(hwBrightnessPct) + "|" +
      String(timerBaseColor(value)) + "|" +
      hwDigitSize + "|" +
      hwTimeFormat + "|" +
      (hwUnitColors ? "1" : "0") + "|" +
      String(hwColorHours) + "|" +
      String(hwColorMinutes) + "|" +
      String(hwColorSeconds) + "|" +
      String(hwColorFraction) + "|" +
      String(hwColorSeparator) + "|" +
      String(hwColorSign) + "|" +
      (inv ? "1" : "0");

    if (key != lastRenderKey) {
      drawBitmapText(text, value, inv);
      lastRenderKey = key;
    }
    return;
  }

  if (lastRenderKey != "UNKNOWN") {
    matrix->clearScreen();
    presentFrame();
    lastRenderKey = "UNKNOWN";
  }
}

// ============================================================
// Preferences
// ============================================================

void savePrefsNow() {
  prefs.begin("carac19", false);

  prefs.putString("mode", hwMode);
  prefs.putLong64("programmed", hwProgrammedMs);
  prefs.putBool("overrun", hwOverrun);
  prefs.putBool("afterzero", hwContinueAfterZero);

  prefs.putUChar("bright", hwBrightnessPct);
  prefs.putUInt("color", hwColor);
  prefs.putUInt("ovcolor", hwOverrunColor);
  prefs.putBool("ovauto", hwOverrunAuto);
  prefs.putString("digits", hwDigitSize);
  prefs.putString("format", hwTimeFormat);

  prefs.putBool("unitcolors", hwUnitColors);
  prefs.putUInt("ch", hwColorHours);
  prefs.putUInt("cm", hwColorMinutes);
  prefs.putUInt("cs", hwColorSeconds);
  prefs.putUInt("cf", hwColorFraction);
  prefs.putUInt("csep", hwColorSeparator);
  prefs.putUInt("csign", hwColorSign);

  prefs.putString("remote", remoteUrl);

  prefs.end();

  prefsDirty = false;
}

void loadPrefs() {
  prefs.begin("carac19", true);

  hwMode = prefs.getString("mode", "clock");
  hwProgrammedMs = prefs.getLong64("programmed", 300000);
  hwOverrun = prefs.getBool("overrun", false);
  hwContinueAfterZero = prefs.getBool("afterzero", false);

  hwBrightnessPct = prefs.getUChar("bright", 80);
  hwColor = prefs.getUInt("color", 0xFFFFFF);
  hwOverrunColor = prefs.getUInt("ovcolor", 0xFF0000);
  hwOverrunAuto = prefs.getBool("ovauto", true);
  hwDigitSize = prefs.getString("digits", "medium");
  hwTimeFormat = prefs.getString("format", "hms");

  hwUnitColors = prefs.getBool("unitcolors", false);
  hwColorHours = prefs.getUInt("ch", 0xFFFFFF);
  hwColorMinutes = prefs.getUInt("cm", 0xFFFFFF);
  hwColorSeconds = prefs.getUInt("cs", 0xFFFFFF);
  hwColorFraction = prefs.getUInt("cf", 0xFFFFFF);
  hwColorSeparator = prefs.getUInt("csep", 0xFFFFFF);
  hwColorSign = prefs.getUInt("csign", 0xFFFFFF);

  remoteUrl = prefs.getString("remote", "http://carac-remote.local");

  prefs.end();

  hwBaseMs = (hwMode == "countdown") ? hwProgrammedMs : 0;
  hwRunning = false;
  hwAnchorMs = millis();
}

void servicePrefs() {
  if (prefsDirty && millis() - prefsDirtyAt > 3000UL) {
    savePrefsNow();
  }
}

// ============================================================
// Wi-Fi
// ============================================================

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      wifiReconnectCount++;
      wifiGotIpFlag = true;
      mdnsRestartRequested = true;
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      wifiDisconnectCount++;
      wifiLastReason = info.wifi_sta_disconnected.reason;
      wifiDisconnectedFlag = true;
      if (wifiLostAt == 0) wifiLostAt = millis();
      remoteIPValid = false;
      break;

    default:
      break;
  }
}

void stopMdns() {
  if (mdnsStarted) {
    MDNS.end();
    mdnsStarted = false;
  }
}

void startMdns() {
  stopMdns();

  if (WiFi.status() != WL_CONNECTED) return;

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    mdnsStarted = true;
    addLog("mDNS actif : carac-timer.local");
  }
}

bool connectWifiInitial() {
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(HOSTNAME);

  WiFi.onEvent(onWiFiEvent);

  addLog("Connexion Wi-Fi : " + String(WIFI_SSID));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t started = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - started < 20000UL) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiLostAt = 0;
    addLog("Wi-Fi OK " + WiFi.localIP().toString() + " / " + WiFi.SSID() +
           " / RSSI " + String(WiFi.RSSI()) + " dBm");
    return true;
  }

  addLog("Wi-Fi initial indisponible");
  return false;
}

void startRescueAP() {
  if (rescueAP) return;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(RESCUE_SSID, RESCUE_PASS);
  rescueAP = true;

  addLog("AP secours actif : " + WiFi.softAPIP().toString());
}

void stopRescueAP() {
  if (!rescueAP) return;

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  rescueAP = false;

  addLog("AP secours arrete");
}

void maintainWifi() {
  if (millis() - lastWifiMaintenance < 1000UL) return;
  lastWifiMaintenance = millis();

  if (wifiGotIpFlag) {
    wifiGotIpFlag = false;
    wifiLostAt = 0;

    addLog("Wi-Fi reconnecte : " + WiFi.localIP().toString() +
           " / RSSI " + String(WiFi.RSSI()) + " dBm");

    if (rescueAP) {
      // Give STA a few seconds to settle, then close AP.
      static uint32_t gotIpAt = millis();
      if (millis() - gotIpAt > 5000UL) stopRescueAP();
    }

    mdnsRestartRequested = true;
  }

  if (wifiDisconnectedFlag) {
    wifiDisconnectedFlag = false;
    addLog("Wi-Fi perdu · raison " + String(wifiLastReason));
  }

  if (mdnsRestartRequested && WiFi.status() == WL_CONNECTED) {
    mdnsRestartRequested = false;
    startMdns();
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiLostAt = 0;
    return;
  }

  if (wifiLostAt == 0) wifiLostAt = millis();

  if (millis() - lastReconnectTry >= 5000UL) {
    lastReconnectTry = millis();
    WiFi.reconnect();
  }

  // Do not start rescue AP immediately.
  // Short Wi-Fi glitches should not force a mode change.
  if (!rescueAP && millis() - wifiLostAt >= 45000UL) {
    startRescueAP();
  }
}

// ============================================================
// NTP
// ============================================================

void setupTimeSync() {
  configTzTime(
    "CET-1CEST,M3.5.0,M10.5.0/3",
    "pool.ntp.org",
    "time.google.com",
    "time.cloudflare.com"
  );
}

// ============================================================
// Remote ESP8266
// ============================================================

String jsonField(const String& p, const String& key) {
  String k = "\"" + key + "\":";
  int i = p.indexOf(k);

  if (i < 0) return "";

  i += k.length();

  while (i < (int)p.length() && p[i] == ' ') i++;

  if (i < (int)p.length() && p[i] == '"') {
    int e = p.indexOf('"', i + 1);
    if (e < 0) return "";
    return p.substring(i + 1, e);
  }

  int e = i;
  while (e < (int)p.length() && p[e] != ',' && p[e] != '}') e++;

  return p.substring(i, e);
}

void applyRemoteAction(const String& action) {
  if (!(hwMode == "stopwatch" || hwMode == "countdown" || hwMode == "deadline")) return;

  if (prestartActive) {
    prestartActive = false;
  }

  if (action == "PLAY") {
    if (!hwRunning) playLocal("REMOTE");
  } else if (action == "PAUSE") {
    if (hwRunning) pauseLocal("REMOTE");
  } else if (action == "RESET") {
    resetLocal("REMOTE");
  } else {
    return;
  }
}

bool resolveRemoteIp() {
  if (WiFi.status() != WL_CONNECTED || !mdnsStarted) return false;

  IPAddress ip = MDNS.queryHost("carac-remote", 800);

  if ((uint32_t)ip == 0) return false;

  remoteIP = ip;
  remoteIPValid = true;

  addLog("Remote resolue : " + remoteIP.toString());
  return true;
}

void pollRemote() {
  if (WiFi.status() != WL_CONNECTED) return;

  const uint32_t now = millis();

  // Cache the remote IP. Avoid resolving mDNS for every command poll.
  if (!remoteIPValid) {
    if (now - lastRemoteResolve < 15000UL) return;
    lastRemoteResolve = now;

    if (!resolveRemoteIp()) {
      if (remoteSeen) {
        uint32_t age = now - remoteLastSeenMs;

        if (remoteSleepTimeoutS > 0 &&
            age < remoteSleepTimeoutS * 1000UL + 25000UL) {
          remoteState = "EN VEILLE";
        } else if (age >= 3000UL) {
          remoteState = "ETEINTE";
        }
      }
      return;
    }
  }

  // About 2 polls/sec. Payload is tiny and IP is already cached.
  if (now - lastRemotePoll < 500UL) return;
  lastRemotePoll = now;

  HTTPClient http;
  http.setConnectTimeout(120);
  http.setTimeout(220);
  http.setReuse(false);

  String url = "http://" + remoteIP.toString() + "/status";

  if (!http.begin(url)) {
    remoteIPValid = false;
    return;
  }

  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();

    uint32_t counter = (uint32_t)jsonField(payload, "counter").toInt();
    String action = jsonField(payload, "last_action");

    if (remoteSeen && counter > remoteCounter) {
      applyRemoteAction(action);
    }

    remoteSeen = true;
    remoteCounter = counter;
    remoteLastAction = action;

    remoteBattery = jsonField(payload, "battery_percent").toInt();
    remoteVoltage = jsonField(payload, "battery_voltage").toFloat();
    remoteCharging = jsonField(payload, "battery_charging") == "true";
    remoteRSSI = jsonField(payload, "rssi").toInt();
    remoteSleepTimeoutS = (uint32_t)jsonField(payload, "sleep_timeout_s").toInt();

    remoteLastSeenMs = now;
    remoteState = "EN LIGNE";
  } else {
    remoteIPValid = false;

    if (remoteSeen) {
      uint32_t age = now - remoteLastSeenMs;

      if (remoteSleepTimeoutS > 0 &&
          age < remoteSleepTimeoutS * 1000UL + 25000UL) {
        remoteState = "EN VEILLE";
      } else if (age >= 3000UL) {
        remoteState = "ETEINTE";
      }
    }
  }

  http.end();
}

// ============================================================
// JSON status
// ============================================================

String statusJson() {
  String j = "{";

  j += "\"ok\":true,";
  j += "\"firmware\":\"" + String(FW_VERSION) + "\",";
  j += "\"state_seq\":" + String(stateSeq) + ",";
  j += "\"last_source\":\"" + jsonEscape(lastSource) + "\",";

  j += "\"ip\":\"" +
       (WiFi.status() == WL_CONNECTED ?
          WiFi.localIP().toString() :
          WiFi.softAPIP().toString()) +
       "\",";

  j += "\"wifi_connected\":" +
       String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";

  j += "\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\",";
  j += "\"rssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) + ",";
  j += "\"wifi_disconnects\":" + String(wifiDisconnectCount) + ",";
  j += "\"wifi_reconnects\":" + String(wifiReconnectCount) + ",";
  j += "\"wifi_last_reason\":" + String(wifiLastReason) + ",";

  j += "\"heap_free\":" + String(ESP.getFreeHeap()) + ",";
  j += "\"uptime_s\":" + String(millis() / 1000UL) + ",";

  j += "\"mode\":\"" + jsonEscape(hwMode) + "\",";
  j += "\"running\":" + String(hwRunning ? "true" : "false") + ",";
  j += "\"value_ms\":" + i64str(localTimerValue()) + ",";
  j += "\"programmed_ms\":" + i64str(hwProgrammedMs) + ",";
  j += "\"animation_loaded\":" + String(animLoaded ? "true" : "false") + ",";
  j += "\"animation_items\":" + String(animItemCount) + ",";
  j += "\"animation_bytes\":" + String(animBlobSize) + ",";

  j += "\"remote_state\":\"" + remoteState + "\",";
  j += "\"remote_battery\":" + String(remoteBattery) + ",";
  j += "\"remote_voltage\":" + String(remoteVoltage, 2) + ",";
  j += "\"remote_charging\":" + String(remoteCharging ? "true" : "false") + ",";
  j += "\"remote_rssi\":" + String(remoteRSSI);

  j += "}";

  return j;
}

String logsJson() {
  String j = "[";

  for (int k = 0; k < diagCount; k++) {
    int idx = (diagHead - diagCount + k + 48) % 48;

    if (k) j += ",";

    j += "\"" + jsonEscape(diagLog[idx]) + "\"";
  }

  j += "]";
  return j;
}

// ============================================================
// Web handlers
// ============================================================

void sendRoot() {
  addCors();
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(
    200,
    "text/html; charset=utf-8",
    (PGM_P)CARAC_WEB_GZ,
    CARAC_WEB_GZ_LEN
  );
}

void applyConfigArgs() {
  String previousMode = hwMode;
  if (server.hasArg("mode")) hwMode = server.arg("mode");
  if (hwMode == "standby" && previousMode != "standby") resetAnimationPlayback();

  if (server.hasArg("programmed_ms")) {
    hwProgrammedMs = parseI64(server.arg("programmed_ms"));
  }

  if (server.hasArg("overrun")) {
    hwOverrun = server.arg("overrun") == "1";
  }

  if (server.hasArg("continue_after_zero")) {
    hwContinueAfterZero = server.arg("continue_after_zero") == "1";
  }
  if (server.hasArg("brightness")) {
    hwBrightnessPct = (uint8_t)constrain(server.arg("brightness").toInt(), 0, 100);
  }

  if (server.hasArg("color")) {
    hwColor = parseRgb(server.arg("color"), hwColor);
  }

  if (server.hasArg("overrun_auto")) {
    hwOverrunAuto = server.arg("overrun_auto") == "1";
  }

  if (server.hasArg("overrun_color")) {
    hwOverrunColor = parseRgb(server.arg("overrun_color"), hwOverrunColor);
  }

  if (server.hasArg("digit_size")) hwDigitSize = server.arg("digit_size");
  if (server.hasArg("time_format")) hwTimeFormat = server.arg("time_format");

  if (server.hasArg("unit_colors")) {
    hwUnitColors = server.arg("unit_colors") == "1";
  }

  if (server.hasArg("color_hours")) {
    hwColorHours = parseRgb(server.arg("color_hours"), hwColorHours);
  }

  if (server.hasArg("color_minutes")) {
    hwColorMinutes = parseRgb(server.arg("color_minutes"), hwColorMinutes);
  }

  if (server.hasArg("color_seconds")) {
    hwColorSeconds = parseRgb(server.arg("color_seconds"), hwColorSeconds);
  }

  if (server.hasArg("color_fraction")) {
    hwColorFraction = parseRgb(server.arg("color_fraction"), hwColorFraction);
  }

  if (server.hasArg("color_separator")) {
    hwColorSeparator = parseRgb(server.arg("color_separator"), hwColorSeparator);
  }

  if (server.hasArg("color_sign")) {
    hwColorSign = parseRgb(server.arg("color_sign"), hwColorSign);
  }

  if (server.hasArg("deadline_epoch")) {
    hwDeadlineEpochMs = parseI64(server.arg("deadline_epoch"));
  }

  if (server.hasArg("clock_offset_ms")) {
    clockOffsetMs = parseI64(server.arg("clock_offset_ms"));
  }

  if (server.hasArg("clock_frozen")) {
    bool newFrozen = server.arg("clock_frozen") == "1";

    if (newFrozen && server.hasArg("clock_epoch_ms")) {
      clockFrozenEpochMs = parseI64(server.arg("clock_epoch_ms"));
    }

    clockFrozen = newFrozen;
  }

  if (server.hasArg("clock_seconds")) {
    clockShowSeconds = server.arg("clock_seconds") == "1";
  }

  if (server.hasArg("clock_12h")) {
    clock12h = server.arg("clock_12h") == "1";
  }

  if (server.hasArg("alert")) {
    bool newAlert = server.arg("alert") == "1";

    if (newAlert != alertActive) {
      alertActive = newAlert;
      alertStartedMs = millis();
    }
  }

  if (server.hasArg("remote_url")) {
    String newUrl = server.arg("remote_url");
    newUrl.trim();

    if (newUrl != remoteUrl) {
      remoteUrl = newUrl;
      remoteIPValid = false;
      markPrefsDirty();
    }
  }

  markPrefsDirty();
}

void handleSync() {
  addCors();

  applyConfigArgs();

  bool browserRunning = server.arg("running") == "1";
  int64_t browserValue = parseI64(server.arg("value_ms"));

  bool browserPrestart = server.arg("prestart_active") == "1";

  if (browserPrestart) {
    String target = server.arg("prestart_target");
    if (target.length() == 0) target = hwMode;

    long durationRaw = server.arg("prestart_duration_ms").toInt();
    long remainingRaw = server.arg("prestart_remaining_ms").toInt();

    uint32_t duration = (uint32_t)(durationRaw < 1 ? 1 : durationRaw);
    uint32_t remaining = (uint32_t)(remainingRaw < 0 ? 0 : remainingRaw);

    bool newPrestart =
      !prestartActive ||
      target != prestartTarget ||
      server.arg("prestart_style") != prestartStyle;

    if (newPrestart) {
      prestartActive = true;
      prestartTarget = target;
      prestartStyle = server.arg("prestart_style");
      if (prestartStyle.length() == 0) prestartStyle = "classic";

      prestartDurationMs = duration;
      prestartStartMs = millis();
      prestartEndMs = millis() + remaining;

      hwMode = target;
      hwBaseMs = browserValue;
      hwAnchorMs = millis();
      hwRunning = false;

      bumpState("WEB");
      addLog("WEB PRESTART " + prestartStyle);
    }
  } else {
    if (prestartActive) {
      prestartActive = false;
      addLog("PRESTART annule / termine");
    }

    // Snapshot only arrives on a user action or meaningful UI change.
    // It is therefore safe to resynchronise the master here.
    setSnapshotState(browserValue, browserRunning, "WEB");
  }

  matrix->setBrightness8(
    (uint8_t)map(hwBrightnessPct, 0, 100, 0, 255)
  );

  String response =
    "{\"ok\":true,\"state_seq\":" +
    String(stateSeq) +
    "}";

  server.send(200, "application/json", response);
}

void handleCommand() {
  addCors();

  String action = server.arg("action");

  if (server.hasArg("target")) {
    String target = server.arg("target");
    if (target.length()) hwMode = target;
  }

  prestartActive = false;

  if (action == "PLAY") {
    playLocal("WEB");
  } else if (action == "PAUSE") {
    pauseLocal("WEB");
  } else if (action == "RESET") {
    resetLocal("WEB");
  }

  server.send(
    200,
    "application/json",
    "{\"ok\":true,\"state_seq\":" + String(stateSeq) + "}"
  );
}



void handleModeChange() {
  addCors();

  String newMode = server.arg("mode");
  if (!(newMode == "clock" || newMode == "stopwatch" || newMode == "countdown" ||
        newMode == "deadline" || newMode == "standby" || newMode == "off")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid mode\"}");
    return;
  }

  // Freeze current master state before switching mode.
  if (hwRunning && (hwMode == "stopwatch" || hwMode == "countdown" || hwMode == "deadline")) {
    hwBaseMs = localTimerValue();
  }

  hwMode = newMode;
  prestartActive = false;

  if (server.hasArg("programmed_ms")) {
    hwProgrammedMs = parseI64(server.arg("programmed_ms"));
  }

  int64_t value = 0;
  if (server.hasArg("value_ms")) value = parseI64(server.arg("value_ms"));
  else if (newMode == "countdown") value = hwProgrammedMs;
  else if (newMode == "deadline" && hwDeadlineEpochMs > 0) value = hwDeadlineEpochMs - systemEpochMs();

  hwBaseMs = value;
  hwAnchorMs = millis();
  hwRunning = server.hasArg("running") && server.arg("running") == "1";

  if (newMode == "clock" || newMode == "off" || newMode == "standby") {
    hwRunning = false;
  }

  resetAnimationPlayback();
  lastRenderKey = "";
  bumpState("WEB_MODE");
  markPrefsDirty();

  addLog("WEB MODE -> " + newMode);

  server.send(
    200,
    "application/json",
    "{\"ok\":true,\"mode\":\"" + newMode + "\",\"state_seq\":" + String(stateSeq) + "}"
  );
}

bool allocateAnimationUpload() {
  if (animUploadBuffer) {
    free(animUploadBuffer);
    animUploadBuffer = nullptr;
  }

  animUploadBuffer = (uint8_t*)heap_caps_malloc(
    ANIM_UPLOAD_CAPACITY,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
  );

  if (!animUploadBuffer) {
    animUploadBuffer = (uint8_t*)malloc(ANIM_UPLOAD_CAPACITY);
  }

  animUploadBytes = 0;
  animUploadFailed = animUploadBuffer == nullptr;
  animUploadError = animUploadFailed ? "memoire insuffisante" : "";
  return !animUploadFailed;
}

void animationUploadChunk() {
  HTTPUpload& u = server.upload();

  if (u.status == UPLOAD_FILE_START) {
    allocateAnimationUpload();
    addLog("Animation : reception...");
  }

  else if (u.status == UPLOAD_FILE_WRITE) {
    if (animUploadFailed || !animUploadBuffer) return;

    if (animUploadBytes + u.currentSize > ANIM_UPLOAD_CAPACITY) {
      animUploadFailed = true;
      animUploadError = "animation trop volumineuse";
      return;
    }

    memcpy(animUploadBuffer + animUploadBytes, u.buf, u.currentSize);
    animUploadBytes += u.currentSize;
  }

  else if (u.status == UPLOAD_FILE_END) {
    if (animUploadFailed || !animUploadBuffer) return;

    String error;
    uint8_t* completed = animUploadBuffer;

    if (installAnimation(completed, animUploadBytes, error)) {
      animUploadBuffer = nullptr; // ownership moved to animBlob
      addLog(
        "Animation chargee : " +
        String(animItemCount) +
        " element(s), " +
        String(animBlobSize) +
        " octets"
      );
    } else {
      animUploadFailed = true;
      animUploadError = error;
    }
  }

  else if (u.status == UPLOAD_FILE_ABORTED) {
    animUploadFailed = true;
    animUploadError = "transfert annule";
  }
}

void handleAnimationUploadDone() {
  addCors();

  if (animUploadFailed || !animLoaded) {
    if (animUploadBuffer) {
      free(animUploadBuffer);
      animUploadBuffer = nullptr;
    }

    String error = animUploadError.length() ? animUploadError : "animation invalide";
    server.send(
      400,
      "application/json",
      "{\"ok\":false,\"error\":\"" + jsonEscape(error) + "\"}"
    );
    return;
  }

  server.send(
    200,
    "application/json",
    "{\"ok\":true,\"items\":" +
    String(animItemCount) +
    ",\"bytes\":" +
    String(animBlobSize) +
    ",\"speed\":" +
    String(animSpeedPct) +
    "}"
  );
}

void handleAnimationSettings() {
  addCors();

  if (server.hasArg("speed")) {
    animSpeedPct = (uint8_t)constrain(server.arg("speed").toInt(), 0, 100);
  }

  if (server.hasArg("direction")) {
    animDirection = server.arg("direction") == "1" ? 1 : 0;
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

void runBootTest() {
  otaActive = true;
  startupAnimation();
  otaActive = false;
  addLog("Test animation de demarrage");
}

void setupWeb() {
  server.on("/", HTTP_GET, sendRoot);
  server.on("/index.html", HTTP_GET, sendRoot);

  server.on("/api/status", HTTP_GET, []() {
    addCors();
    server.send(200, "application/json", statusJson());
  });

  server.on("/status", HTTP_GET, []() {
    addCors();
    server.send(200, "application/json", statusJson());
  });

  server.on("/api/logs", HTTP_GET, []() {
    addCors();
    server.send(200, "application/json", logsJson());
  });

  server.on("/api/sync", HTTP_POST, handleSync);
  server.on("/api/command", HTTP_POST, handleCommand);
  server.on("/api/mode", HTTP_POST, handleModeChange);
  server.on("/api/animation", HTTP_POST, handleAnimationUploadDone, animationUploadChunk);
  server.on("/api/animation/settings", HTTP_POST, handleAnimationSettings);

  server.on("/api/boot-animation", HTTP_POST, []() {
    addCors();
    server.send(200, "application/json", "{\"ok\":true}");
    delay(20);
    runBootTest();
  });

  server.on("/api/system/restart", HTTP_POST, []() {
    addCors();
    server.send(200, "application/json", "{\"ok\":true}");
    delay(400);
    ESP.restart();
  });

  server.on("/api/diagnostics/run", HTTP_POST, []() {
    addCors();

    String j = "{";
    j += "\"ok\":true,";
    j += "\"wifi_disconnects\":" + String(wifiDisconnectCount) + ",";
    j += "\"wifi_reconnects\":" + String(wifiReconnectCount) + ",";
    j += "\"wifi_last_reason\":" + String(wifiLastReason) + ",";
    j += "\"heap_free\":" + String(ESP.getFreeHeap()) + ",";
    j += "\"checks\":[\"esp32-s3\",\"hub75-64x32\",\"wifi-master\",\"ntp\",\"web\",\"ota\",\"remote-api\"]";
    j += "}";

    addLog("Auto-diagnostic execute");
    server.send(200, "application/json", j);
  });

  // OTA is kept for the NEXT update, even though this V1.9.0 is installed by USB.
  server.on("/update", HTTP_GET, []() {
    addCors();

    server.send(
      200,
      "text/html; charset=utf-8",
      "<html><body style='background:#0c0f12;color:#edf0f3;font-family:Segoe UI,Arial;padding:30px'>"
      "<h2>CARAC TIMER · OTA</h2>"
      "<form method='POST' action='/api/ota' enctype='multipart/form-data'>"
      "<input type='file' name='firmware' accept='.bin' required>"
      "<button type='submit'>Mettre a jour</button>"
      "</form></body></html>"
    );
  });

  server.on(
    "/api/ota",
    HTTP_POST,
    []() {
      addCors();

      bool ok = !Update.hasError();

      server.send(
        200,
        "application/json",
        ok ?
          "{\"ok\":true,\"restart\":true}" :
          "{\"ok\":false}"
      );

      delay(1800);

      if (ok) ESP.restart();
    },
    []() {
      HTTPUpload& u = server.upload();

      if (u.status == UPLOAD_FILE_START) {
        otaActive = true;
        showPanelMessage("UPDATE", C_YELLOW, 1, 10);

        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          Update.printError(Serial);
        }
      }

      else if (u.status == UPLOAD_FILE_WRITE) {
        if (Update.write(u.buf, u.currentSize) != u.currentSize) {
          Update.printError(Serial);
        }
      }

      else if (u.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
          showPanelMessage("OTA OK", C_GREEN, 1, 10);
        } else {
          Update.printError(Serial);
          showPanelMessage("OTA ERR", C_RED, 1, 10);
          otaActive = false;
        }
      }
    }
  );

  server.onNotFound([]() {
    addCors();
    server.send(404, "text/plain", "404");
  });

  server.begin();
  addLog("Serveur web actif");
}

// ============================================================
// Arduino OTA
// ============================================================

void setupArduinoOTA() {
  ArduinoOTA.setHostname(HOSTNAME);

  ArduinoOTA.onStart([]() {
    otaActive = true;
    showPanelMessage("UPDATE", C_YELLOW, 1, 10);
    addLog("Arduino OTA START");
  });

  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    int pct = t ? (p * 100U / t) : 0;

    showPanelMessage(String(pct) + "%", C_WHITE, 1, 10);
  });

  ArduinoOTA.onEnd([]() {
    showPanelMessage("OTA OK", C_GREEN, 1, 10);
  });

  ArduinoOTA.onError([](ota_error_t) {
    showPanelMessage("OTA ERR", C_RED, 1, 10);
    otaActive = false;
  });

  ArduinoOTA.begin();
  addLog("ArduinoOTA actif");
}

// ============================================================
// Setup / loop
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(300);

  loadPrefs();
  setupMatrix();
  startupAnimation();

  bool wifiOK = connectWifiInitial();

  if (!wifiOK) {
    wifiLostAt = millis();
  } else {
    setupTimeSync();
    startMdns();
  }

  setupArduinoOTA();
  setupWeb();

  addLog("CARAC TIMER V" + String(FW_VERSION) + " PRET");
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();

  maintainWifi();

  if (!otaActive) {
    // Remote HTTP polling and HUB75 rendering are intentionally paused during OTA.
    // This keeps the upload socket responsive and avoids large concurrent workloads.
    pollRemote();
    drawDisplay();
    servicePrefs();
  }

  delay(1);
}