// ============================================================
//                     ZEGAREK V2
// ============================================================

#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <Wire.h>
#include "RTClib.h"
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_now.h>
#include <time.h>
#include <esp_wifi.h>

IPAddress local_IP(192, 168, 5, 1);
IPAddress gateway(192, 168, 5, 1);
IPAddress subnet(255, 255, 255, 0);

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define DATA_PIN  6
#define CS_PIN    7
#define CLK_PIN   3
#define BTN_HOUR  1
#define BTN_MIN   2
#define LDR_PIN   0

struct __attribute__((packed)) SensorData {
    float temp;
    float voltage;
};
SensorData extData = {0.0, 0.0};

unsigned long lastSeenOutdoor = 0;
bool outdoorActive = false;
int lastRSSI = 0;
int configInterval = 1;
int nightStart = 22;
int nightEnd = 6;
int timeOffsetHours = 1;
int ntpSyncHour = 3;  // Domyślna godzina synchronizacji NTP

volatile bool pendingSend = false;
uint8_t pendingPeerAddr[6];

MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
MD_MAX72XX *mx;

DS1307 rtc;

Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;
WebServer server(80);
Preferences preferences;

int Y_OFFSET = 1;
float tempOffset = 0.0;
int lightVal = -1;
int currentSate = 5000;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 250;
unsigned long mTimer = 0;
int mode = 0;

bool showIPMode = false;
int ipScrollPos = 0;
unsigned long lastIPScroll = 0;

const uint8_t PROGMEM charMap[][5] = {
  // 0-9: cyfry
  {0x1f, 0x11, 0x1f, 0, 0}, {0x00, 0x1f, 0x00, 0, 0}, {0x1d, 0x15, 0x17, 0, 0},
  {0x15, 0x15, 0x1f, 0, 0}, {0x07, 0x04, 0x1f, 0, 0}, {0x17, 0x15, 0x1d, 0, 0},
  {0x1f, 0x15, 0x1d, 0, 0}, {0x01, 0x01, 0x1f, 0, 0}, {0x1f, 0x15, 0x1f, 0, 0},
  {0x17, 0x15, 0x1f, 0, 0},
  // 10: dwukropek migający, 11: pusty
  {0x00, 0x0a, 0x00, 0, 0}, {0x00, 0x00, 0x00, 0, 0},
  // 12-15: symbole hPa (h,P,a), F
  {0x1f, 0x05, 0x07, 0, 0}, {0x1f, 0x05, 0x1f, 0, 0}, {0x0e, 0x11, 0x11, 0, 0},
  {0x1f, 0x04, 0x1c, 0, 0},
  // 16: kropka, 17: procent, 18: stopień
  {0x00, 0x10, 0x00, 0, 0}, {0x12, 0x04, 0x09, 0, 0}, {0x06, 0x09, 0x06, 0, 0},
  // 19-21: ikony sygnału WiFi
  {0x00, 0x0e, 0x0a, 0x0e, 0}, {0x00, 0x0e, 0x08, 0x0e, 0},
  {0x00, 0x02, 0x0e, 0x02, 0},
  // 22-24: ikony baterii (pełna, średnia, niska)
  {0x1E, 0x1F, 0x1E, 0, 0}, {0x1E, 0x1B, 0x1E, 0, 0},
  {0x1E, 0x11, 0x1E, 0, 0},
  // 25: minus, 26-28: ikony sygnału RSSI
  {0x00, 0x08, 0x08, 0, 0}, {0x10, 0x00, 0x00, 0, 0},
  {0x10, 0x18, 0x00, 0, 0}, {0x10, 0x18, 0x1C, 0, 0},
  // 29: A, 30: P, 31: S, 32: T
  {0x1e, 0x05, 0x1e, 0, 0},  // A
  {0x1f, 0x05, 0x02, 0, 0},  // P
  {0x16, 0x15, 0x0d, 0, 0},  // S
  {0x01, 0x1f, 0x01, 0, 0},  // T
};

String getSuccessHTML(String title, String desc) {
  return "<html><head><meta charset='UTF-8'><style>body{text-align:center;font-family:Arial;margin-top:100px;background:#f4f4f4;}h1{font-size:80px;color:#007bff;}p{font-size:55px;}a{font-size:40px;color:#28a745;text-decoration:none;border:2px solid;padding:10px;}</style></head><body>"
         "<h1>" + title + "</h1><p>" + desc + "</p><br><a href='/'>Wróć</a></body></html>";
}

const char MAIN_page[] PROGMEM = R"=====(
<!DOCTYPE html><html><head><meta charset="UTF-8"><title>Zegarek Config</title>
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<style>
  body { font-family: Arial; text-align: center; margin-top: 20px; background: #f4f4f4; }
  .container { width: 320px; margin: 0 auto; background: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
  h2 { color: #333; margin-bottom: 5px; }
  h3 { color: #333; margin-bottom: 5px; }
  .section { border-top: 2px solid #eee; margin-top: 15px; padding-top: 10px; }
  .inp-sm  { width: 50px;  height: 45px; font-size: 22px; text-align: center; border: 2px solid #ccc; border-radius: 5px; margin: 6px 2px; }
  .inp-tz  { width: 60px;  height: 45px; font-size: 22px; text-align: center; border: 2px solid #ccc; border-radius: 5px; margin: 6px 2px; }
  .inp-int { width: 60px;  height: 45px; font-size: 22px; text-align: center; border: 2px solid #ccc; border-radius: 5px; margin: 6px 2px; }
  .inp-off { width: 70px;  height: 45px; font-size: 22px; text-align: center; border: 2px solid #ccc; border-radius: 5px; margin: 6px 2px; }
  input[type="text"], input[type="password"] { width: 80%; height: 45px; font-size: 22px; text-align: center; border: 2px solid #ccc; border-radius: 5px; margin: 10px 0; }
  input[type="submit"] { background: #28a745; color: white; padding: 15px; width: 100%; font-size: 20px; margin-top: 10px; border: none; border-radius: 4px; cursor: pointer; }
  .btn-blue   { background: #007bff !important; }
  .btn-orange { background: #ff9800 !important; }
  .btn-purple { background: #6f42c1 !important; }
  .btn-red    { background: #dc3545 !important; }
  .btn-ygreen { background: #8bc34a !important; }
  .btn-dyellow{ background: #f9a825 !important; }
  .ext-temp { font-size: 20px; font-weight: bold; color: #7b2fbe; margin: 4px 0; white-space: nowrap; }
  .ext-bat  { font-size: 20px; font-weight: bold; color: #28a745; margin: 4px 0; }
  .ext-int  { font-size: 20px; font-weight: bold; color: #ff9800; margin: 4px 0; }
  .night-icon { color: #ff9800; font-size: 20px; }
  .sep { font-size: 24px; font-weight: bold; line-height: 45px; }
</style>
</head><body><div class="container">
  <h2>Ustawienia Zegara</h2>

  <!-- GODZINA -->
  <div class="section">
    <form action="/set_time" method="POST">
      <input class="inp-sm" type="number" name="hour" placeholder="GG" min="0" max="23">
      <span class="sep">:</span>
      <input class="inp-sm" type="number" name="minute" placeholder="MM" min="0" max="59">
      <br><input type="submit" value="Ustaw Godzinę">
    </form>
  </div>

  <!-- STREFA UTC -->
  <div class="section">
    <h3>Strefa UTC: TZ_VAL h</h3>
    <form action="/set_tz" method="POST">
      <input class="inp-tz" type="number" name="tz" value="TZ_VAL" min="-12" max="14">
      <br><input type="submit" value="Zapisz Strefę" class="btn-blue">
    </form>
  </div>

  <!-- CZUJNIK ZEWNĘTRZNY -->
  <div class="section">
    <p class="ext-temp">🌡 Tem. Zew.: EXT_TEMP &deg;C</p>
    <p class="ext-bat">🔋 Bateria: EXT_BAT V</p>
    <p class="ext-int">Int: <b>INT_VAL min</b> &nbsp;<span class="night-icon">MODE_ICON</span></p>
    <form action="/set_interval" method="POST">
      <input class="inp-int" type="number" name="interval" value="INT_VAL" min="1" max="60">
      <br><input type="submit" value="Zapisz Interwał" class="btn-orange">
    </form>
  </div>

  <!-- SYNCHRONIZACJA NTP -->
  <div class="section">
    <h3>Synchronizacja: NTP_HOUR_VAL:00</h3>
    <label style="font-size:16px; color:#555;">0 - 23</label>
    <form action="/set_ntp_hour" method="POST">
      <input class="inp-sm" type="number" name="ntphour" value="NTP_HOUR_VAL" min="0" max="23">
      <br><input type="submit" value="Zapisz" class="btn-dyellow">
    </form>
  </div>

  <!-- KOREKTA TEMPERATURY -->
  <div class="section">
    <h3>Korekta: OFFSET_VAL &deg;C</h3>
    <form action="/set_offset" method="POST">
      <input class="inp-off" type="number" name="offset" step="0.1" value="OFFSET_VAL">
      <br><input type="submit" value="Zapisz Korektę" class="btn-red">
    </form>
  </div>

  <!-- NOC -->
  <div class="section">
    <h3>Noc: N_START_VAL : N_END_VAL</h3>
    <form action="/set_night" method="POST">
      <input class="inp-sm" type="number" name="nStart" value="N_START_VAL" min="0" max="23">
      <span class="sep">:</span>
      <input class="inp-sm" type="number" name="nEnd" value="N_END_VAL" min="0" max="23">
      <br><input type="submit" value="Zapisz Godziny" class="btn-purple">
    </form>
  </div>

  <!-- WIFI – NA KOŃCU -->
  <div class="section">
    <h3>WiFi: <span style="font-family:monospace;">SSID_NAME</span></h3>
    <form action="/set_wifi" method="POST">
      <input type="text" name="ssid" placeholder="SSID"><br>
      <input type="password" name="pass" id="passField" placeholder="HASŁO"><br>
      <label style="font-size:16px;">
        <input type="checkbox" onclick="document.getElementById('passField').type=this.checked?'text':'password'">
        Pokaż hasło
      </label><br>
      <input type="submit" value="Połącz z WiFi" class="btn-ygreen">
    </form>
  </div>

</div></body></html>
)=====";

void handleRoot() {
  String html = String(MAIN_page);
  html.replace("NTP_HOUR_VAL", String(ntpSyncHour));
  html.replace("OFFSET_VAL", String(tempOffset, 1));
  html.replace("INT_VAL",    String(configInterval));
  html.replace("N_START_VAL", String(nightStart));
  html.replace("N_END_VAL",   String(nightEnd));
  html.replace("TZ_VAL",     String(timeOffsetHours));
  DateTime now = rtc.now();
  bool isNight = (nightStart < nightEnd)
    ? (now.hour() >= nightStart && now.hour() < nightEnd)
    : (now.hour() >= nightStart || now.hour() < nightEnd);
  html.replace("MODE_ICON", isNight ? "🌙" : "☀️");
  String ssidName = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "brak";
  if (ssidName.length() > 12) ssidName = ssidName.substring(0, 12);
  while (ssidName.length() < 12) ssidName += "&nbsp;";
  html.replace("SSID_NAME", ssidName);
  if (outdoorActive) {
    html.replace("EXT_TEMP", String(extData.temp, 1));
    html.replace("EXT_BAT",  String(extData.voltage, 2));
  } else {
    html.replace("EXT_TEMP", "--.-");
    html.replace("EXT_BAT",  "-.--");
  }
  server.send(200, "text/html", html);
}

void handleSetWifi() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    preferences.begin("clock-settings", false);
    preferences.putString("w-ssid", ssid);
    preferences.putString("w-pass", pass);
    preferences.end();
    server.send(200, "text/html", getSuccessHTML("📡", "Łączenie z: " + ssid));
    delay(500);
    WiFi.begin(ssid.c_str(), pass.c_str());
  }
}

void handleSetOffset() {
  if (server.hasArg("offset")) {
    tempOffset = server.arg("offset").toFloat();
    preferences.begin("clock-settings", false);
    preferences.putFloat("temp-off", tempOffset);
    preferences.end();
    server.send(200, "text/html", getSuccessHTML("🌡", "Korekta: " + String(tempOffset, 1) + " °C"));
  }
}

void handleSetTz() {
  if (server.hasArg("tz")) {
    timeOffsetHours = server.arg("tz").toInt();
    preferences.begin("clock-settings", false);
    preferences.putInt("tz-off", timeOffsetHours);
    preferences.end();
    server.send(200, "text/html", getSuccessHTML("🌍", "Strefa UTC: " + String(timeOffsetHours) + " h"));
  }
}

void handleSetTime() {
  if (server.hasArg("hour") && server.hasArg("minute")) {
    rtc.adjust(DateTime(2026, 1, 1, server.arg("hour").toInt(), server.arg("minute").toInt(), 0));
    server.send(200, "text/html", getSuccessHTML("⏰", "Czas ustawiony."));
  }
}

void handleSetInterval() {
  if (server.hasArg("interval")) {
    configInterval = server.arg("interval").toInt();
    preferences.begin("clock-settings", false);
    preferences.putInt("interval", configInterval);
    preferences.end();
    server.send(200, "text/html", getSuccessHTML("🌡", "Interwał: " + String(configInterval) + " min"));
  }
}

void handleSetNtpHour() {
  if (server.hasArg("ntphour")) {
    ntpSyncHour = server.arg("ntphour").toInt();
    preferences.begin("clock-settings", false);
    preferences.putInt("ntp-hour", ntpSyncHour);
    preferences.end();
    server.send(200, "text/html", getSuccessHTML("🕐", "Synchronizacja: " + String(ntpSyncHour) + ":00"));
  }
}

void handleSetNight() {
  if (server.hasArg("nStart") && server.hasArg("nEnd")) {
    nightStart = server.arg("nStart").toInt();
    nightEnd   = server.arg("nEnd").toInt();
    preferences.begin("clock-settings", false);
    preferences.putInt("n-start", nightStart);
    preferences.putInt("n-end",   nightEnd);
    preferences.end();
    server.send(200, "text/html", getSuccessHTML("🌙", "Noc: " + String(nightStart) + ":00 - " + String(nightEnd) + ":00"));
  }
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incoming, int len) {
  if (len == sizeof(SensorData)) {
    memcpy(&extData, incoming, sizeof(extData));
    lastRSSI = recv_info->rx_ctrl->rssi;
    lastSeenOutdoor = millis();
    outdoorActive = true;
    memcpy(pendingPeerAddr, recv_info->src_addr, 6);
    pendingSend = true;
  }
}

void sendIntervalToPeer() {
  if (!pendingSend) return;
  pendingSend = false;
  if (!esp_now_is_peer_exist(pendingPeerAddr)) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, pendingPeerAddr, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
  }
  int intervalToSend = configInterval;
  esp_now_send(pendingPeerAddr, (uint8_t*)&intervalToSend, sizeof(intervalToSend));
  //Serial.printf("Wysłano interwał %d min do czujnika\n", intervalToSend);
}

uint8_t flipByte(uint8_t b) {
  b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
  b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
  b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
  return b >> Y_OFFSET;
}

void drawChar(uint8_t index, uint8_t colAddr, uint8_t width, int yShift = 0) {
  for (int i = 0; i < width; i++) {
    int targetCol = colAddr + i;
    if (targetCol >= 0 && targetCol < 32) {
      uint8_t colData = pgm_read_byte(&charMap[index][i]);
      if (yShift > 0)      colData = colData << yShift;
      else if (yShift < 0) colData = colData >> abs(yShift);
      mx->setColumn(targetCol, flipByte(colData));
    }
  }
}

// Zamienia znak na indeks w charMap
int charToIdx(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c == '.') return 16;
  if (c == ' ') return 11;
  if (c == 'A') return 29;
  if (c == 'P') return 30;
  if (c == 'S') return 31;
  if (c == 'T') return 32;
  return 11;
}

// Wyświetlanie IP z prefiksem AP lub STA
// Krótka pauza (8 kroków) zanim tekst wjedzie z prawej, potem przewijanie, pauza na końcu
void displayScrollingIP() {
  bool isSTA = (WiFi.status() == WL_CONNECTED);
  String full = isSTA ? "STA " + WiFi.localIP().toString()
                       : "AP "  + WiFi.softAPIP().toString();
  int textWidth = full.length() * 4;

  if (millis() - lastIPScroll > 80) {
    mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
    mx->clear();

    if (ipScrollPos > 8) {
      int x = 31 - (ipScrollPos - 8);
      for (int i = 0; i < (int)full.length(); i++) {
        if (x < 32 && x > -4) drawChar(charToIdx(full[i]), x, 3);
        x += 4;
      }
    }

    mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
    ipScrollPos++;
    lastIPScroll = millis();

    if (ipScrollPos > textWidth + 40) {
      showIPMode  = false;
      ipScrollPos = 0;
      mTimer      = millis();
    }
  }
}

void displayTime() {
  DateTime now = rtc.now();
  drawChar(now.hour() / 10, 2, 3); drawChar(now.hour() % 10, 6, 3);
  drawChar((millis() % 1000 < 500 ? 10 : 11), 9, 3);
  drawChar(now.minute() / 10, 12, 3); drawChar(now.minute() % 10, 16, 3);
  drawChar(10, 19, 3); drawChar(now.second() / 10, 22, 3); drawChar(now.second() % 10, 26, 3);
}

void displayTemperature() {
  sensors_event_t h_e, t_e;
  aht.getEvent(&h_e, &t_e);
  int t = (int)(t_e.temperature + tempOffset);
  int h = (int)h_e.relative_humidity;
  drawChar(t / 10, 1, 3); drawChar(t % 10, 5, 3); drawChar(18, 9, 3); drawChar(14, 13, 3);
  drawChar(h / 10, 20, 3); drawChar(h % 10, 24, 3); drawChar(17, 28, 3);
}

void displayPressure() {
  int p = (int)(bmp.readPressure() / 100);
  drawChar((p / 1000) == 0 ? 11 : p / 1000, 1, 3);
  drawChar((p / 100) % 10, 5, 3); drawChar((p / 10) % 10, 9, 3); drawChar(p % 10, 13, 3);
  drawChar(15, 19, 3); drawChar(12, 23, 3); drawChar(13, 27, 3);
}

void displayOutdoor() {
  int batIdx  = (extData.voltage >= 3.9) ? 22 : (extData.voltage >= 3.6 ? 23 : 24);
  int rssiIdx = (lastRSSI > -65) ? 28 : (lastRSSI > -80 ? 27 : 26);
  int t = (int)extData.temp; int absT = abs(t);
  drawChar(batIdx, 1, 3, 0);
  drawChar(rssiIdx, 5, 3, 0);
  if (t < 0) drawChar(25, 11, 3, 0); else drawChar(11, 11, 3, 0);
  if (absT / 10 == 0) drawChar(11, 15, 3, 0); else drawChar(absT / 10, 15, 3, 0);
  drawChar(absT % 10, 19, 3, 0); drawChar(18, 23, 3, 0); drawChar(14, 27, 3, 0);
}

void syncRTCwithNTP() {
  if (WiFi.status() == WL_CONNECTED) {
    configTime(timeOffsetHours * 3600, 0, "pool.ntp.org", "time.google.com");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5000)) {
      rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
      //Serial.println("RTC zsynchronizowany z NTP");
    }
  }
}

void setup() {
  //Serial.begin(115200);
  Wire.begin(8, 9);
  pinMode(BTN_HOUR, INPUT_PULLUP);
  pinMode(BTN_MIN,  INPUT_PULLUP);

  P.begin();
  P.setIntensity(1);
  mx = P.getGraphicObject();

  rtc.begin();
  aht.begin();
  bmp.begin(0x77);

  preferences.begin("clock-settings", false);
  tempOffset      = preferences.getFloat("temp-off", 0.0);
  configInterval  = preferences.getInt("interval",   1);
  nightStart      = preferences.getInt("n-start",    22);
  nightEnd        = preferences.getInt("n-end",      6);
  ntpSyncHour     = preferences.getInt("ntp-hour",   3);
  timeOffsetHours = preferences.getInt("tz-off",     1);
  String sSsid    = preferences.getString("w-ssid",  "");
  String sPass    = preferences.getString("w-pass",  "");
  preferences.end();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP("ZEGAR_ESP32", "12345678");
  WiFi.setSleep(false);

  if (sSsid != "") {
    WiFi.begin(sSsid.c_str(), sPass.c_str());
    //Serial.printf("Łączę z WiFi: %s\n", sSsid.c_str());
    unsigned long wStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wStart < 10000) {
      delay(200);
    }
    if (WiFi.status() == WL_CONNECTED) {
      //Serial.printf("WiFi OK: %s\n", WiFi.localIP().toString().c_str());
      syncRTCwithNTP();
    }
    //else { Serial.println("WiFi: nie połączono, działam jako AP"); }
  }

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);
    //Serial.println("ESP-NOW OK");
  }
  //else { Serial.println("ESP-NOW FAIL"); }

  server.on("/",             handleRoot);
  server.on("/set_wifi",     HTTP_POST, handleSetWifi);
  server.on("/set_tz",       HTTP_POST, handleSetTz);
  server.on("/set_time",     HTTP_POST, handleSetTime);
  server.on("/set_offset",   HTTP_POST, handleSetOffset);
  server.on("/set_interval", HTTP_POST, handleSetInterval);
  server.on("/set_ntp_hour", HTTP_POST, handleSetNtpHour);
  server.on("/set_night",    HTTP_POST, handleSetNight);
  server.begin();

  //Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
}

void loop() {
  server.handleClient();
  sendIntervalToPeer();

  DateTime now = rtc.now();

  // Debug kanału co 5s – zakomentowany
  /*
  static unsigned long chTimer = 0;
  if (millis() - chTimer > 5000) {
    chTimer = millis();
    uint8_t primaryChan;
    wifi_second_chan_t secondChan;
    esp_wifi_get_channel(&primaryChan, &secondChan);
    Serial.printf("Kanał: %d | Outdoor: %s | WiFi: %s\n",
                  primaryChan,
                  outdoorActive ? "OK" : "BRAK",
                  WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "brak");
  }
  */

  if (millis() - mTimer > currentSate && !showIPMode) {
    mode = (mode + 1) % 4;
    if (millis() - lastSeenOutdoor > 600000UL) outdoorActive = false;
    if (mode == 3 && !outdoorActive) mode = 0;
    mTimer = millis();
    if      (mode == 0) currentSate = 5000;
    else if (mode == 1) currentSate = 3000;
    else if (mode == 2) currentSate = 2000;
    else if (mode == 3) currentSate = 2000;
  }

  // Synchronizacja NTP o ustawionej godzinie, raz na dobę
  static int lastNTPDay = -1;
  if (WiFi.status() == WL_CONNECTED) {
    DateTime now = rtc.now();
    if (now.hour() == ntpSyncHour && now.day() != lastNTPDay) {
      syncRTCwithNTP();
      lastNTPDay = now.day();
    }
  }

  if (showIPMode) {
    displayScrollingIP();
  } else {
    if (digitalRead(BTN_HOUR) == LOW && (millis() - lastDebounceTime > debounceDelay)) {
      rtc.adjust(DateTime(now.year(), now.month(), now.day(), (now.hour() + 1) % 24, now.minute(), now.second()));
      lastDebounceTime = millis();
    }
    if (digitalRead(BTN_MIN) == LOW && (millis() - lastDebounceTime > debounceDelay)) {
      rtc.adjust(DateTime(now.year(), now.month(), now.day(), now.hour(), (now.minute() + 1) % 60, 0));
      lastDebounceTime = millis();
    }
    if (digitalRead(BTN_HOUR) == LOW && digitalRead(BTN_MIN) == LOW) {
      showIPMode = true; ipScrollPos = 0;
    }

    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 150) {
      mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
      mx->clear();
      if      (mode == 0) displayTime();
      else if (mode == 1) displayTemperature();
      else if (mode == 2) displayPressure();
      else if (mode == 3 && outdoorActive) displayOutdoor();

      int ldr = analogRead(LDR_PIN);
      if (abs(ldr - lightVal) > 150) {
        P.setIntensity((ldr < 1400) ? 0 : (ldr < 3200 ? 5 : 12));
        lightVal = ldr;
      }
      mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
      lastUpdate = millis();
    }
  }
}


