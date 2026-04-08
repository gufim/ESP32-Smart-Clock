
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

// --- KONFIGURACJA SPRZĘTU ---
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

// ZAKTUALIZOWANA TABLICA ZNAKÓW (Szerokość 3)
const uint8_t PROGMEM charMap[][5] = {
  {0x1f, 0x11, 0x1f, 0, 0}, {0x00, 0x1f, 0x00, 0, 0}, {0x1d, 0x15, 0x17, 0, 0}, // 0, 1, 2
  {0x15, 0x15, 0x1f, 0, 0}, {0x07, 0x04, 0x1f, 0, 0}, {0x17, 0x15, 0x1d, 0, 0}, // 3, 4, 5
  {0x1f, 0x15, 0x1d, 0, 0}, {0x01, 0x01, 0x1f, 0, 0}, {0x1f, 0x15, 0x1f, 0, 0}, // 6, 7, 8
  {0x17, 0x15, 0x1f, 0, 0}, {0x00, 0x0a, 0x00, 0, 0}, {0x00, 0x00, 0x00, 0, 0}, // 9, 10, 11
  {0x1f, 0x05, 0x07, 0, 0}, {0x1f, 0x05, 0x1f, 0, 0}, {0x0e, 0x11, 0x11, 0, 0}, // 12, 13, 14
  {0x1f, 0x04, 0x1c, 0, 0}, {0x00, 0x10, 0x00, 0, 0}, {0x12, 0x04, 0x09, 0, 0}, // 15, 16, 17
  {0x06, 0x09, 0x06, 0, 0}, {0x00, 0x0e, 0x0a, 0x0e, 0}, {0x00, 0x0e, 0x08, 0x0e, 0}, // 18, 19, 20
  {0x00, 0x02, 0x0e, 0x02, 0}, // 21

// 22: Bateria Pełna (Boki wys. 4, środek wys. 5)
{0x1E, 0x1F, 0x1E, 0, 0},

// 23: Bateria Średnia (Boki wys. 4, środek z dziurą + cypelek)
{0x1E, 0x1B, 0x1E, 0, 0},

// 24: Bateria Pusta (Tylko ramka wys. 4 + cypelek)
{0x1E, 0x11, 0x1E, 0, 0},

// 25: Minus (-)
{0x00, 0x08, 0x08, 0, 0},

// ANTENA (26-28, szerokość 3)
{0x10, 0x00, 0x00, 0, 0}, // 26: 1 kropka
{0x10, 0x18, 0x00, 0, 0}, // 27: 2 kropki
{0x10, 0x18, 0x1C, 0, 0}, // 28: 3 kropki

};

uint8_t flipByte(uint8_t b) {
  b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
  b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
  b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
  return b >> Y_OFFSET;
}

void drawChar(uint8_t index, uint8_t colAddr, uint8_t width, int yShift = 0) {
  for (int i = 0; i < width; i++) {
    uint8_t colData = pgm_read_byte(&charMap[index][i]);
    if(yShift > 0) colData = colData << yShift;
    else if(yShift < 0) colData = colData >> abs(yShift);
    mx->setColumn(colAddr + i, flipByte(colData));
  }
}

bool checkIsNight() {
  DateTime now = rtc.now();
  int h = now.hour();
  if (nightStart < nightEnd) return (h >= nightStart && h < nightEnd);
  else return (h >= nightStart || h < nightEnd);
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incoming, int len) {
  if (len == sizeof(SensorData)) {
    memcpy(&extData, incoming, sizeof(extData));
    lastRSSI = recv_info->rx_ctrl->rssi;
    lastSeenOutdoor = millis();
    outdoorActive = true;
    int intervalToSend = checkIsNight() ? configInterval * 2 : configInterval;
    const uint8_t* mac = recv_info->src_addr;
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, mac, 6);
    if (!esp_now_is_peer_exist(mac)) esp_now_add_peer(&peerInfo);
    esp_now_send(mac, (uint8_t *) &intervalToSend, sizeof(intervalToSend));
  }
}

const char MAIN_page[] PROGMEM = R"=====(
<!DOCTYPE html><html><head><meta charset="UTF-8"><title>Zegarek Config</title>
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<style>
  body { font-family: Arial; text-align: center; margin-top: 20px; background: #f4f4f4; }
  .container { width: 320px; margin: 0 auto; background: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
  h2, h3 { color: #333; margin-bottom: 5px; }
  .section { border-top: 2px solid #eee; margin-top: 15px; padding-top: 10px; }
  .data-label { font-size: 16px; color: #666; display: block; }
  .data-temp { font-size: 30px; font-weight: bold; color: #d32f2f; margin-bottom: 10px; }
  .data-bat { font-size: 26px; font-weight: bold; color: #6f42c1; }
  .data-rssi { font-size: 18px; color: #28a745; font-weight: bold; }
  input[type="number"] { width: 80px; height: 40px; font-size: 20px; text-align: center; border: 2px solid #ccc; border-radius: 5px; }
  input[type="submit"] { background: #28a745; color: white; padding: 12px; width: 100%; font-size: 18px; margin-top: 8px; border: none; border-radius: 4px; cursor: pointer; }
  .btn-blue { background: #007bff !important; }
  .btn-orange { background: #ff9800 !important; }
  .btn-purple { background: #6f42c1 !important; }
  .status-icon { font-size: 24px; vertical-align: middle; color: #FFD700; text-shadow: 1px 1px 2px #000; }
</style></head><body><div class="container">
  <h2>Ustawienia Zegara</h2>
  <form action="/set_time" method="POST">
    <input type="number" name="hour" min="0" max="23" placeholder="GG" required> :
    <input type="number" name="minute" min="0" max="59" placeholder="MM" required>
    <input type="submit" value="Ustaw Czas">
  </form>
  <div class="section">
    <h3>Kalibracja Wewnętrzna</h3>
    <p>Aktualna: <b>OFFSET_VAL &deg;C</b></p>
    <form action="/set_offset" method="POST">
      <input type="number" name="offset" step="0.1" value="OFFSET_VAL" required style="width:110px;">
      <input type="submit" value="Zapisz Korektę" class="btn-blue">
    </form>
  </div>
  <div class="section">
    <h3>Czujnik Zewnętrzny</h3>
    <span class="data-label">Temperatura</span>
    <div class="data-temp">EXT_TEMP &deg;C</div>
    <span class="data-label">Bateria</span>
    <div class="data-bat">EXT_BAT V</div>
    <span class="data-label">Sygnał RSSI</span>
    <div class="data-rssi">EXT_RSSI dBm</div>
    <p>Interwał: <b>INT_VAL min</b></p>
    <form action="/set_interval" method="POST">
      <input type="number" name="interval" min="1" max="60" value="INT_VAL" required>
      <input type="submit" value="Zapisz Interwał" class="btn-orange">
    </form>
  </div>
  <div class="section">
    <h3>Harmonogram Nocny (x2)</h3>
    <p>Interwał x2 w godzinach: MODE_ICON</p>
    <form action="/set_night" method="POST">
      <input type="number" name="nStart" min="0" max="23" value="N_START_VAL"> do
      <input type="number" name="nEnd" min="0" max="23" value="N_END_VAL">
      <input type="submit" value="Zapisz Godziny" class="btn-purple">
    </form>
  </div>
</div></body></html>
)=====";

void handleRoot() {
  String html = String(MAIN_page);
  html.replace("OFFSET_VAL", String(tempOffset, 1));
  html.replace("INT_VAL", String(configInterval));
  html.replace("N_START_VAL", String(nightStart));
  html.replace("N_END_VAL", String(nightEnd));
  html.replace("MODE_ICON", checkIsNight() ? "<span class='status-icon'>🌙</span>" : "<span class='status-icon'>☀️</span>");
  if(outdoorActive) {
    html.replace("EXT_TEMP", String(extData.temp, 1));
    html.replace("EXT_BAT", String(extData.voltage, 2));
    html.replace("EXT_RSSI", String(lastRSSI));
  } else {
    html.replace("EXT_TEMP", "--.-"); html.replace("EXT_BAT", "-.--"); html.replace("EXT_RSSI", "---");
  }
  server.send(200, "text/html", html);
}

void handleSetTime() {
  if (server.hasArg("hour") && server.hasArg("minute")) {
    int h = server.arg("hour").toInt(); int m = server.arg("minute").toInt();
    rtc.adjust(DateTime(2025, 1, 21, h, m, 0));
    server.send(200, "text/html", "OK");
  }
}
void handleSetOffset() {
  if (server.hasArg("offset")) {
    tempOffset = server.arg("offset").toFloat();
    preferences.begin("clock-settings", false); preferences.putFloat("temp-off", tempOffset); preferences.end();
    server.send(200, "text/html", "Zapisano");
  }
}
void handleSetInterval() {
  if (server.hasArg("interval")) {
    configInterval = server.arg("interval").toInt();
    preferences.begin("clock-settings", false); preferences.putInt("interval", configInterval); preferences.end();
    server.send(200, "text/html", "Zapisano");
  }
}
void handleSetNight() {
  if (server.hasArg("nStart") && server.hasArg("nEnd")) {
    nightStart = server.arg("nStart").toInt(); nightEnd = server.arg("nEnd").toInt();
    preferences.begin("clock-settings", false); preferences.putInt("n-start", nightStart); preferences.putInt("n-end", nightEnd); preferences.end();
    server.send(200, "text/html", "Zapisano");
  }
}

void displayTime() {
    DateTime now = rtc.now();
    drawChar(now.hour()/10, 2, 3); drawChar(now.hour()%10, 6, 3);
    drawChar((millis()%1000 < 500 ? 10 : 11), 9, 3);
    drawChar(now.minute()/10, 12, 3); drawChar(now.minute()%10, 16, 3);
    drawChar(10, 19, 3); drawChar(now.second()/10, 22, 3); drawChar(now.second()%10, 26, 3);
}

void displayTemperature() {
    sensors_event_t h_e, t_e; aht.getEvent(&h_e, &t_e);
    int t = (int)(t_e.temperature + tempOffset); int h = (int)h_e.relative_humidity;
    drawChar(t/10, 1, 3); drawChar(t%10, 5, 3); drawChar(18, 9, 3); drawChar(14, 13, 3);
    drawChar(h/10, 20, 3); drawChar(h%10, 24, 3); drawChar(17, 28, 3);
}

void displayPressure() {
    int p = (int)(bmp.readPressure() / 100);
    if((p/1000) == 0 ) drawChar(11, 1, 3); else drawChar(p/1000, 1, 3);
    drawChar((p/100)%10, 5, 3); drawChar((p/10)%10, 9, 3); drawChar(p%10, 13, 3);
    drawChar(15, 19, 3); drawChar(12, 23, 3); drawChar(13, 27, 3);
}

void displayOutdoor() {
    // 1. BATERIA: Poz. 1, szer. 3, przesunięcie 1 w dół (-1)
    int batIdx = (extData.voltage >= 3.9) ? 22 : (extData.voltage >= 3.5 ? 23 : 24);
    drawChar(batIdx, 1, 3, 0);

    // 2. ANTENA: Poz. 5, szer. 3
    int rssiIdx;
    if (lastRSSI > -65) rssiIdx = 28;
    else if (lastRSSI > -80) rssiIdx = 27;
    else rssiIdx = 26;
    drawChar(rssiIdx, 5, 3, 0);

    // 3. TEMPERATURA: Start od poz. 11 (aby był odstęp)
    int t = (int)extData.temp; int absT = abs(t);
    if (t < 0) drawChar(25, 11, 3, 0); else drawChar(11, 11, 3, 0);
    if (absT / 10 == 0) drawChar(11, 15, 3, 0); else drawChar(absT / 10, 15, 3, 0);
    drawChar(absT % 10, 19, 3, 0);
    drawChar(18, 23, 3, 0);
    drawChar(14, 27, 3, 0);
}

void setup() {
  Wire.begin(8, 9); pinMode(BTN_HOUR, INPUT_PULLUP); pinMode(BTN_MIN, INPUT_PULLUP);
  P.begin(); P.setIntensity(1); mx = P.getGraphicObject(); mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  rtc.begin(); aht.begin(); bmp.begin(0x77);
  WiFi.mode(WIFI_AP_STA); if (esp_now_init() == ESP_OK) esp_now_register_recv_cb(OnDataRecv);

  preferences.begin("clock-settings", false);
  tempOffset = preferences.getFloat("temp-off", 0.0); configInterval = preferences.getInt("interval", 1);
  nightStart = preferences.getInt("n-start", 22); nightEnd = preferences.getInt("n-end", 6);
  preferences.end();

  WiFi.softAP("ZEGAR_ESP32", "12345678");
  server.on("/", handleRoot); server.on("/set_time", HTTP_POST, handleSetTime);
  server.on("/set_offset", HTTP_POST, handleSetOffset);
  server.on("/set_interval", HTTP_POST, handleSetInterval);
  server.on("/set_night", HTTP_POST, handleSetNight);
  server.begin();
}

void loop() {
  server.handleClient();
  if (digitalRead(BTN_HOUR) == LOW && (millis() - lastDebounceTime > debounceDelay)) {
    DateTime now = rtc.now(); rtc.adjust(DateTime(now.year(), now.month(), now.day(), (now.hour() + 1) % 24, now.minute(), now.second()));
    lastDebounceTime = millis();
  }
  if (digitalRead(BTN_MIN) == LOW && (millis() - lastDebounceTime > debounceDelay)) {
    DateTime now = rtc.now(); rtc.adjust(DateTime(now.year(), now.month(), now.day(), now.hour(), (now.minute() + 1) % 60, 0));
    lastDebounceTime = millis();
  }

  if (millis() - mTimer > currentSate){
    mode = (mode + 1) % 4;
    int multiplier = checkIsNight() ? 2 : 1;
    unsigned long timeout = (unsigned long)((configInterval * multiplier) + 2) * 60000;
    if (outdoorActive && (millis() - lastSeenOutdoor) > timeout) outdoorActive = false;
    if (mode == 3 && !outdoorActive) mode = 0;
    mTimer = millis();
    mx->clear();   // Czyszczenie LCD
    if (mode == 0) currentSate = 5000;
    else if (mode == 1) currentSate = 3000;
    else if (mode == 2) currentSate = 2000;
    else if (mode == 3) currentSate = 2000;

  }

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 200) {
    if (mode == 0) displayTime();
    else if (mode == 1) displayTemperature();
    else if (mode == 2) displayPressure();
    else if (mode == 3 && outdoorActive) displayOutdoor();

    int ldr = analogRead(LDR_PIN);
    if (abs(ldr - lightVal) > 200) {
      int intens = (ldr < 1000) ? 1 : (ldr < 3200 ? 5 : 10);
      P.setIntensity(intens); lightVal = ldr;
    }
    mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
    lastUpdate = millis();
  }
}
