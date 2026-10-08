#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <time.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#define FW_VERSION "1.0.0"
#define PANEL_W 64
#define PANEL_H 32
#define PANEL_CHAIN 1

const char* HOSTNAME = "carac-timer";
const char* HOME_SSID_HINT = "Sarah_Charles_Home";
const char* RESCUE_SSID = "CARAC-TIMER-SETUP";
const char* RESCUE_PASS = "CaracTimer2026";
const char* REMOTE_HOST = "carac-remote.local";

MatrixPanel_I2S_DMA* matrix = nullptr;
WebServer server(80);
Preferences prefs;

enum Mode : uint8_t { CLOCK, STOPWATCH, COUNTDOWN, DEADLINE, STANDBY, EDITOR, OFFMODE };
Mode mode = CLOCK;

uint16_t C_BLACK, C_WHITE, C_RED, C_GREEN, C_BLUE, C_YELLOW, C_ORANGE;
uint8_t brightness = 90;
uint32_t displayColor = 0xFFFFFF;
bool running = false;
int64_t storedMs = 0;
uint32_t runStartMs = 0;
int64_t countdownPresetMs = 5LL * 60LL * 1000LL;
int deadlineHour = 12, deadlineMinute = 0;
bool deadlineActive = false;
bool overrunEnabled = true;
bool rescueAP = false;
String clockFormat = "HM";
String timerFormat = "AUTO";

uint8_t customBitmap[256] = {0};
bool customBitmapValid = false;

String logLines[24];
uint8_t logHead = 0, logCount = 0;

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

uint32_t lastDraw = 0;
uint32_t lastWifiCheck = 0;
bool otaActive = false;

void addLog(const String& s) {
  logLines[logHead] = String(millis()/1000) + "s  " + s;
  logHead = (logHead + 1) % 24;
  if (logCount < 24) logCount++;
  Serial.println(s);
}

String jsonEscape(const String& in) {
  String o; o.reserve(in.length()+8);
  for (size_t i=0;i<in.length();i++) {
    char c=in[i];
    if (c=='\\' || c=='"') { o+='\\'; o+=c; }
    else if (c=='\n') o+="\\n";
    else if (c!='\r') o+=c;
  }
  return o;
}

uint16_t color565(uint32_t rgb) {
  return matrix->color565((rgb>>16)&255,(rgb>>8)&255,rgb&255);
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

String fmtSignedMs(int64_t ms, bool forceHMS=false) {
  bool neg=ms<0;
  uint64_t a=neg ? (uint64_t)(-ms) : (uint64_t)ms;
  uint64_t sec=a/1000ULL;
  unsigned h=sec/3600ULL;
  unsigned m=(sec%3600ULL)/60ULL;
  unsigned s=sec%60ULL;
  char b[20];
  if (forceHMS || h>0 || timerFormat=="HMS") snprintf(b,sizeof(b),"%s%02u:%02u:%02u",neg?"-":"",h,m,s);
  else snprintf(b,sizeof(b),"%s%02u:%02u",neg?"-":"",m,s);
  return String(b);
}

int64_t currentTimerMs() {
  if (!running) return storedMs;
  return storedMs + (int64_t)(uint32_t)(millis()-runStartMs);
}

int64_t currentCountdownMs() {
  int64_t elapsed = running ? (int64_t)(uint32_t)(millis()-runStartMs) : 0;
  int64_t v = storedMs - elapsed;
  if (!overrunEnabled && v<0) v=0;
  return v;
}

void playTimer() {
  if (running) return;
  runStartMs=millis();
  running=true;
  addLog("PLAY");
}
void pauseTimer() {
  if (!running) return;
  if (mode==STOPWATCH) storedMs=currentTimerMs();
  else if (mode==COUNTDOWN) storedMs=currentCountdownMs();
  running=false;
  addLog("PAUSE");
}
void resetTimer() {
  running=false;
  if (mode==STOPWATCH) storedMs=0;
  else if (mode==COUNTDOWN) storedMs=countdownPresetMs;
  addLog("RESET");
}
void toggleTimer() { running ? pauseTimer() : playTimer(); }

void adjustTimer(int32_t deltaSec) {
  int64_t d=(int64_t)deltaSec*1000LL;
  if (mode==STOPWATCH) {
    int64_t now=currentTimerMs()+d;
    storedMs=max<int64_t>(0,now);
    if (running) runStartMs=millis();
  } else if (mode==COUNTDOWN) {
    int64_t now=currentCountdownMs()+d;
    storedMs=now;
    if (running) runStartMs=millis();
  }
  addLog("Correction " + String(deltaSec) + "s");
}

void setMode(Mode m) {
  if (running) pauseTimer();
  mode=m;
  prefs.begin("carac",false);
  prefs.putUChar("mode",(uint8_t)mode);
  prefs.end();
  addLog("Mode " + String((int)mode));
}

void saveSettings() {
  prefs.begin("carac",false);
  prefs.putUChar("bright",brightness);
  prefs.putUInt("color",displayColor);
  prefs.putLong64("cdpreset",countdownPresetMs);
  prefs.putBool("overrun",overrunEnabled);
  prefs.putInt("dlh",deadlineHour);
  prefs.putInt("dlm",deadlineMinute);
  prefs.putString("clockfmt",clockFormat);
  prefs.putString("timerfmt",timerFormat);
  prefs.putBytes("bitmap",customBitmap,sizeof(customBitmap));
  prefs.putBool("bmvalid",customBitmapValid);
  prefs.end();
}
void loadSettings() {
  prefs.begin("carac",true);
  mode=(Mode)prefs.getUChar("mode",(uint8_t)CLOCK);
  brightness=prefs.getUChar("bright",90);
  displayColor=prefs.getUInt("color",0xFFFFFF);
  countdownPresetMs=prefs.getLong64("cdpreset",5LL*60LL*1000LL);
  overrunEnabled=prefs.getBool("overrun",true);
  deadlineHour=prefs.getInt("dlh",12);
  deadlineMinute=prefs.getInt("dlm",0);
  clockFormat=prefs.getString("clockfmt","HM");
  timerFormat=prefs.getString("timerfmt","AUTO");
  customBitmapValid=prefs.getBool("bmvalid",false);
  if (prefs.getBytesLength("bitmap")==sizeof(customBitmap)) prefs.getBytes("bitmap",customBitmap,sizeof(customBitmap));
  prefs.end();
  storedMs=(mode==COUNTDOWN)?countdownPresetMs:0;
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
  matrix->begin();
  matrix->setBrightness8(brightness);
  C_BLACK=matrix->color565(0,0,0); C_WHITE=matrix->color565(255,255,255);
  C_RED=matrix->color565(255,0,0); C_GREEN=matrix->color565(0,255,0);
  C_BLUE=matrix->color565(0,0,255); C_YELLOW=matrix->color565(255,255,0);
  C_ORANGE=matrix->color565(255,100,0);
  matrix->clearScreen();
}

void startupAnimation() {
  const uint32_t cols[]={0xFF0000,0x00FF00,0x0000FF,0x00FFFF,0xFF00FF,0xFFFFFF};
  for (int k=0;k<6;k++) {
    for (int v=20;v<=255;v+=25) {
      uint8_t r=((cols[k]>>16)&255)*v/255, g=((cols[k]>>8)&255)*v/255, b=(cols[k]&255)*v/255;
      matrix->fillScreen(matrix->color565(r,g,b)); delay(35);
    }
  }
  for(int i=0;i<3;i++){ matrix->fillScreen(C_WHITE); delay(90); matrix->fillScreen(C_BLACK); delay(90); }
  matrix->clearScreen();
  for(int br=20;br<=255;br+=18){ matrix->clearScreen(); textCenter("CARAC",8,matrix->color565(br,br,br),2); delay(45); }
  for(int r=2;r<36;r+=4){
    matrix->clearScreen(); textCenter("CARAC",8,C_WHITE,2);
    for(int a=0;a<16;a++){ float ang=a*6.2831853f/16.0f; int x=32+cos(ang)*r, y=16+sin(ang)*r/2; matrix->drawLine(32,16,x,y,C_WHITE); }
    delay(35);
  }
  matrix->clearScreen();
  textCenter("CARAC",8,matrix->color565(255,90,0),2);
  delay(600);
}

void drawClock() {
  struct tm t;
  matrix->clearScreen();
  if(!getLocalTime(&t,5)){ textCenter("--:--",8,color565(displayColor),2); return; }
  char b[16];
  if(clockFormat=="HMS"){ strftime(b,sizeof(b),"%H:%M:%S",&t); textCenter(String(b),12,color565(displayColor),1); }
  else { strftime(b,sizeof(b),"%H:%M",&t); textCenter(String(b),8,color565(displayColor),2); }
}

void drawTimerValue(int64_t ms) {
  matrix->clearScreen();
  String s=fmtSignedMs(ms);
  uint16_t c=color565(displayColor);
  if(mode==COUNTDOWN && ms<0) c=C_RED;
  else if(mode==COUNTDOWN && ms<=60000) c=C_RED;
  else if(mode==COUNTDOWN && ms<=300000) c=C_YELLOW;
  uint8_t size=(s.length()<=5)?2:1;
  textCenter(s,size==2?8:12,c,size);
}

void drawDeadline() {
  struct tm t;
  matrix->clearScreen();
  if(!getLocalTime(&t,5)){ textCenter("--:--",8,C_WHITE,2); return; }
  struct tm target=t;
  target.tm_hour=deadlineHour; target.tm_min=deadlineMinute; target.tm_sec=0;
  time_t now=mktime(&t), tar=mktime(&target);
  int64_t diff=(int64_t)(tar-now)*1000LL;
  String s=fmtSignedMs(diff,true);
  textCenter(s,12,diff<0?C_RED:color565(displayColor),1);
}

void drawStandby() {
  static int x=-30; static int dir=1; static uint32_t last=0;
  if(millis()-last>70){ last=millis(); x+=dir; if(x>35||x<-30) dir=-dir; }
  matrix->clearScreen();
  matrix->setTextSize(1); matrix->setTextColor(color565(displayColor)); matrix->setCursor(x,12); matrix->print("CARAC TIMER");
}

void drawEditor() {
  matrix->clearScreen();
  if(!customBitmapValid) { textCenter("EMPTY",12,C_WHITE,1); return; }
  uint16_t c=color565(displayColor);
  for(int y=0;y<32;y++) for(int x=0;x<64;x++) {
    int p=y*64+x, byte=p>>3, bit=7-(p&7);
    if(customBitmap[byte]&(1<<bit)) matrix->drawPixel(x,y,c);
  }
}

void updateDisplay() {
  if(otaActive) return;
  if(millis()-lastDraw<80) return;
  lastDraw=millis();
  matrix->setBrightness8(brightness);
  switch(mode){
    case CLOCK: drawClock(); break;
    case STOPWATCH: drawTimerValue(currentTimerMs()); break;
    case COUNTDOWN: drawTimerValue(currentCountdownMs()); break;
    case DEADLINE: drawDeadline(); break;
    case STANDBY: drawStandby(); break;
    case EDITOR: drawEditor(); break;
    case OFFMODE: matrix->clearScreen(); break;
  }
}

void showAlert() {
  uint32_t start=millis();
  while(millis()-start<900){ matrix->fillScreen(C_RED); textCenter("ALERT",8,C_WHITE,2); delay(120); matrix->clearScreen(); delay(80); }
  addLog("ALERT");
}

void showPrestart() {
  running=false;
  for(int n=5;n>=1;n--){
    matrix->clearScreen();
    for(int i=0;i<5;i++) matrix->fillCircle(8+i*12,16,4,i<(6-n)?C_RED:matrix->color565(25,25,25));
    delay(650);
  }
  matrix->fillScreen(C_GREEN); delay(350); matrix->clearScreen();
  if(mode==STOPWATCH){storedMs=0;playTimer();}
  if(mode==COUNTDOWN){storedMs=countdownPresetMs;playTimer();}
  addLog("PRESTART");
}

bool connectSavedWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin();
  addLog("Connexion Wi-Fi memorisee...");
  uint32_t st=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-st<15000) delay(200);
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
  addLog("AP secours " + WiFi.softAPIP().toString());
}

void setupTimeSync() {
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3","pool.ntp.org","time.google.com","time.cloudflare.com");
}

void applyRemoteAction(const String& a) {
  if(a=="PLAY"){ if(!running) playTimer(); }
  else if(a=="PAUSE"){ if(running) pauseTimer(); }
  else if(a=="RESET") resetTimer();
}

void pollRemote() {
  if(WiFi.status()!=WL_CONNECTED || millis()-lastRemotePoll<1000) return;
  lastRemotePoll=millis();
  HTTPClient http;
  http.setTimeout(350);
  String url=String("http://")+REMOTE_HOST+"/status";
  if(!http.begin(url)) return;
  int code=http.GET();
  if(code==200){
    String p=http.getString();
    auto val=[&](const String& key)->String{
      String k="\""+key+"\":";
      int i=p.indexOf(k); if(i<0) return "";
      i+=k.length(); while(i<(int)p.length() && p[i]==' ') i++;
      if(p[i]=='\"'){ int e=p.indexOf('"',i+1); return p.substring(i+1,e); }
      int e=i; while(e<(int)p.length() && p[e]!=',' && p[e]!='}') e++;
      return p.substring(i,e);
    };
    uint32_t ctr=(uint32_t)val("counter").toInt();
    String act=val("last_action");
    if(remoteSeen && ctr!=remoteCounter) applyRemoteAction(act);
    remoteSeen=true; remoteCounter=ctr; remoteLastAction=act;
    remoteBattery=val("battery_percent").toInt();
    remoteVoltage=val("battery_voltage").toFloat();
    remoteCharging=(val("battery_charging")=="true");
    remoteRSSI=val("rssi").toInt();
    remoteSleepTimeoutS=(uint32_t)val("sleep_timeout_s").toInt();
    remoteLastSeenMs=millis(); remoteState="EN LIGNE";
  } else {
    if(remoteSeen){
      uint32_t age=(millis()-remoteLastSeenMs)/1000;
      if(remoteSleepTimeoutS>0 && age<remoteSleepTimeoutS+25) remoteState="EN VEILLE";
      else if(age>=3) remoteState="ETEINTE";
    }
  }
  http.end();
}

String modeName() {
  switch(mode){case CLOCK:return"CLOCK";case STOPWATCH:return"STOPWATCH";case COUNTDOWN:return"COUNTDOWN";case DEADLINE:return"DEADLINE";case STANDBY:return"STANDBY";case EDITOR:return"EDITOR";default:return"OFF";}
}

String statusJson() {
  int64_t val=0;
  if(mode==STOPWATCH) val=currentTimerMs(); else if(mode==COUNTDOWN) val=currentCountdownMs();
  String j="{";
  j+="\"firmware\":\""+String(FW_VERSION)+"\",";
  j+="\"mode\":\""+modeName()+"\",";
  j+="\"running\":" + String(running?"true":"false") + ",";
  j+="\"value_ms\":"+String((long long)val)+",";
  j+="\"brightness\":"+String(brightness)+",";
  j+="\"color\":"+String(displayColor)+",";
  j+="\"wifi_connected\":"+String(WiFi.status()==WL_CONNECTED?"true":"false")+",";
  j+="\"ssid\":\""+jsonEscape(WiFi.SSID())+"\",";
  j+="\"ip\":\""+jsonEscape(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString())+"\",";
  j+="\"rssi\":"+String(WiFi.status()==WL_CONNECTED?WiFi.RSSI():0)+",";
  j+="\"uptime_s\":"+String(millis()/1000)+",";
  j+="\"remote_state\":\""+remoteState+"\",";
  j+="\"remote_battery\":"+String(remoteBattery)+",";
  j+="\"remote_voltage\":"+String(remoteVoltage,2)+",";
  j+="\"remote_charging\":"+String(remoteCharging?"true":"false")+",";
  j+="\"remote_rssi\":"+String(remoteRSSI);
  j+="}";
  return j;
}

String logsJson(){
  String j="[";
  for(int k=0;k<logCount;k++){
    int idx=(logHead-logCount+k+24)%24;
    if(k)j+=",";
    j+="\""+jsonEscape(logLines[idx])+"\"";
  }
  j+="]";
  return j;
}

const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CARAC TIMER REGIE</title><style>
:root{color-scheme:dark;--bg:#08090b;--panel:#111318;--line:#282c34;--mut:#838995;--green:#27d777;--red:#ef4444;--amber:#f4aa17;--blue:#4299ff}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:#fff;font:14px Arial,Helvetica,sans-serif}.app{max-width:1500px;margin:auto;padding:18px}
header{display:flex;justify-content:space-between;align-items:center;gap:15px;margin-bottom:14px}h1{font-size:23px;margin:0;letter-spacing:.5px}h2{font-size:14px;text-transform:uppercase;letter-spacing:1px;color:#cdd1d8;margin:0 0 12px}
.badges{display:flex;gap:7px;flex-wrap:wrap}.badge{padding:6px 9px;border-radius:999px;background:#252932;color:#aaa;font-weight:700}.ok{background:#133c29;color:#65e69b}.bad{background:#421c20;color:#ff8e94}
.grid{display:grid;grid-template-columns:minmax(420px,1.35fr) minmax(350px,1fr);gap:14px}.card{background:var(--panel);border:1px solid var(--line);border-radius:13px;padding:15px;margin-bottom:14px}
.matrixWrap{display:grid;place-items:center;background:#050607;border-radius:10px;padding:18px}.matrix{width:min(100%,768px);aspect-ratio:2/1;display:grid;grid-template-columns:repeat(64,1fr);grid-template-rows:repeat(32,1fr);gap:1px;background:#050505}.px{background:#111;border-radius:50%}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}.sp{justify-content:space-between}.modes{display:grid;grid-template-columns:repeat(6,1fr);gap:7px}
button,input,select{font:inherit}button{border:1px solid #353a45;border-radius:8px;background:#22262e;color:#fff;padding:10px 12px;font-weight:700;cursor:pointer}button:hover{filter:brightness(1.18)}button.active{outline:2px solid var(--blue)}button.play{background:#14783f}button.pause{background:#8b6114}button.reset{background:#8d282c}button.alert{background:#b41420}
input,select{background:#090b0f;border:1px solid #343943;border-radius:8px;color:#fff;padding:9px}input[type=number]{width:88px}input[type=color]{padding:2px;width:54px;height:37px}
.bigctl{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}.bigctl button{font-size:17px;padding:15px}
.kv{display:grid;grid-template-columns:1fr auto;gap:8px;border-bottom:1px solid #222630;padding:7px 0}.mut{color:var(--mut)}.tabs{display:flex;gap:5px;margin-bottom:12px}.tab{padding:8px 10px}.pane{display:none}.pane.on{display:block}
canvas{image-rendering:pixelated;width:100%;max-width:640px;aspect-ratio:2/1;background:#000;border:1px solid #444;cursor:crosshair}.log{height:140px;overflow:auto;background:#08090c;border-radius:8px;padding:10px;font:12px monospace;color:#c7ccd5;white-space:pre-wrap}
.drawer{display:grid;grid-template-columns:1fr 1fr;gap:10px}.full{grid-column:1/-1}.ota{display:flex;gap:8px;align-items:center}
@media(max-width:900px){.grid{grid-template-columns:1fr}.modes{grid-template-columns:repeat(3,1fr)}}@media(max-width:520px){.modes{grid-template-columns:repeat(2,1fr)}.drawer{grid-template-columns:1fr}}
</style></head><body><div class="app">
<header><div><h1>CARAC TIMER · RÉGIE</h1><div class="mut">64×32 HUB75 · firmware <span id="fw">—</span></div></div><div class="badges"><span id="wifi" class="badge">WIFI</span><span id="remote" class="badge">REMOTE</span><span id="run" class="badge">PAUSE</span></div></header>
<div class="grid"><main>
<section class="card"><h2>Sortie plateau</h2><div class="matrixWrap"><div id="matrix" class="matrix"></div></div></section>
<section class="card"><h2>Mode</h2><div class="modes">
<button data-mode="clock">HORLOGE</button><button data-mode="stopwatch">CHRONO</button><button data-mode="countdown">COUNTDOWN</button><button data-mode="deadline">HEURE CIBLE</button><button data-mode="standby">STANDBY</button><button data-mode="off">OFF</button>
</div></section>
<section class="card"><h2>Transport</h2><div class="bigctl"><button class="play" onclick="cmd('/api/play')">PLAY</button><button class="pause" onclick="cmd('/api/pause')">PAUSE</button><button class="reset" onclick="cmd('/api/reset')">RESET</button></div>
<div class="row" style="margin-top:10px"><button onclick="adj(-60)">-1 MIN</button><button onclick="adj(-30)">-30 S</button><button onclick="adj(-10)">-10 S</button><button onclick="adj(10)">+10 S</button><button onclick="adj(30)">+30 S</button><button onclick="adj(60)">+1 MIN</button><button onclick="cmd('/api/prestart')" style="margin-left:auto">PRESTART F1</button><button class="alert" onclick="cmd('/api/alert')">ALERTE</button></div>
</section>
<section class="card"><div class="tabs"><button class="tab active" data-pane="chrono">CHRONO</button><button class="tab" data-pane="countdown">COUNTDOWN</button><button class="tab" data-pane="deadline">HEURE CIBLE</button><button class="tab" data-pane="editor">ÉDITEUR 64×32</button></div>
<div id="p-chrono" class="pane on"><div class="row"><span>Saisie manuelle</span><input id="manual" placeholder="00:00 ou 00:00:00"><button onclick="manual()">APPLIQUER</button></div></div>
<div id="p-countdown" class="pane"><div class="row"><button onclick="preset(60)">1 MIN</button><button onclick="preset(180)">3 MIN</button><button onclick="preset(300)">5 MIN</button><button onclick="preset(600)">10 MIN</button><button onclick="preset(900)">15 MIN</button><input id="cdmin" type="number" value="5" min="0"><span>min</span><input id="cdsec" type="number" value="0" min="0" max="59"><span>s</span><button onclick="setcd()">CHARGER</button><label><input id="overrun" type="checkbox" checked onchange="saveSettings()"> négatif après 00:00</label></div></div>
<div id="p-deadline" class="pane"><div class="row"><input id="dlh" type="number" min="0" max="23" value="12"><span>:</span><input id="dlm" type="number" min="0" max="59" value="0"><button onclick="setdeadline()">APPLIQUER</button></div></div>
<div id="p-editor" class="pane"><canvas id="ed" width="64" height="32"></canvas><div class="row" style="margin-top:8px"><button onclick="clearEd()">EFFACER</button><button onclick="sendEd()">ENVOYER À L'ÉCRAN</button></div></div>
</section>
</main><aside>
<section class="card"><h2>Affichage</h2><div class="drawer"><label>Couleur<br><input id="color" type="color" value="#ffffff" onchange="saveSettings()"></label><label>Luminosité<br><input id="bright" type="range" min="1" max="255" value="90" oninput="bval.textContent=this.value" onchange="saveSettings()"><span id="bval">90</span></label><label>Horloge<br><select id="clockfmt" onchange="saveSettings()"><option value="HM">HH:MM</option><option value="HMS">HH:MM:SS</option></select></label><label>Chrono<br><select id="timerfmt" onchange="saveSettings()"><option value="AUTO">AUTO</option><option value="HMS">HH:MM:SS</option></select></label></div></section>
<section class="card"><h2>Télécommande présentateur</h2><div class="kv"><span>État</span><b id="rstate">—</b></div><div class="kv"><span>Batterie</span><b id="rbat">—</b></div><div class="kv"><span>Tension</span><b id="rvolt">—</b></div><div class="kv"><span>RSSI</span><b id="rrssi">—</b></div></section>
<section class="card"><h2>Système</h2><div class="kv"><span>IP</span><b id="ip">—</b></div><div class="kv"><span>RSSI</span><b id="rssi">—</b></div><div class="kv"><span>Uptime</span><b id="uptime">—</b></div><div class="kv"><span>Mode</span><b id="mode">—</b></div><div class="log" id="log"></div></section>
<section class="card"><h2>Mise à jour OTA</h2><form class="ota" method="POST" action="/update" enctype="multipart/form-data"><input type="file" name="firmware" accept=".bin" required><button type="submit">INSTALLER</button></form><div class="mut" style="margin-top:8px">Arduino OTA : carac-timer.local</div></section>
</aside></div></div>
<script>
const E=id=>document.getElementById(id), px=[]; const mat=E('matrix');
for(let i=0;i<2048;i++){let d=document.createElement('i');d.className='px';mat.appendChild(d);px.push(d)}
async function cmd(u,opt){let r=await fetch(u,opt);return r}
document.querySelectorAll('[data-mode]').forEach(b=>b.onclick=()=>cmd('/api/mode?set='+b.dataset.mode))
document.querySelectorAll('.tab').forEach(b=>b.onclick=()=>{document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));b.classList.add('active');document.querySelectorAll('.pane').forEach(x=>x.classList.remove('on'));E('p-'+b.dataset.pane).classList.add('on')})
function adj(s){cmd('/api/adjust?sec='+s)} function preset(s){E('cdmin').value=Math.floor(s/60);E('cdsec').value=s%60;setcd()}
function setcd(){cmd('/api/countdown?sec='+(+E('cdmin').value*60 + +E('cdsec').value))}
function setdeadline(){cmd('/api/deadline?h='+E('dlh').value+'&m='+E('dlm').value)}
function manual(){cmd('/api/manual?value='+encodeURIComponent(E('manual').value))}
function saveSettings(){let c=E('color').value.substring(1);cmd('/api/settings?brightness='+E('bright').value+'&color='+c+'&clockfmt='+E('clockfmt').value+'&timerfmt='+E('timerfmt').value+'&overrun='+(E('overrun').checked?1:0))}
function renderPreview(s){px.forEach(p=>p.style.background='#111');let text='';if(s.mode==='CLOCK') text=new Date().toLocaleTimeString('fr-FR',{hour:'2-digit',minute:'2-digit'});else if(s.mode==='STOPWATCH'||s.mode==='COUNTDOWN'){let n=Math.abs(Math.trunc(s.value_ms/1000)),m=Math.floor(n/60),ss=n%60;text=(s.value_ms<0?'-':'')+String(m).padStart(2,'0')+':'+String(ss).padStart(2,'0')}else text=s.mode; let cv=document.createElement('canvas');cv.width=64;cv.height=32;let c=cv.getContext('2d');c.fillStyle='#000';c.fillRect(0,0,64,32);c.fillStyle='#fff';c.font='bold 12px monospace';c.textAlign='center';c.textBaseline='middle';c.fillText(text,32,16);let im=c.getImageData(0,0,64,32).data,col=E('color').value;for(let i=0;i<2048;i++)if(im[i*4]>40)px[i].style.background=col}
async function load(){try{let s=await (await fetch('/status',{cache:'no-store'})).json();E('fw').textContent=s.firmware;E('wifi').textContent=s.wifi_connected?'WIFI OK':'WIFI';E('wifi').className='badge '+(s.wifi_connected?'ok':'bad');E('run').textContent=s.running?'PLAY':'PAUSE';E('run').className='badge '+(s.running?'ok':'');E('ip').textContent=s.ip;E('rssi').textContent=s.rssi+' dBm';E('uptime').textContent=s.uptime_s+' s';E('mode').textContent=s.mode;E('remote').textContent='REMOTE '+s.remote_state;E('remote').className='badge '+(s.remote_state==='EN LIGNE'?'ok':s.remote_state==='ETEINTE'?'bad':'');E('rstate').textContent=s.remote_state;E('rbat').textContent=s.remote_charging?'EN CHARGE':(s.remote_battery>=0?s.remote_battery+' %':'—');E('rvolt').textContent=s.remote_voltage?s.remote_voltage.toFixed(2)+' V':'—';E('rrssi').textContent=s.remote_rssi?s.remote_rssi+' dBm':'—';document.querySelectorAll('[data-mode]').forEach(b=>b.classList.toggle('active',b.dataset.mode.toUpperCase()===s.mode));renderPreview(s);let l=await (await fetch('/api/log')).json();E('log').textContent=l.join('\n');E('log').scrollTop=99999}catch(e){E('wifi').className='badge bad'}}
setInterval(load,1000);load();
const ed=E('ed'),ctx=ed.getContext('2d');ctx.imageSmoothingEnabled=false;let drawing=false,erase=false;
function pos(ev){let r=ed.getBoundingClientRect();return [Math.floor((ev.clientX-r.left)*64/r.width),Math.floor((ev.clientY-r.top)*32/r.height)]}
function paint(ev){if(!drawing)return;let[x,y]=pos(ev);ctx.fillStyle=erase?'#000':'#fff';ctx.fillRect(x,y,1,1)}
ed.onpointerdown=e=>{drawing=true;erase=e.button===2;paint(e)};ed.onpointermove=paint;window.onpointerup=()=>drawing=false;ed.oncontextmenu=e=>e.preventDefault();
function clearEd(){ctx.fillStyle='#000';ctx.fillRect(0,0,64,32)}
async function sendEd(){let d=ctx.getImageData(0,0,64,32).data,b=new Uint8Array(256);for(let p=0;p<2048;p++)if(d[p*4]>80)b[p>>3]|=1<<(7-(p&7));let h=[...b].map(x=>x.toString(16).padStart(2,'0')).join('');await fetch('/api/editor',{method:'POST',headers:{'Content-Type':'text/plain'},body:h});cmd('/api/mode?set=editor')}
</script></body></html>
)HTML";

void setupWeb() {
  server.on("/",HTTP_GET,[]{server.send_P(200,"text/html; charset=utf-8",PAGE);});
  server.on("/status",HTTP_GET,[]{server.send(200,"application/json",statusJson());});
  server.on("/api/log",HTTP_GET,[]{server.send(200,"application/json",logsJson());});
  server.on("/api/mode",HTTP_GET,[]{
    String s=server.arg("set");
    if(s=="clock")setMode(CLOCK); else if(s=="stopwatch")setMode(STOPWATCH); else if(s=="countdown"){setMode(COUNTDOWN);storedMs=countdownPresetMs;}
    else if(s=="deadline")setMode(DEADLINE); else if(s=="standby")setMode(STANDBY); else if(s=="editor")setMode(EDITOR); else if(s=="off")setMode(OFFMODE);
    server.send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/play",HTTP_GET,[]{playTimer();server.send(200,"application/json","{\"ok\":true}");});
  server.on("/api/pause",HTTP_GET,[]{pauseTimer();server.send(200,"application/json","{\"ok\":true}");});
  server.on("/api/reset",HTTP_GET,[]{resetTimer();server.send(200,"application/json","{\"ok\":true}");});
  server.on("/api/adjust",HTTP_GET,[]{adjustTimer(server.arg("sec").toInt());server.send(200,"application/json","{\"ok\":true}");});
  server.on("/api/countdown",HTTP_GET,[]{
    int sec=max(0,server.arg("sec").toInt()); countdownPresetMs=(int64_t)sec*1000LL; storedMs=countdownPresetMs; running=false; setMode(COUNTDOWN); saveSettings();
    server.send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/deadline",HTTP_GET,[]{
    deadlineHour=constrain(server.arg("h").toInt(),0,23); deadlineMinute=constrain(server.arg("m").toInt(),0,59); deadlineActive=true; setMode(DEADLINE); saveSettings();
    server.send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/manual",HTTP_GET,[]{
    String v=server.arg("value"); int a=0,b=0,c=0; long sec=0;
    if(sscanf(v.c_str(),"%d:%d:%d",&a,&b,&c)==3) sec=a*3600L+b*60L+c; else if(sscanf(v.c_str(),"%d:%d",&a,&b)==2) sec=a*60L+b;
    if(mode==STOPWATCH) storedMs=max<long>(0,sec)*1000LL; else if(mode==COUNTDOWN){countdownPresetMs=(int64_t)max<long>(0,sec)*1000LL;storedMs=countdownPresetMs;}
    running=false; saveSettings(); server.send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/settings",HTTP_GET,[]{
    brightness=constrain(server.arg("brightness").toInt(),1,255);
    String cs=server.arg("color"); if(cs.length()) displayColor=strtoul(cs.c_str(),nullptr,16)&0xFFFFFF;
    clockFormat=server.arg("clockfmt"); timerFormat=server.arg("timerfmt"); overrunEnabled=server.arg("overrun")=="1"; saveSettings();
    server.send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/alert",HTTP_GET,[]{server.send(200,"application/json","{\"ok\":true}");showAlert();});
  server.on("/api/prestart",HTTP_GET,[]{server.send(200,"application/json","{\"ok\":true}");showPrestart();});
  server.on("/api/editor",HTTP_POST,[]{
    String h=server.arg("plain"); if(h.length()>=512){for(int i=0;i<256;i++)customBitmap[i]=(uint8_t)strtoul(h.substring(i*2,i*2+2).c_str(),nullptr,16);customBitmapValid=true;saveSettings();}
    server.send(200,"application/json","{\"ok\":true}");
  });
  server.on("/update",HTTP_GET,[]{
    server.send(200,"text/html","<html><body style='background:#111;color:#fff;font-family:Arial;padding:30px'><h2>CARAC TIMER OTA</h2><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='firmware' accept='.bin' required><button type='submit'>INSTALLER</button></form></body></html>");
  });
  server.on("/update",HTTP_POST,[]{
    bool ok=!Update.hasError(); server.send(200,"text/plain",ok?"UPDATE OK - REDÉMARRAGE":"UPDATE ERROR"); delay(500); if(ok)ESP.restart();
  },[]{
    HTTPUpload& u=server.upload();
    if(u.status==UPLOAD_FILE_START){otaActive=true;matrix->clearScreen();textCenter("UPDATE",10,C_YELLOW,1);Update.begin(UPDATE_SIZE_UNKNOWN);}
    else if(u.status==UPLOAD_FILE_WRITE){if(Update.write(u.buf,u.currentSize)!=u.currentSize)Update.printError(Serial);}
    else if(u.status==UPLOAD_FILE_END){if(Update.end(true)){matrix->clearScreen();textCenter("OTA OK",10,C_GREEN,1);}else Update.printError(Serial);}
  });
  server.onNotFound([](){server.send(404,"text/plain","404");});
  server.begin(); addLog("Serveur web actif");
}

void setupOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([](){otaActive=true;matrix->clearScreen();textCenter("UPDATE",10,C_YELLOW,1);addLog("Arduino OTA START");});
  ArduinoOTA.onProgress([](unsigned int p,unsigned int t){int pct=t?(p*100U/t):0;matrix->clearScreen();textCenter(String(pct)+"%",10,C_WHITE,1);});
  ArduinoOTA.onEnd([](){matrix->clearScreen();textCenter("OTA OK",10,C_GREEN,1);});
  ArduinoOTA.onError([](ota_error_t e){matrix->clearScreen();textCenter("OTA ERR",10,C_RED,1);otaActive=false;});
  ArduinoOTA.begin(); addLog("ArduinoOTA actif");
}

void setup() {
  Serial.begin(115200); delay(300);
  loadSettings();
  setupMatrix();
  startupAnimation();
  bool ok=connectSavedWifi();
  if(!ok) startRescueAP();
  if(WiFi.status()==WL_CONNECTED){setupTimeSync();if(MDNS.begin(HOSTNAME)){MDNS.addService("http","tcp",80);addLog("mDNS actif");}}
  setupOTA();
  setupWeb();
  addLog("CARAC TIMER V" + String(FW_VERSION) + " PRET");
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  updateDisplay();
  pollRemote();
  if(millis()-lastWifiCheck>5000){
    lastWifiCheck=millis();
    if(WiFi.status()!=WL_CONNECTED && !rescueAP){WiFi.reconnect();}
  }
  delay(2);
}
