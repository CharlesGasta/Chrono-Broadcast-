#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <LittleFS.h>
extern "C" {
#include "user_interface.h"
}

#define FIRMWARE_VERSION "1.10.0"
// Public build: battery logger + charge detection + sleep diagnostics

// ======================================================
// CHRONO BROADCAST / CARAC TIMER REMOTE — ESP8266
// V1.10.0
//
// - Short press: PLAY / PAUSE
// - Long press 1.5 s: RESET
// - LiPo battery monitoring on A0
// - DHCP or static IP
// - Configurable IP / gateway / subnet / DNS1 / DNS2 / hostname
// - Persistent Wi-Fi configuration in EEPROM
// - Automatic rescue AP + captive portal
// - Web diagnostics and settings
// - Browser OTA + ArduinoOTA
// - /status API compatible with the CARAC TIMER control UI
// - Configurable automatic light-sleep, wake on PLAY/PAUSE button
// - Persistent battery discharge logger (CSV in LittleFS)
// - Automatic charging-state detection; percentage hidden while charging
// ======================================================

#define BUTTON_PIN D5
#define LED_PIN LED_BUILTIN
#define BATTERY_PIN A0

const unsigned long LONG_PRESS_MS = 1500;
const unsigned long DEBOUNCE_MS = 30;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long WIFI_RETRY_INTERVAL_MS = 30000;
const unsigned long BATTERY_SAMPLE_INTERVAL_MS = 2000;
const uint8_t BATTERY_SAMPLE_COUNT = 24;
const unsigned long BATTERY_LOG_INTERVAL_MS = 60000UL;
const float CHARGE_START_RISE_V = 0.025f;
const uint8_t CHARGE_START_CONFIRM_SAMPLES = 2;
const float CHARGE_STOP_DROP_V = 0.010f;
const char* BATTERY_LOG_PATH = "/battery-log.csv";

// Battery divider:
// Battery + after switch -> 100k -> A0 -> 100k -> GND
const float R_TOP_OHM = 100000.0f;
const float R_BOTTOM_OHM = 100000.0f;
const float DIVIDER_RATIO = (R_TOP_OHM + R_BOTTOM_OHM) / R_BOTTOM_OHM;
const float A0_FULL_SCALE_V = 3.30f;

// Calibrated from 3.68 V reported vs 4.10 V multimeter.
const float BATTERY_CALIBRATION = 1.114f;

const uint16_t EEPROM_SIZE = 512;
const uint32_t CONFIG_MAGIC = 0x43425232; // CBR2
const uint16_t CONFIG_SCHEMA = 3;
const uint32_t LEGACY_WIFI_MAGIC = 0x43415243; // V1.7.x "CARC"

struct LegacyWifiConfig {
  uint32_t magic;
  char ssid[33];
  char password[65];
};

struct NetworkConfigV2 {
  uint32_t magic;
  uint16_t schema;

  char ssid[33];
  char password[65];
  char hostname[33];

  uint8_t dhcp;
  uint8_t mdnsEnabled;

  char staticIp[16];
  char gateway[16];
  char subnet[16];
  char dns1[16];
  char dns2[16];

  char apSsid[33];
  char apPassword[65];
};

struct NetworkConfig {
  uint32_t magic;
  uint16_t schema;

  char ssid[33];
  char password[65];
  char hostname[33];

  uint8_t dhcp;
  uint8_t mdnsEnabled;

  char staticIp[16];
  char gateway[16];
  char subnet[16];
  char dns1[16];
  char dns2[16];

  char apSsid[33];
  char apPassword[65];

  uint32_t sleepTimeoutSeconds; // 0 = never
};

NetworkConfig config;

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
DNSServer dnsServer;

bool apMode = false;
bool mdnsStarted = false;
unsigned long lastWifiRetry = 0;
unsigned long lastActivityAt = 0;
volatile bool sleepWakeTriggered = false;

bool timerRunning = false;
bool lastRawButton = HIGH;
bool stableButton = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long pressStartedAt = 0;
bool longPressTriggered = false;

String lastAction = "NONE";
unsigned long commandCounter = 0;

float batteryVoltage = 0.0f;
uint8_t batteryPercent = 0;
unsigned long lastBatterySample = 0;
uint16_t batteryAdcRaw = 0;

bool batteryCharging = false;
float chargeBaselineVoltage = 0.0f;
float chargePeakVoltage = 0.0f;
uint8_t chargeRiseConfirmCount = 0;

bool batteryLoggerActive = false;
unsigned long batteryLoggerStartedAt = 0;
unsigned long lastBatteryLogAt = 0;
uint32_t batteryLogSamples = 0;

struct SocPoint {
  uint8_t percent;
  float voltage;
};

const SocPoint SOC_TABLE[] = {
  {  0, 3.30f}, {  5, 3.40f}, { 10, 3.50f}, { 15, 3.60f},
  { 20, 3.65f}, { 30, 3.70f}, { 40, 3.75f}, { 50, 3.80f},
  { 60, 3.85f}, { 70, 3.90f}, { 75, 3.95f}, { 80, 4.00f},
  { 85, 4.05f}, { 90, 4.10f}, { 95, 4.15f}, {100, 4.20f}
};

const size_t SOC_TABLE_COUNT = sizeof(SOC_TABLE) / sizeof(SOC_TABLE[0]);

void copyString(char* dst, size_t dstSize, const String& value) {
  if (!dst || dstSize == 0) return;
  memset(dst, 0, dstSize);
  value.substring(0, dstSize - 1).toCharArray(dst, dstSize);
}

void setDefaults() {
  memset(&config, 0, sizeof(config));
  config.magic = CONFIG_MAGIC;
  config.schema = CONFIG_SCHEMA;

  copyString(config.hostname, sizeof(config.hostname), "carac-remote");
  config.dhcp = 1;
  config.mdnsEnabled = 1;

  copyString(config.staticIp, sizeof(config.staticIp), "192.168.1.90");
  copyString(config.gateway, sizeof(config.gateway), "192.168.1.1");
  copyString(config.subnet, sizeof(config.subnet), "255.255.255.0");
  copyString(config.dns1, sizeof(config.dns1), "1.1.1.1");
  copyString(config.dns2, sizeof(config.dns2), "8.8.8.8");

  copyString(config.apSsid, sizeof(config.apSsid), "CARAC-REMOTE-SETUP");
  copyString(config.apPassword, sizeof(config.apPassword), "caracremote");
  config.sleepTimeoutSeconds = 0;
}

void saveConfig() {
  config.magic = CONFIG_MAGIC;
  config.schema = CONFIG_SCHEMA;
  EEPROM.put(0, config);
  EEPROM.commit();
}

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, config);

  if (config.magic == CONFIG_MAGIC && config.schema == CONFIG_SCHEMA) {
    return;
  }

  NetworkConfigV2 oldV2;
  EEPROM.get(0, oldV2);

  if (oldV2.magic == CONFIG_MAGIC && oldV2.schema == 2) {
    setDefaults();

    copyString(config.ssid, sizeof(config.ssid), String(oldV2.ssid));
    copyString(config.password, sizeof(config.password), String(oldV2.password));
    copyString(config.hostname, sizeof(config.hostname), String(oldV2.hostname));

    config.dhcp = oldV2.dhcp;
    config.mdnsEnabled = oldV2.mdnsEnabled;

    copyString(config.staticIp, sizeof(config.staticIp), String(oldV2.staticIp));
    copyString(config.gateway, sizeof(config.gateway), String(oldV2.gateway));
    copyString(config.subnet, sizeof(config.subnet), String(oldV2.subnet));
    copyString(config.dns1, sizeof(config.dns1), String(oldV2.dns1));
    copyString(config.dns2, sizeof(config.dns2), String(oldV2.dns2));

    copyString(config.apSsid, sizeof(config.apSsid), String(oldV2.apSsid));
    copyString(config.apPassword, sizeof(config.apPassword), String(oldV2.apPassword));

    config.sleepTimeoutSeconds = 0;
    saveConfig();
    return;
  }

  LegacyWifiConfig legacy;
  EEPROM.get(0, legacy);

  setDefaults();

  if (legacy.magic == LEGACY_WIFI_MAGIC && legacy.ssid[0] != '\0') {
    copyString(config.ssid, sizeof(config.ssid), String(legacy.ssid));
    copyString(config.password, sizeof(config.password), String(legacy.password));
  }

  saveConfig();
}

void clearWifiOnly() {
  memset(config.ssid, 0, sizeof(config.ssid));
  memset(config.password, 0, sizeof(config.password));
  config.dhcp = 1;
  saveConfig();
}

void ledOn() { digitalWrite(LED_PIN, LOW); }
void ledOff() { digitalWrite(LED_PIN, HIGH); }

void flashLED(int count, int onTime = 80, int offTime = 80) {
  for (int i = 0; i < count; i++) {
    ledOn();
    delay(onTime);
    ledOff();
    delay(offTime);
  }
}

uint8_t voltageToPercent(float v) {
  if (v <= SOC_TABLE[0].voltage) return 0;
  if (v >= SOC_TABLE[SOC_TABLE_COUNT - 1].voltage) return 100;

  for (size_t i = 1; i < SOC_TABLE_COUNT; i++) {
    if (v <= SOC_TABLE[i].voltage) {
      const float v0 = SOC_TABLE[i - 1].voltage;
      const float v1 = SOC_TABLE[i].voltage;
      const float p0 = SOC_TABLE[i - 1].percent;
      const float p1 = SOC_TABLE[i].percent;
      const float t = (v - v0) / (v1 - v0);
      return (uint8_t)constrain((int)lroundf(p0 + t * (p1 - p0)), 0, 100);
    }
  }

  return 0;
}

float readBatteryVoltageNow() {
  uint32_t total = 0;

  for (uint8_t i = 0; i < BATTERY_SAMPLE_COUNT; i++) {
    total += analogRead(BATTERY_PIN);
    delayMicroseconds(350);
  }

  const float raw = (float)total / BATTERY_SAMPLE_COUNT;
  batteryAdcRaw = (uint16_t)lroundf(raw);
  const float a0Voltage = (raw / 1023.0f) * A0_FULL_SCALE_V;
  return a0Voltage * DIVIDER_RATIO * BATTERY_CALIBRATION;
}

void updateChargeDetection() {
  if (batteryVoltage <= 0.1f) return;

  if (chargeBaselineVoltage <= 0.1f) {
    chargeBaselineVoltage = batteryVoltage;
    chargePeakVoltage = batteryVoltage;
    return;
  }

  if (!batteryCharging) {
    if (batteryVoltage < chargeBaselineVoltage) {
      chargeBaselineVoltage = batteryVoltage;
    }

    if (batteryVoltage - chargeBaselineVoltage >= CHARGE_START_RISE_V) {
      if (chargeRiseConfirmCount < 255) chargeRiseConfirmCount++;

      if (chargeRiseConfirmCount >= CHARGE_START_CONFIRM_SAMPLES) {
        batteryCharging = true;
        chargePeakVoltage = batteryVoltage;
        chargeRiseConfirmCount = 0;
        Serial.println("[BATTERY] EN CHARGE");
      }
    } else {
      chargeRiseConfirmCount = 0;
    }

    return;
  }

  if (batteryVoltage > chargePeakVoltage) {
    chargePeakVoltage = batteryVoltage;
  }

  if (chargePeakVoltage - batteryVoltage >= CHARGE_STOP_DROP_V) {
    batteryCharging = false;
    chargeBaselineVoltage = batteryVoltage;
    chargePeakVoltage = batteryVoltage;
    chargeRiseConfirmCount = 0;
    Serial.println("[BATTERY] FIN DE CHARGE");
  }
}

void updateBattery(bool force = false) {
  const unsigned long now = millis();

  if (!force && now - lastBatterySample < BATTERY_SAMPLE_INTERVAL_MS) {
    return;
  }

  lastBatterySample = now;
  const float measured = readBatteryVoltageNow();

  if (batteryVoltage <= 0.1f) {
    batteryVoltage = measured;
  } else {
    batteryVoltage = batteryVoltage * 0.72f + measured * 0.28f;
  }

  batteryPercent = voltageToPercent(batteryVoltage);
  updateChargeDetection();
}

void ensureBatteryLogFile() {
  if (LittleFS.exists(BATTERY_LOG_PATH)) return;

  File f = LittleFS.open(BATTERY_LOG_PATH, "w");
  if (!f) return;

  f.println("uptime_s,elapsed_s,adc_raw,voltage_v,percent,charging");
  f.close();
}

void appendBatteryLog() {
  ensureBatteryLogFile();

  File f = LittleFS.open(BATTERY_LOG_PATH, "a");
  if (!f) {
    Serial.println("[BATTERY LOG] impossible d'ouvrir le fichier");
    return;
  }

  const unsigned long now = millis();
  const unsigned long elapsedS = batteryLoggerActive
    ? (now - batteryLoggerStartedAt) / 1000UL
    : 0;

  f.print(now / 1000UL);
  f.print(',');
  f.print(elapsedS);
  f.print(',');
  f.print(batteryAdcRaw);
  f.print(',');
  f.print(batteryVoltage, 3);
  f.print(',');
  f.print(batteryPercent);
  f.print(',');
  f.println(batteryCharging ? 1 : 0);
  f.close();

  batteryLogSamples++;
  lastBatteryLogAt = now;
}

void updateBatteryLogger() {
  if (!batteryLoggerActive) return;

  const unsigned long now = millis();

  if (
    batteryLogSamples == 0 ||
    lastBatteryLogAt == 0 ||
    now - lastBatteryLogAt >= BATTERY_LOG_INTERVAL_MS
  ) {
    appendBatteryLog();
  }
}

void startBatteryLogger() {
  ensureBatteryLogFile();
  batteryLoggerActive = true;
  batteryLoggerStartedAt = millis();
  lastBatteryLogAt = 0;
  batteryLogSamples = 0;
  appendBatteryLog();

  Serial.println("[BATTERY LOG] demarre");
}

void stopBatteryLogger() {
  batteryLoggerActive = false;
  Serial.println("[BATTERY LOG] arrete");
}

void commandToggle() {
  timerRunning = !timerRunning;
  commandCounter++;
  lastAction = timerRunning ? "PLAY" : "PAUSE";

  Serial.print(">>> ");
  Serial.println(lastAction);

  flashLED(1, 80, 30);
}

void commandReset() {
  timerRunning = false;
  commandCounter++;
  lastAction = "RESET";

  Serial.println(">>> RESET");
  flashLED(3, 60, 50);
}

void handleButton() {
  const bool rawButton = digitalRead(BUTTON_PIN);

  if (rawButton != lastRawButton) {
    lastDebounceTime = millis();
    lastRawButton = rawButton;
  }

  if (millis() - lastDebounceTime < DEBOUNCE_MS) return;

  if (rawButton != stableButton) {
    stableButton = rawButton;

    if (stableButton == LOW) {
      pressStartedAt = millis();
      longPressTriggered = false;
      Serial.println("[BUTTON] DOWN");
    } else {
      Serial.println("[BUTTON] UP");

      if (!longPressTriggered) {
        if (millis() - pressStartedAt < LONG_PRESS_MS) {
          commandToggle();
        }
      }
    }
  }

  if (
    stableButton == LOW &&
    !longPressTriggered &&
    millis() - pressStartedAt >= LONG_PRESS_MS
  ) {
    longPressTriggered = true;
    commandReset();
  }
}

bool parseIp(const String& value, IPAddress& out) {
  if (!out.fromString(value)) return false;
  return true;
}

String ipToString(const IPAddress& ip) {
  return ip.toString();
}

bool applyNetworkConfig() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  String hostname = String(config.hostname);
  hostname.trim();

  if (hostname.length() == 0) hostname = "carac-remote";
  WiFi.hostname(hostname);

  if (config.dhcp) {
    WiFi.config(
      IPAddress(0,0,0,0),
      IPAddress(0,0,0,0),
      IPAddress(0,0,0,0)
    );
    return true;
  }

  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns1;
  IPAddress dns2;

  if (!parseIp(String(config.staticIp), ip)) return false;
  if (!parseIp(String(config.gateway), gateway)) return false;
  if (!parseIp(String(config.subnet), subnet)) return false;
  if (!parseIp(String(config.dns1), dns1)) return false;
  if (!parseIp(String(config.dns2), dns2)) return false;

  return WiFi.config(ip, gateway, subnet, dns1, dns2);
}

void stopMdns() {
  if (mdnsStarted) {
    MDNS.close();
    mdnsStarted = false;
  }
}

void startMdns() {
  if (
    WiFi.status() == WL_CONNECTED &&
    config.mdnsEnabled &&
    !mdnsStarted
  ) {
    String hostname = String(config.hostname);
    hostname.trim();
    if (hostname.length() == 0) hostname = "carac-remote";

    if (MDNS.begin(hostname.c_str())) {
      mdnsStarted = true;
      Serial.print("mDNS : http://");
      Serial.print(hostname);
      Serial.println(".local");
    }
  }
}

bool connectToConfiguredWifi(unsigned long timeoutMs) {
  if (config.ssid[0] == '\0') return false;

  stopMdns();

  if (!applyNetworkConfig()) {
    Serial.println("Configuration IP statique invalide");
    return false;
  }

  Serial.print("Connexion Wi-Fi a ");
  Serial.println(config.ssid);

  WiFi.begin(config.ssid, config.password);

  const unsigned long started = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - started < timeoutMs
  ) {
    handleButton();
    updateBattery();

    ledOn();
    delay(35);
    ledOff();
    delay(215);

    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    apMode = false;

    Serial.println("Wi-Fi OK");
    Serial.print("SSID : ");
    Serial.println(WiFi.SSID());
    Serial.print("IP : ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway : ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("Subnet : ");
    Serial.println(WiFi.subnetMask());
    Serial.print("DNS 1 : ");
    Serial.println(WiFi.dnsIP(0));
    Serial.print("DNS 2 : ");
    Serial.println(WiFi.dnsIP(1));
    Serial.print("RSSI : ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    startMdns();
    return true;
  }

  Serial.println("Echec Wi-Fi");
  return false;
}

void startFallbackAP() {
  stopMdns();

  WiFi.mode(WIFI_AP_STA);

  String apSsid = String(config.apSsid);
  String apPassword = String(config.apPassword);

  if (apSsid.length() == 0) apSsid = "CARAC-REMOTE-SETUP";
  if (apPassword.length() < 8) apPassword = "caracremote";

  const bool ok = WiFi.softAP(apSsid.c_str(), apPassword.c_str());
  apMode = true;

  const IPAddress apIP = WiFi.softAPIP();

  Serial.println();
  Serial.println("==============================");
  Serial.println(" MODE SECOURS WIFI ACTIF");
  Serial.println("==============================");
  Serial.print("SSID : ");
  Serial.println(apSsid);
  Serial.print("Mot de passe : ");
  Serial.println(apPassword);
  Serial.print("Adresse : http://");
  Serial.println(apIP);
  Serial.println("==============================");

  if (ok) {
    dnsServer.start(53, "*", apIP);
  }

  flashLED(5, 70, 70);
}

void stopFallbackAP() {
  if (!apMode) return;

  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  apMode = false;

  Serial.println("Mode secours Wi-Fi arrete");
}

void maintainWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (apMode) {
      stopFallbackAP();
    }

    startMdns();
    return;
  }

  stopMdns();

  if (!apMode) {
    startFallbackAP();
  }

  const unsigned long now = millis();

  if (
    config.ssid[0] != '\0' &&
    now - lastWifiRetry >= WIFI_RETRY_INTERVAL_MS
  ) {
    lastWifiRetry = now;

    Serial.println("Nouvelle tentative Wi-Fi...");
    applyNetworkConfig();
    WiFi.begin(config.ssid, config.password);
  }
}


void markActivity() {
  lastActivityAt = millis();
}

void lightSleepWakeCallback() {
  sleepWakeTriggered = true;
}

bool autoSleepDue() {
  if (config.sleepTimeoutSeconds == 0) return false;
  if (batteryLoggerActive) return false;
  if (apMode) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  const unsigned long timeoutMs = config.sleepTimeoutSeconds * 1000UL;
  return millis() - lastActivityAt >= timeoutMs;
}

void enterAutomaticLightSleep() {
  Serial.println();
  Serial.println("==================================");
  Serial.println(" VEILLE AUTOMATIQUE");
  Serial.println(" Appuyer sur PLAY/PAUSE pour reveiller");
  Serial.println("==================================");
  Serial.flush();

  stopMdns();
  server.stop();

  WiFi.mode(WIFI_OFF);
  delay(10);

  sleepWakeTriggered = false;

  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);
  gpio_pin_wakeup_enable(GPIO_ID_PIN(14), GPIO_PIN_INTR_LOLEVEL);
  wifi_fpm_set_wakeup_cb(lightSleepWakeCallback);
  wifi_fpm_open();

  ledOff();

  wifi_fpm_do_sleep(0xFFFFFFF);
  delay(10);

  wifi_fpm_close();
  gpio_pin_wakeup_disable();

  Serial.println("Reveil par bouton");

  // The wake-up press is itself the PLAY / PAUSE command.
  commandToggle();

  // Consume this press so it is not processed twice by handleButton().
  while (digitalRead(BUTTON_PIN) == LOW) {
    delay(5);
  }

  lastRawButton = HIGH;
  stableButton = HIGH;
  longPressTriggered = false;
  lastDebounceTime = millis();

  applyNetworkConfig();
  WiFi.begin(config.ssid, config.password);

  const unsigned long started = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - started < WIFI_CONNECT_TIMEOUT_MS
  ) {
    delay(50);
  }

  if (WiFi.status() == WL_CONNECTED) {
    startMdns();
    Serial.print("Wi-Fi reconnecte : ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi non reconnecte, mode secours...");
    startFallbackAP();
  }

  server.begin();
  markActivity();
}

void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader(
    "Cache-Control",
    "no-store, no-cache, must-revalidate, max-age=0"
  );
}

String jsonEscape(const String& input) {
  String out;
  out.reserve(input.length() + 8);

  for (size_t i = 0; i < input.length(); i++) {
    const char c = input[i];

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

void handleStatus() {
  updateBattery();
  addCorsHeaders();

  const bool staConnected = WiFi.status() == WL_CONNECTED;

  String ip = staConnected
    ? WiFi.localIP().toString()
    : WiFi.softAPIP().toString();

  String json = "{";

  json += "\"running\":";
  json += timerRunning ? "true" : "false";

  json += ",\"last_action\":\"";
  json += jsonEscape(lastAction);
  json += "\"";

  json += ",\"counter\":";
  json += String(commandCounter);

  json += ",\"wifi_connected\":";
  json += staConnected ? "true" : "false";

  json += ",\"ap_mode\":";
  json += apMode ? "true" : "false";

  json += ",\"network_mode\":\"";
  json += config.dhcp ? "DHCP" : "STATIC";
  json += "\"";

  json += ",\"mdns_enabled\":";
  json += config.mdnsEnabled ? "true" : "false";

  json += ",\"configured_static_ip\":\"";
  json += jsonEscape(String(config.staticIp));
  json += "\"";

  json += ",\"configured_gateway\":\"";
  json += jsonEscape(String(config.gateway));
  json += "\"";

  json += ",\"configured_subnet\":\"";
  json += jsonEscape(String(config.subnet));
  json += "\"";

  json += ",\"configured_dns1\":\"";
  json += jsonEscape(String(config.dns1));
  json += "\"";

  json += ",\"configured_dns2\":\"";
  json += jsonEscape(String(config.dns2));
  json += "\"";

  json += ",\"ap_ssid\":\"";
  json += jsonEscape(String(config.apSsid));
  json += "\"";

  json += ",\"ssid\":\"";
  json += jsonEscape(staConnected ? WiFi.SSID() : String(""));
  json += "\"";

  json += ",\"configured_ssid\":\"";
  json += jsonEscape(String(config.ssid));
  json += "\"";

  json += ",\"hostname\":\"";
  json += jsonEscape(String(config.hostname));
  json += "\"";

  json += ",\"rssi\":";
  json += staConnected ? String(WiFi.RSSI()) : String(0);

  json += ",\"channel\":";
  json += staConnected ? String(WiFi.channel()) : String(0);

  json += ",\"bssid\":\"";
  json += staConnected ? WiFi.BSSIDstr() : String("");
  json += "\"";

  json += ",\"mac\":\"";
  json += WiFi.macAddress();
  json += "\"";

  json += ",\"ip\":\"";
  json += ip;
  json += "\"";

  json += ",\"gateway\":\"";
  json += staConnected ? WiFi.gatewayIP().toString() : String("");
  json += "\"";

  json += ",\"subnet\":\"";
  json += staConnected ? WiFi.subnetMask().toString() : String("");
  json += "\"";

  json += ",\"dns1\":\"";
  json += staConnected ? WiFi.dnsIP(0).toString() : String("");
  json += "\"";

  json += ",\"dns2\":\"";
  json += staConnected ? WiFi.dnsIP(1).toString() : String("");
  json += "\"";

  json += ",\"battery_voltage\":";
  json += String(batteryVoltage, 3);

  json += ",\"battery_percent\":";
  json += String(batteryPercent);

  json += ",\"battery_adc_raw\":";
  json += String(batteryAdcRaw);

  json += ",\"battery_charging\":";
  json += batteryCharging ? "true" : "false";

  json += ",\"battery_log_active\":";
  json += batteryLoggerActive ? "true" : "false";

  json += ",\"battery_log_samples\":";
  json += String(batteryLogSamples);

  json += ",\"battery_log_elapsed_s\":";
  json += batteryLoggerActive
    ? String((millis() - batteryLoggerStartedAt) / 1000UL)
    : String(0);

  json += ",\"sleep_timeout_s\":";
  json += String(config.sleepTimeoutSeconds);

  json += ",\"sleep_enabled\":";
  json += config.sleepTimeoutSeconds > 0 ? "true" : "false";

  json += ",\"firmware\":\"";
  json += FIRMWARE_VERSION;
  json += "\"";

  json += ",\"uptime_s\":";
  json += String(millis() / 1000UL);

  json += "}";

  server.send(200, "application/json", json);
}

const char PAGE_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Chrono Broadcast Remote</title>
<style>
:root{color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;background:#0b0c0f;color:#fff;font:14px Arial,Helvetica,sans-serif}
.wrap{max-width:1050px;margin:auto;padding:22px}
header{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:18px}
h1{font-size:25px;margin:0}
h2{font-size:18px;margin:0 0 14px}
.muted{color:#8e929d}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px}
.card{background:#15171c;border:1px solid #292c34;border-radius:14px;padding:18px}
.big{font-size:30px;font-weight:800;margin:7px 0}
.row{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:9px 0}
.badge{padding:6px 10px;border-radius:999px;background:#472025;color:#ff8b8b;font-weight:800}
.badge.ok{background:#153e29;color:#64ec9c}
.battery{width:54px;height:25px;border:2px solid #d8dbe2;border-radius:6px;padding:3px;position:relative}
.battery:after{content:"";position:absolute;width:4px;height:11px;background:#d8dbe2;right:-6px;top:5px;border-radius:0 3px 3px 0}
.batteryFill{height:100%;width:0;border-radius:2px;background:#39d879;transition:.25s}
button,input,select{font:inherit}
button{border:0;border-radius:9px;background:#313640;color:#fff;padding:11px 14px;font-weight:700;cursor:pointer}
button.primary{background:#177647}
button.danger{background:#8b292d}
button.warn{background:#835d18}
input,select{width:100%;border:1px solid #353a45;border-radius:8px;background:#0d0f13;color:#fff;padding:11px}
label{display:block;color:#aeb2bb;margin:12px 0 6px}
.actions{display:flex;gap:8px;flex-wrap:wrap}
.two{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.three{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}
hr{border:0;border-top:1px solid #292c34;margin:18px 0}
code{color:#a7d1ff}
small{color:#777d88}
.check{display:flex;align-items:center;gap:8px;margin-top:13px}
.check input{width:auto}
.staticFields.hidden{display:none}
@media(max-width:700px){.two,.three{grid-template-columns:1fr}}
</style>
</head>
<body>
<div class="wrap">
<header>
<div>
<h1>Chrono Broadcast — Télécommande</h1>
<div class="muted">Diagnostic · Réseau · Batterie · OTA</div>
</div>
<div id="online" class="badge">CONNEXION...</div>
</header>

<div class="grid">
<section class="card">
<div class="muted">État télécommande</div>
<div id="action" class="big">—</div>
<div class="row"><span>Compteur commandes</span><strong id="counter">0</strong></div>
<div class="row"><span>Firmware</span><strong id="firmware">—</strong></div>
<div class="row"><span>Uptime</span><strong id="uptime">—</strong></div>
<hr>
<div class="actions">
<button class="primary" onclick="post('/api/test/toggle')">TEST PLAY / PAUSE</button>
<button class="danger" onclick="post('/api/test/reset')">TEST RESET</button>
</div>
</section>

<section class="card">
<div class="muted">Batterie</div>
<div class="row">
<div>
<div id="batteryPct" class="big">—%</div>
<div id="batteryV" class="muted">— V</div>
</div>
<div class="battery"><div id="batteryFill" class="batteryFill"></div></div>
</div>
</section>

<section class="card">
<div class="muted">Réseau actuel</div>
<div class="row"><span>Mode</span><strong id="networkMode">—</strong></div>
<div class="row"><span>SSID</span><strong id="ssid">—</strong></div>
<div class="row"><span>RSSI</span><strong id="rssi">—</strong></div>
<div class="row"><span>Canal</span><strong id="channel">—</strong></div>
<div class="row"><span>IP</span><strong id="ip">—</strong></div>
<div class="row"><span>Gateway</span><strong id="gateway">—</strong></div>
<div class="row"><span>Masque</span><strong id="subnet">—</strong></div>
<div class="row"><span>DNS 1</span><strong id="dns1">—</strong></div>
<div class="row"><span>DNS 2</span><strong id="dns2">—</strong></div>
<div class="row"><span>BSSID</span><strong id="bssid">—</strong></div>
<div class="row"><span>MAC</span><strong id="mac">—</strong></div>
</section>
</div>

<section class="card" style="margin-top:12px">
<h2>Configuration réseau</h2>
<form method="POST" action="/network/save">
<div class="two">
<div>
<label>SSID Wi-Fi 2,4 GHz</label>
<input name="ssid" id="cfgSsid" maxlength="32" required>
</div>
<div>
<label>Mot de passe Wi-Fi</label>
<input name="password" maxlength="64" type="password" placeholder="Vide = conserver l'actuel">
</div>
</div>

<div class="two">
<div>
<label>Hostname</label>
<input name="hostname" id="cfgHostname" maxlength="32" value="carac-remote">
</div>
<div>
<label>Adressage IPv4</label>
<select name="mode" id="mode" onchange="toggleStatic()">
<option value="dhcp">DHCP automatique</option>
<option value="static">IP fixe</option>
</select>
</div>
</div>

<div id="staticFields" class="staticFields">
<div class="three">
<div><label>Adresse IP</label><input name="static_ip" id="cfgStaticIp" value="192.168.1.90"></div>
<div><label>Passerelle</label><input name="gateway" id="cfgGateway" value="192.168.1.1"></div>
<div><label>Masque</label><input name="subnet" id="cfgSubnet" value="255.255.255.0"></div>
</div>
<div class="two">
<div><label>DNS primaire</label><input name="dns1" id="cfgDns1" value="1.1.1.1"></div>
<div><label>DNS secondaire</label><input name="dns2" id="cfgDns2" value="8.8.8.8"></div>
</div>
</div>

<label class="check">
<input type="checkbox" name="mdns" value="1" id="cfgMdns" checked>
Activer mDNS (<span id="mdnsPreview">carac-remote.local</span>)
</label>

<hr>
<div class="muted">Point d'accès de secours</div>
<div class="two">
<div>
<label>SSID AP secours</label>
<input name="ap_ssid" id="cfgApSsid" maxlength="32" value="CARAC-REMOTE-SETUP">
</div>
<div>
<label>Mot de passe AP secours</label>
<input name="ap_password" maxlength="64" type="password" placeholder="Vide = conserver l'actuel">
</div>
</div>

<div class="actions" style="margin-top:16px">
<button class="primary" type="submit">ENREGISTRER ET REDÉMARRER</button>
<button class="warn" type="button" onclick="if(confirm('Effacer uniquement le Wi-Fi enregistré ?')) post('/wifi/clear')">EFFACER LE WIFI</button>
</div>
</form>
<p><small>En DHCP, IP/gateway/masque/DNS sont fournis par le routeur. En IP fixe, gardez l'adresse hors de la plage DHCP ou réservez-la dans le routeur. Si le réseau devient introuvable, le point d'accès de secours démarre automatiquement.</small></p>
</section>

<section class="card" style="margin-top:12px">
<h2>Veille automatique</h2>
<form method="POST" action="/power/save">
<div class="two">
<div>
<label>Mode</label>
<select name="sleep_mode" id="sleepMode" onchange="toggleSleepFields()">
<option value="never">Jamais</option>
<option value="timer">Après une durée d'inactivité</option>
</select>
</div>
<div id="sleepFields">
<label>Délai</label>
<div class="two">
<input name="sleep_value" id="sleepValue" type="number" min="1" max="1440" value="5">
<select name="sleep_unit" id="sleepUnit">
<option value="minutes">minute(s)</option>
<option value="hours">heure(s)</option>
</select>
</div>
</div>
</div>
<div class="actions" style="margin-top:16px">
<button class="primary" type="submit">ENREGISTRER</button>
</div>
</form>
<p><small>La veille coupe le Wi-Fi et met l'ESP8266 en Light Sleep. Le bouton PLAY / PAUSE sur D5 / GPIO14 réveille la télécommande et cet appui est immédiatement envoyé comme commande PLAY / PAUSE. Aucun câblage supplémentaire n'est nécessaire.</small></p>
</section>

<section class="card" style="margin-top:12px">
<h2>Battery Logger — calibration temporaire</h2>
<div class="grid">
<div>
<div class="row"><span>État</span><strong id="batteryLogState">—</strong></div>
<div class="row"><span>Durée</span><strong id="batteryLogElapsed">—</strong></div>
<div class="row"><span>Échantillons</span><strong id="batteryLogSamples">—</strong></div>
<div class="row"><span>ADC brut</span><strong id="batteryAdcRaw">—</strong></div>
</div>
<div>
<p class="muted">Un point est enregistré toutes les 60 secondes dans LittleFS. Tant que le logger est actif, la veille automatique est inhibée pour permettre une décharge complète.</p>
<div class="actions">
<button class="primary" type="button" onclick="post('/battery-log/start')">DÉMARRER</button>
<button type="button" onclick="post('/battery-log/stop')">ARRÊTER</button>
<button type="button" onclick="location.href='/battery-log.csv'">TÉLÉCHARGER CSV</button>
<button class="danger" type="button" onclick="if(confirm('Effacer le log batterie ?')) post('/battery-log/clear')">EFFACER</button>
</div>
</div>
</div>
</section>

<section class="card" style="margin-top:12px">
<h2>Mise à jour firmware sans USB</h2>
<p>Envoyer le fichier <code>CHRONO_BROADCAST_REMOTE_OTA.bin</code> depuis la page OTA.</p>
<div class="actions">
<button class="primary" onclick="location.href='/update'">OUVRIR LA MISE À JOUR OTA</button>
<button onclick="post('/api/restart')">REDÉMARRER</button>
</div>
<p><small>ArduinoOTA reste également actif pour un téléversement réseau depuis Arduino IDE.</small></p>
</section>
</div>

<script>
const e=(id)=>document.getElementById(id);
const post=async(url)=>{
  try{await fetch(url,{method:'POST'});setTimeout(load,300)}
  catch(err){alert('Commande impossible')}
}
const fmtUptime=(s)=>{
  s=Number(s)||0;
  const d=Math.floor(s/86400);s%=86400;
  const h=Math.floor(s/3600);s%=3600;
  const m=Math.floor(s/60);
  return(d?d+'j ':'')+String(h).padStart(2,'0')+':'+String(m).padStart(2,'0');
}
const batteryColor=(p)=>{
  if(p<=15)return'#ff4d5a';
  if(p<=35)return'#f0a528';
  return'#39d879';
}
const toggleStatic=()=>{
  e('staticFields').classList.toggle('hidden',e('mode').value!=='static');
}
const toggleSleepFields=()=>{
  e('sleepFields').style.opacity=e('sleepMode').value==='timer'?'1':'.35';
  e('sleepValue').disabled=e('sleepMode').value!=='timer';
  e('sleepUnit').disabled=e('sleepMode').value!=='timer';
}
e('cfgHostname').addEventListener('input',()=>{e('mdnsPreview').textContent=(e('cfgHostname').value||'carac-remote')+'.local'});
const load=async()=>{
  try{
    const r=await fetch('/status?ts='+Date.now(),{cache:'no-store'});
    const s=await r.json();

    e('online').textContent=s.ap_mode&&!s.wifi_connected?'MODE SECOURS':'EN LIGNE';
    e('online').classList.add('ok');

    e('action').textContent=s.last_action;
    e('counter').textContent=s.counter;
    e('firmware').textContent='V'+s.firmware;
    e('uptime').textContent=fmtUptime(s.uptime_s);

    if(s.battery_charging){
      e('batteryPct').textContent='EN CHARGE';
      e('batteryV').textContent='—';
      e('batteryFill').style.width='100%';
      e('batteryFill').style.background='#4fa3ff';
    }else{
      e('batteryPct').textContent=s.battery_percent+'%';
      e('batteryV').textContent=Number(s.battery_voltage).toFixed(2)+' V';
      e('batteryFill').style.width=Math.max(0,Math.min(100,s.battery_percent))+'%';
      e('batteryFill').style.background=batteryColor(s.battery_percent);
    }

    e('batteryLogState').textContent=s.battery_log_active?'ENREGISTREMENT ACTIF':'ARRÊTÉ';
    e('batteryLogElapsed').textContent=fmtUptime(s.battery_log_elapsed_s);
    e('batteryLogSamples').textContent=s.battery_log_samples;
    e('batteryAdcRaw').textContent=s.battery_adc_raw;

    e('networkMode').textContent=s.network_mode;
    e('ssid').textContent=s.ssid||'—';
    e('rssi').textContent=s.wifi_connected?s.rssi+' dBm':'—';
    e('channel').textContent=s.wifi_connected?s.channel:'—';
    e('ip').textContent=s.ip||'—';
    e('gateway').textContent=s.gateway||'—';
    e('subnet').textContent=s.subnet||'—';
    e('dns1').textContent=s.dns1||'—';
    e('dns2').textContent=s.dns2||'—';
    e('bssid').textContent=s.bssid||'—';
    e('mac').textContent=s.mac||'—';

    if(!e('cfgSsid').dataset.loaded){
      e('cfgSsid').value=s.configured_ssid||'';
      e('cfgHostname').value=s.hostname||'carac-remote';
      e('mode').value=s.network_mode==='STATIC'?'static':'dhcp';
      e('cfgStaticIp').value=s.configured_static_ip||'192.168.1.90';
      e('cfgGateway').value=s.configured_gateway||'192.168.1.1';
      e('cfgSubnet').value=s.configured_subnet||'255.255.255.0';
      e('cfgDns1').value=s.configured_dns1||'1.1.1.1';
      e('cfgDns2').value=s.configured_dns2||'8.8.8.8';
      e('cfgApSsid').value=s.ap_ssid||'CARAC-REMOTE-SETUP';
      e('cfgMdns').checked=!!s.mdns_enabled;
      const sleepSeconds=Number(s.sleep_timeout_s)||0;
      e('sleepMode').value=sleepSeconds>0?'timer':'never';

      if(sleepSeconds>0 && sleepSeconds%3600===0){
        e('sleepUnit').value='hours';
        e('sleepValue').value=Math.max(1,Math.round(sleepSeconds/3600));
      }else{
        e('sleepUnit').value='minutes';
        e('sleepValue').value=Math.max(1,Math.round(sleepSeconds/60)||5);
      }

      e('cfgSsid').dataset.loaded='1';
      toggleStatic();
      toggleSleepFields();
      e('mdnsPreview').textContent=(e('cfgHostname').value||'carac-remote')+'.local';
    }
  }catch(err){
    e('online').textContent='HORS LIGNE';
    e('online').classList.remove('ok');
  }
}
toggleStatic();
toggleSleepFields();
setInterval(load,1000);
load();
</script>
</body>
</html>
)HTML";

void handleRoot() {
  markActivity();
  addCorsHeaders();
  server.send_P(200, "text/html; charset=utf-8", PAGE_HTML);
}

bool validIPv4(const String& value) {
  IPAddress ip;
  return ip.fromString(value);
}

void handleNetworkSave() {
  markActivity();

  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "SSID manquant");
    return;
  }

  const String ssid = server.arg("ssid");
  const String newPassword = server.arg("password");
  const String hostname = server.arg("hostname");
  const String mode = server.arg("mode");
  const String staticIp = server.arg("static_ip");
  const String gateway = server.arg("gateway");
  const String subnet = server.arg("subnet");
  const String dns1 = server.arg("dns1");
  const String dns2 = server.arg("dns2");
  const String apSsid = server.arg("ap_ssid");
  const String newApPassword = server.arg("ap_password");

  if (ssid.length() < 1 || ssid.length() > 32) {
    server.send(400, "text/plain", "SSID invalide");
    return;
  }

  if (hostname.length() < 1 || hostname.length() > 32) {
    server.send(400, "text/plain", "Hostname invalide");
    return;
  }

  const bool dhcp = mode != "static";

  if (!dhcp) {
    if (
      !validIPv4(staticIp) ||
      !validIPv4(gateway) ||
      !validIPv4(subnet) ||
      !validIPv4(dns1) ||
      !validIPv4(dns2)
    ) {
      server.send(400, "text/plain", "Parametre IPv4 statique invalide");
      return;
    }
  }

  if (apSsid.length() < 1 || apSsid.length() > 32) {
    server.send(400, "text/plain", "SSID AP invalide");
    return;
  }

  if (newApPassword.length() > 0 && newApPassword.length() < 8) {
    server.send(400, "text/plain", "Mot de passe AP: minimum 8 caracteres");
    return;
  }

  copyString(config.ssid, sizeof(config.ssid), ssid);

  if (newPassword.length() > 0) {
    copyString(config.password, sizeof(config.password), newPassword);
  }

  copyString(config.hostname, sizeof(config.hostname), hostname);

  config.dhcp = dhcp ? 1 : 0;
  config.mdnsEnabled = server.hasArg("mdns") ? 1 : 0;

  copyString(config.staticIp, sizeof(config.staticIp), staticIp);
  copyString(config.gateway, sizeof(config.gateway), gateway);
  copyString(config.subnet, sizeof(config.subnet), subnet);
  copyString(config.dns1, sizeof(config.dns1), dns1);
  copyString(config.dns2, sizeof(config.dns2), dns2);

  copyString(config.apSsid, sizeof(config.apSsid), apSsid);

  if (newApPassword.length() > 0) {
    copyString(config.apPassword, sizeof(config.apPassword), newApPassword);
  }

  saveConfig();

  server.send(
    200,
    "text/html; charset=utf-8",
    "<html><body style='font-family:Arial;background:#111;color:#fff;padding:30px'>"
    "<h2>Configuration reseau enregistree</h2>"
    "<p>La telecommande redemarre...</p>"
    "</body></html>"
  );

  delay(1200);
  ESP.restart();
}

void handleWifiClear() {
  clearWifiOnly();
  server.send(200, "application/json", "{\"ok\":true,\"restart\":true}");
  delay(500);
  ESP.restart();
}

void handleTestToggle() {
  markActivity();
  commandToggle();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleTestReset() {
  markActivity();
  commandReset();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleRestart() {
  markActivity();
  server.send(200, "application/json", "{\"ok\":true}");
  delay(500);
  ESP.restart();
}

void handleBatteryLogStart() {
  markActivity();
  startBatteryLogger();
  server.send(200, "application/json", "{\"ok\":true,\"active\":true}");
}

void handleBatteryLogStop() {
  markActivity();
  stopBatteryLogger();
  server.send(200, "application/json", "{\"ok\":true,\"active\":false}");
}

void handleBatteryLogClear() {
  markActivity();
  stopBatteryLogger();

  if (LittleFS.exists(BATTERY_LOG_PATH)) {
    LittleFS.remove(BATTERY_LOG_PATH);
  }

  batteryLogSamples = 0;
  lastBatteryLogAt = 0;
  batteryLoggerStartedAt = 0;
  ensureBatteryLogFile();

  server.send(200, "application/json", "{\"ok\":true}");
}

void handleBatteryLogDownload() {
  markActivity();
  ensureBatteryLogFile();

  File f = LittleFS.open(BATTERY_LOG_PATH, "r");
  if (!f) {
    server.send(500, "text/plain", "Log batterie indisponible");
    return;
  }

  server.sendHeader(
    "Content-Disposition",
    "attachment; filename=chrono-battery-discharge.csv"
  );
  server.streamFile(f, "text/csv");
  f.close();
}

void handlePowerSave() {
  const String mode = server.arg("sleep_mode");

  if (mode == "never") {
    config.sleepTimeoutSeconds = 0;
  } else {
    long value = server.arg("sleep_value").toInt();
    const String unit = server.arg("sleep_unit");

    if (value < 1) value = 1;
    if (value > 1440) value = 1440;

    if (unit == "hours") {
      if (value > 168) value = 168;
      config.sleepTimeoutSeconds = (uint32_t)value * 3600UL;
    } else {
      config.sleepTimeoutSeconds = (uint32_t)value * 60UL;
    }
  }

  saveConfig();
  markActivity();

  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/network/save", HTTP_POST, handleNetworkSave);
  server.on("/wifi/clear", HTTP_POST, handleWifiClear);
  server.on("/api/test/toggle", HTTP_POST, handleTestToggle);
  server.on("/api/test/reset", HTTP_POST, handleTestReset);
  server.on("/api/restart", HTTP_POST, handleRestart);
  server.on("/power/save", HTTP_POST, handlePowerSave);
  server.on("/battery-log/start", HTTP_POST, handleBatteryLogStart);
  server.on("/battery-log/stop", HTTP_POST, handleBatteryLogStop);
  server.on("/battery-log/clear", HTTP_POST, handleBatteryLogClear);
  server.on("/battery-log.csv", HTTP_GET, handleBatteryLogDownload);

  httpUpdater.setup(&server, "/update");

  server.onNotFound([]() {
    if (apMode) {
      server.sendHeader("Location", "/", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "404");
    }
  });

  server.begin();
  Serial.println("Serveur HTTP OK");
}

void setupArduinoOTA() {
  String hostname = String(config.hostname);
  hostname.trim();
  if (hostname.length() == 0) hostname = "carac-remote";

  ArduinoOTA.setHostname(hostname.c_str());

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Arduino : debut");
    ledOn();
  });

  ArduinoOTA.onEnd([]() {
    ledOff();
    Serial.println("\nOTA Arduino : termine");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    const unsigned int pct = total ? (progress * 100U) / total : 0;
    Serial.printf("OTA : %u%%\r", pct);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    ledOff();
    Serial.printf("Erreur OTA [%u]\n", error);
  });

  ArduinoOTA.begin();
  Serial.println("ArduinoOTA OK");
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  ledOff();

  Serial.begin(115200);
  delay(400);

  if (!LittleFS.begin()) {
    Serial.println("[LittleFS] erreur de montage");
  } else {
    ensureBatteryLogFile();
  }

  Serial.println();
  Serial.println("==================================");
  Serial.print(" CHRONO BROADCAST REMOTE V");
  Serial.println(FIRMWARE_VERSION);
  Serial.println("==================================");

  flashLED(3, 70, 70);

  loadConfig();
  updateBattery(true);

  if (!connectToConfiguredWifi(WIFI_CONNECT_TIMEOUT_MS)) {
    startFallbackAP();
  }

  setupWebServer();
  setupArduinoOTA();

  Serial.print("Batterie : ");
  Serial.print(batteryVoltage, 2);
  Serial.print(" V / ");
  Serial.print(batteryPercent);
  Serial.println(" %");

  if (apMode && WiFi.status() != WL_CONNECTED) {
    Serial.println("Configuration : http://192.168.4.1");
  } else {
    String hostname = String(config.hostname);
    hostname.trim();
    if (hostname.length() == 0) hostname = "carac-remote";

    Serial.print("Interface : http://");
    Serial.print(hostname);
    Serial.println(".local");
  }

  flashLED(2, 120, 100);
  markActivity();
}

void loop() {
  handleButton();
  updateBattery();
  updateBatteryLogger();
  maintainWifi();

  if (apMode) {
    dnsServer.processNextRequest();
  }

  server.handleClient();
  ArduinoOTA.handle();

  if (mdnsStarted) {
    MDNS.update();
  }

  if (autoSleepDue()) {
    enterAutomaticLightSleep();
  }

  yield();
}
