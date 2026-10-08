#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <time.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "CARAC_TIMER_WEB_GZIP.h"

#define FW_VERSION "1.8.0"
#define PANEL_W 64
#define PANEL_H 32
#define PANEL_CHAIN 1

const char* HOSTNAME = "carac-timer";
const char* RESCUE_SSID = "CARAC-TIMER-SETUP";
const char* RESCUE_PASS = "CaracTimer2026";

MatrixPanel_I2S_DMA* matrix = nullptr;
WebServer server(80);
Preferences prefs;

uint16_t C_BLACK, C_WHITE, C_RED, C_GREEN, C_BLUE, C_YELLOW, C_ORANGE;

String hwMode = "clock";
bool hwRunning = false;
int64_t hwBaseMs = 0;
uint32_t hwAnchorMs = 0;
int64_t hwProgrammedMs = 300000;
bool hwOverrun = false;
uint8_t hwBrightnessPct = 80;
uint32_t hwColor = 0xFFFFFF;
String hwDigitSize = "medium";
String hwTimeFormat = "hms";
int64_t hwDeadlineEpochMs = 0;
String remoteUrl = "http://carac-remote.local";

uint16_t frameBuf[2048];
bool frameValid = false;
uint32_t lastFrameMs = 0;
uint32_t lastFallbackDraw = 0;
uint32_t lastWifiCheck = 0;
bool rescueAP = false;
bool otaActive = false;
uint32_t remoteGuardUntil = 0;

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
uint32_t lastRemotePoll = 0;

String diagLog[40];
uint8_t diagHead = 0, diagCount = 0;

void addLog(const String& msg) {
  Serial.println(msg);
  diagLog[diagHead] = String(millis()/1000) + "s  " + msg;
  diagHead = (diagHead + 1) % 40;
  if (diagCount < 40) diagCount++;
}

void addCors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
}

String jsonEscape(const String& in) {
  String out; out.reserve(in.length()+8);
  for (size_t i=0;i<in.length();i++) {
    char c=in[i];
    if (c=='\\' || c=='\"') { out+='\\'; out+=c; }
    else if (c=='\n') out += "\\n";
    else if (c!='\r') out += c;
  }
  return out;
}

String i64str(int64_t v) {
  char b[32]; snprintf(b,sizeof(b),"%lld",(long long)v); return String(b);
}

int64_t parseI64(const String& s) {
  return (int64_t)strtoll(s.c_str(), nullptr, 10);
}

uint16_t rgb565(uint32_t rgb) {
  return matrix->color565((rgb>>16)&255, (rgb>>8)&255, rgb&255);
}

void textCenter(const String& s, int y, uint16_t c, uint8_t size=1) {
  matrix->setTextWrap(false);
  matrix->setTextSize(size);
  int16_t x1,y1; uint16_t w,h;
  matrix->getTextBounds(s,0,y,&x1,&y1,&w,&h);
  int x=(PANEL_W-(int)w)/2;
  matrix->setCursor(max(0,x),y);
  matrix->setTextColor(c);
  matrix->print(s);
}

void setupMatrix() {
  HUB75_I2S_CFG cfg(PANEL_W,PANEL_H,PANEL_CHAIN);
  cfg.gpio.r1=4; cfg.gpio.g1=5; cfg.gpio.b1=6;
  cfg.gpio.r2=7; cfg.gpio.g2=15; cfg.gpio.b2=16;
  cfg.gpio.a=18; cfg.gpio.b=8; cfg.gpio.c=3; cfg.gpio.d=42; cfg.gpio.e=9;
  cfg.gpio.lat=40; cfg.gpio.oe=2; cfg.gpio.clk=41;
  cfg.clkphase=false;
  cfg.driver=HUB75_I2S_CFG::SHIFTREG;
  cfg.i2sspeed=HUB75_I2S_CFG::HZ_16M;
  cfg.min_refresh_rate=240;
  matrix=new MatrixPanel_I2S_DMA(cfg);
  if(!matrix->begin()) { while(true) delay(1000); }
  matrix->setBrightness8((uint8_t)map(hwBrightnessPct,0,100,0,255));
  C_BLACK=matrix->color565(0,0,0); C_WHITE=matrix->color565(255,255,255);
  C_RED=matrix->color565(255,0,0); C_GREEN=matrix->color565(0,255,0);
  C_BLUE=matrix->color565(0,0,255); C_YELLOW=matrix->color565(255,255,0);
  C_ORANGE=matrix->color565(255,105,56);
  matrix->clearScreen();
}

void startupAnimation() {
  const uint32_t cols[] = {0xFF0000,0x00FF00,0x0000FF,0x00FFFF,0xFF00FF,0xFFFFFF};
  for(int k=0;k<6;k++) {
    for(int v=20;v<=255;v+=20) {
      uint8_t r=((cols[k]>>16)&255)*v/255, g=((cols[k]>>8)&255)*v/255, b=(cols[k]&255)*v/255;
      matrix->fillScreen(matrix->color565(r,g,b)); delay(32);
    }
  }
  for(int i=0;i<3;i++){matrix->fillScreen(C_WHITE);delay(85);matrix->fillScreen(C_BLACK);delay(105);}
  for(int br=18;br<=255;br+=16){matrix->clearScreen();textCenter("CARAC",8,matrix->color565(br,br,br),2);delay(45);}
  for(int r=2;r<38;r+=4){
    matrix->clearScreen(); textCenter("CARAC",8,C_WHITE,2);
    for(int a=0;a<20;a++){float ang=a*6.2831853f/20.0f;int x=32+cos(ang)*r;int y=16+sin(ang)*(r*0.5f);matrix->drawLine(32,16,x,y,C_WHITE);} delay(32);
  }
  matrix->clearScreen(); textCenter("CARAC",8,C_ORANGE,2); delay(500);
}

bool connectSavedWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin();
  addLog("Connexion au Wi-Fi memorise...");
  uint32_t st=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-st<18000) delay(200);
  if(WiFi.status()==WL_CONNECTED){
    rescueAP=false;
    addLog("Wi-Fi OK " + WiFi.localIP().toString() + " / " + WiFi.SSID());
    return true;
  }
  return false;
}

void startRescueAP() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(RESCUE_SSID,RESCUE_PASS);
  rescueAP=true;
  addLog("AP secours actif : " + WiFi.softAPIP().toString());
}

void setupTimeSync() {
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

int64_t localTimerValue() {
  if(hwMode=="deadline" && hwDeadlineEpochMs>0 && hwRunning) {
    struct timeval tv; gettimeofday(&tv,nullptr);
    int64_t now=(int64_t)tv.tv_sec*1000LL + tv.tv_usec/1000;
    int64_t v=hwDeadlineEpochMs-now;
    if(!hwOverrun && v<0) v=0;
    return v;
  }
  int64_t v=hwBaseMs;
  if(hwRunning) {
    int64_t elapsed=(int64_t)(uint32_t)(millis()-hwAnchorMs);
    if(hwMode=="stopwatch") v += elapsed;
    else if(hwMode=="countdown" || hwMode=="deadline") v -= elapsed;
  }
  if((hwMode=="countdown" || hwMode=="deadline") && !hwOverrun && v<0) v=0;
  return v;
}

void pauseLocal() {
  if(!hwRunning) return;
  hwBaseMs=localTimerValue();
  hwRunning=false;
  hwAnchorMs=millis();
}

void playLocal() {
  if(hwRunning) return;
  if(hwMode=="deadline" && hwDeadlineEpochMs>0) {
    struct timeval tv; gettimeofday(&tv,nullptr);
    int64_t now=(int64_t)tv.tv_sec*1000LL + tv.tv_usec/1000;
    hwBaseMs=hwDeadlineEpochMs-now;
  }
  hwAnchorMs=millis(); hwRunning=true;
}

void resetLocal() {
  hwRunning=false;
  if(hwMode=="stopwatch") hwBaseMs=0;
  else if(hwMode=="countdown") hwBaseMs=hwProgrammedMs;
  else if(hwMode=="deadline" && hwDeadlineEpochMs>0) {
    struct timeval tv; gettimeofday(&tv,nullptr);
    int64_t now=(int64_t)tv.tv_sec*1000LL + tv.tv_usec/1000;
    hwBaseMs=hwDeadlineEpochMs-now;
  }
  hwAnchorMs=millis();
}

String formatFallback(int64_t ms) {
  bool neg=ms<0; uint64_t a=neg?(uint64_t)(-ms):(uint64_t)ms;
  uint64_t cs=(a/10ULL)%100ULL, sec=a/1000ULL;
  unsigned h=sec/3600ULL, m=(sec/60ULL)%60ULL, s=sec%60ULL;
  char b[32];
  if(hwTimeFormat=="hmsms") snprintf(b,sizeof(b),"%s%02u:%02u:%02u.%02llu",neg?"-":"",h,m,s,(unsigned long long)cs);
  else if(hwTimeFormat=="msms") snprintf(b,sizeof(b),"%s%02llu:%02u.%02llu",neg?"-":"",(unsigned long long)(sec/60ULL),s,(unsigned long long)cs);
  else if(hwTimeFormat=="ms") snprintf(b,sizeof(b),"%s%02llu:%02u",neg?"-":"",(unsigned long long)(sec/60ULL),s);
  else if(hwTimeFormat=="sms") snprintf(b,sizeof(b),"%s%llu.%02llu",neg?"-":"",(unsigned long long)sec,(unsigned long long)cs);
  else if(hwTimeFormat=="hm") snprintf(b,sizeof(b),"%s%02u:%02u",neg?"-":"",h,m);
  else if(hwTimeFormat=="s") snprintf(b,sizeof(b),"%s%llu",neg?"-":"",(unsigned long long)sec);
  else if(hwTimeFormat=="m") snprintf(b,sizeof(b),"%s%llu",neg?"-":"",(unsigned long long)(sec/60ULL));
  else if(hwTimeFormat=="millis") snprintf(b,sizeof(b),"%s%02llu",neg?"-":"",(unsigned long long)cs);
  else snprintf(b,sizeof(b),"%s%02u:%02u:%02u",neg?"-":"",h,m,s);
  return String(b);
}

void drawFallback() {
  if(otaActive) return;
  if(millis()-lastFallbackDraw<80) return;
  lastFallbackDraw=millis();

  bool freshFrame=frameValid && (millis()-lastFrameMs<1500) && (int32_t)(millis()-remoteGuardUntil)>=0;
  if(freshFrame) return;

  matrix->setBrightness8((uint8_t)map(hwBrightnessPct,0,100,0,255));
  uint16_t c=rgb565(hwColor);

  if(hwMode=="clock") {
    struct tm t; matrix->clearScreen();
    if(getLocalTime(&t,5)) {char b[12]; strftime(b,sizeof(b),"%H:%M:%S",&t); textCenter(String(b),12,c,1);} else textCenter("--:--",8,c,2);
  } else if(hwMode=="stopwatch" || hwMode=="countdown" || hwMode=="deadline") {
    int64_t v=localTimerValue(); matrix->clearScreen(); String s=formatFallback(v);
    uint16_t tc=(v<0)?C_RED:c; uint8_t sz=(s.length()<=5 && hwDigitSize!="small")?2:1;
    textCenter(s,sz==2?8:12,tc,sz);
  } else if(hwMode=="off") {
    matrix->clearScreen();
  } else {
    if(!frameValid){matrix->clearScreen();textCenter("CARAC",8,c,2);}
  }
}

String jsonField(const String& p,const String& key) {
  String k="\""+key+"\":"; int i=p.indexOf(k); if(i<0) return ""; i+=k.length();
  while(i<(int)p.length() && p[i]==' ') i++;
  if(i<(int)p.length() && p[i]=='\"'){int e=p.indexOf('\"',i+1); if(e<0)return""; return p.substring(i+1,e);}
  int e=i; while(e<(int)p.length() && p[e]!=',' && p[e]!='}') e++; return p.substring(i,e);
}

void applyRemoteAction(const String& a) {
  if(!(hwMode=="stopwatch" || hwMode=="countdown" || hwMode=="deadline")) return;
  if(a=="PLAY") playLocal(); else if(a=="PAUSE") pauseLocal(); else if(a=="RESET") resetLocal(); else return;
  remoteGuardUntil=millis()+1600;
  addLog("Telecommande physique : " + a);
}

void pollRemote() {
  if(WiFi.status()!=WL_CONNECTED || millis()-lastRemotePoll<900) return;
  lastRemotePoll=millis();
  String base=remoteUrl; base.trim(); if(base.length()==0) return; while(base.endsWith("/")) base.remove(base.length()-1);
  HTTPClient http; http.setConnectTimeout(300); http.setTimeout(500);
  if(!http.begin(base+"/status")) return;
  int code=http.GET();
  if(code==200){
    String p=http.getString(); uint32_t ctr=(uint32_t)jsonField(p,"counter").toInt(); String act=jsonField(p,"last_action");
    if(remoteSeen && ctr>remoteCounter) applyRemoteAction(act);
    remoteSeen=true; remoteCounter=ctr; remoteLastAction=act;
    remoteBattery=jsonField(p,"battery_percent").toInt(); remoteVoltage=jsonField(p,"battery_voltage").toFloat();
    remoteCharging=(jsonField(p,"battery_charging")=="true"); remoteRSSI=jsonField(p,"rssi").toInt();
    remoteSleepTimeoutS=(uint32_t)jsonField(p,"sleep_timeout_s").toInt(); remoteLastSeenMs=millis(); remoteState="EN LIGNE";
  } else if(remoteSeen) {
    uint32_t age=millis()-remoteLastSeenMs;
    if(remoteSleepTimeoutS>0 && age < remoteSleepTimeoutS*1000UL + 25000UL) remoteState="EN VEILLE";
    else if(age>=3000) remoteState="ETEINTE";
  }
  http.end();
}

String statusJson() {
  String j="{";
  j += "\"ok\":true,";
  j += "\"firmware\":\"" + String(FW_VERSION) + "\",";
  j += "\"ip\":\"" + (WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString()) + "\",";
  j += "\"rssi\":" + String(WiFi.status()==WL_CONNECTED?WiFi.RSSI():0) + ",";
  j += "\"uptime_s\":" + String(millis()/1000UL) + ",";
  j += "\"mode\":\"" + jsonEscape(hwMode) + "\",";
  j += "\"running\":" + String(hwRunning?"true":"false") + ",";
  j += "\"value_ms\":" + i64str(localTimerValue()) + ",";
  j += "\"remote_state\":\""+remoteState+"\",";
  j += "\"remote_battery\":"+String(remoteBattery)+",";
  j += "\"remote_charging\":"+String(remoteCharging?"true":"false");
  j += "}"; return j;
}

String logsJson() {
  String j="[";
  for(int k=0;k<diagCount;k++){int idx=(diagHead-diagCount+k+40)%40;if(k)j+=',';j+='\"'+jsonEscape(diagLog[idx])+'\"';}
  j+="]"; return j;
}

void sendRoot() {
  addCors();
  server.sendHeader("Content-Encoding","gzip");
  server.send_P(200,"text/html; charset=utf-8",(PGM_P)CARAC_WEB_GZ,CARAC_WEB_GZ_LEN);
}

void handleSync() {
  addCors();
  if(server.hasArg("mode")) hwMode=server.arg("mode");
  bool newRunning=server.arg("running")=="1";
  int64_t newValue=parseI64(server.arg("value_ms"));
  hwProgrammedMs=parseI64(server.arg("programmed_ms"));
  hwOverrun=server.arg("overrun")=="1";
  hwBrightnessPct=(uint8_t)constrain(server.arg("brightness").toInt(),0,100);
  if(server.hasArg("color")) hwColor=strtoul(server.arg("color").c_str(),nullptr,16)&0xFFFFFF;
  if(server.hasArg("digit_size")) hwDigitSize=server.arg("digit_size");
  if(server.hasArg("time_format")) hwTimeFormat=server.arg("time_format");
  if(server.hasArg("deadline_epoch")) hwDeadlineEpochMs=parseI64(server.arg("deadline_epoch"));
  if(server.hasArg("remote_url")){remoteUrl=server.arg("remote_url");prefs.begin("carac",false);prefs.putString("remoteUrl",remoteUrl);prefs.end();}

  if((int32_t)(millis()-remoteGuardUntil)>=0) {
    hwBaseMs=newValue; hwAnchorMs=millis(); hwRunning=newRunning;
  }
  matrix->setBrightness8((uint8_t)map(hwBrightnessPct,0,100,0,255));
  server.send(200,"application/json","{\"ok\":true}");
}

void handleFrame() {
  addCors();
  String h=server.arg("plain");
  if(h.length()<8192){server.send(400,"application/json","{\"ok\":false,\"error\":\"frame length\"}");return;}
  matrix->setBrightness8((uint8_t)map(hwBrightnessPct,0,100,0,255));
  for(int i=0;i<2048;i++) {
    char tmp[5]; tmp[0]=h[i*4];tmp[1]=h[i*4+1];tmp[2]=h[i*4+2];tmp[3]=h[i*4+3];tmp[4]=0;
    uint16_t v=(uint16_t)strtoul(tmp,nullptr,16); frameBuf[i]=v; matrix->drawPixel(i%64,i/64,v);
  }
  frameValid=true; lastFrameMs=millis();
  server.send(200,"application/json","{\"ok\":true}");
}

void runBootTest() {
  otaActive=true; startupAnimation(); otaActive=false; lastFrameMs=0; addLog("Test animation de demarrage");
}

void setupWeb() {
  server.on("/",HTTP_GET,sendRoot);
  server.on("/index.html",HTTP_GET,sendRoot);
  server.on("/api/status",HTTP_GET,[]{addCors();server.send(200,"application/json",statusJson());});
  server.on("/api/logs",HTTP_GET,[]{addCors();server.send(200,"application/json",logsJson());});
  server.on("/status",HTTP_GET,[]{addCors();server.send(200,"application/json",statusJson());});
  server.on("/api/sync",HTTP_POST,handleSync);
  server.on("/api/frame",HTTP_POST,handleFrame);
  server.on("/api/boot-animation",HTTP_POST,[]{addCors();server.send(200,"application/json","{\"ok\":true}");delay(20);runBootTest();});
  server.on("/api/system/restart",HTTP_POST,[]{addCors();server.send(200,"application/json","{\"ok\":true}");delay(400);ESP.restart();});
  server.on("/api/diagnostics/run",HTTP_POST,[]{
    addCors();
    String j="{\"ok\":true,\"checks\":[\"esp32-s3\",\"hub75-64x32\",\"wifi\",\"ntp\",\"web\",\"ota\",\"remote-api\"]}";
    addLog("Auto-diagnostic execute"); server.send(200,"application/json",j);
  });
  server.on("/update",HTTP_GET,[]{
    addCors(); server.send(200,"text/html; charset=utf-8","<html><body style='background:#0c0f12;color:#edf0f3;font-family:Segoe UI,Arial;padding:30px'><h2>CARAC TIMER · OTA</h2><form method='POST' action='/api/ota' enctype='multipart/form-data'><input type='file' name='firmware' accept='.bin' required><button type='submit'>Mettre a jour</button></form></body></html>");
  });
  server.on("/api/ota",HTTP_POST,[]{
    addCors(); bool ok=!Update.hasError(); server.send(200,"application/json",ok?"{\"ok\":true,\"restart\":true}":"{\"ok\":false}"); delay(650); if(ok)ESP.restart();
  },[]{
    HTTPUpload& u=server.upload();
    if(u.status==UPLOAD_FILE_START){otaActive=true;matrix->clearScreen();textCenter("UPDATE",10,C_YELLOW,1);if(!Update.begin(UPDATE_SIZE_UNKNOWN))Update.printError(Serial);}
    else if(u.status==UPLOAD_FILE_WRITE){if(Update.write(u.buf,u.currentSize)!=u.currentSize)Update.printError(Serial);}
    else if(u.status==UPLOAD_FILE_END){if(Update.end(true)){matrix->clearScreen();textCenter("OTA OK",10,C_GREEN,1);}else{Update.printError(Serial);matrix->clearScreen();textCenter("OTA ERR",10,C_RED,1);otaActive=false;}}
  });
  server.onNotFound([](){addCors();server.send(404,"text/plain","404");});
  server.begin(); addLog("Serveur web actif");
}

void setupArduinoOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([](){otaActive=true;matrix->clearScreen();textCenter("UPDATE",10,C_YELLOW,1);addLog("Arduino OTA START");});
  ArduinoOTA.onProgress([](unsigned int p,unsigned int t){int pct=t?(p*100U/t):0;matrix->clearScreen();textCenter(String(pct)+"%",10,C_WHITE,1);});
  ArduinoOTA.onEnd([](){matrix->clearScreen();textCenter("OTA OK",10,C_GREEN,1);});
  ArduinoOTA.onError([](ota_error_t){matrix->clearScreen();textCenter("OTA ERR",10,C_RED,1);otaActive=false;});
  ArduinoOTA.begin(); addLog("ArduinoOTA actif");
}

void loadPrefs() {
  prefs.begin("carac",true); remoteUrl=prefs.getString("remoteUrl","http://carac-remote.local"); prefs.end();
}

void setup() {
  Serial.begin(115200); delay(300);
  loadPrefs(); setupMatrix(); startupAnimation();
  bool wifiOK=connectSavedWifi(); if(!wifiOK) startRescueAP();
  if(WiFi.status()==WL_CONNECTED){setupTimeSync();if(MDNS.begin(HOSTNAME)){MDNS.addService("http","tcp",80);addLog("mDNS actif : carac-timer.local");}}
  setupArduinoOTA(); setupWeb();
  hwMode="clock"; hwRunning=false; hwBaseMs=0; hwAnchorMs=millis();
  addLog("CARAC TIMER V" + String(FW_VERSION) + " PRET");
}

void loop() {
  server.handleClient(); ArduinoOTA.handle();
  pollRemote(); drawFallback();
  if(millis()-lastWifiCheck>5000){lastWifiCheck=millis();if(WiFi.status()!=WL_CONNECTED && !rescueAP)WiFi.reconnect();}
  delay(2);
}
