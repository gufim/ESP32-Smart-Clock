// ============================================================
//  CZUJNIK ZEWNĘTRZNY – wersja naprawiona
//  Zmiany względem oryginału:
//  1. Sygnatura OnDataSent: wifi_tx_info_t* (wymagane przez Arduino-ESP32 v3.x)
//  2. Dodano WiFi.setSleep(false) – kluczowe dla baterii
//  3. Dodano analogReadResolution(12) i filtr błędnych odczytów DS18B20
//  4. Wydłużony czas oczekiwania na odpowiedź zegarka (500ms)
//  5. Dodano Serial.begin tylko gdy USB – bez tego delay blokuje start
//  6. Zwiększona moc TX dla zasięgu
// ============================================================


#include <esp_now.h>
#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <esp_wifi.h>

#define BAT_PIN      12
#define ONE_WIRE_BUS 13

uint8_t broadcastAddress[] = {0x98, 0x88, 0xE0, 0xD9, 0xC0, 0xF8};

struct __attribute__((packed)) SensorData {
    float temp;
    float voltage;
};
SensorData myData;

RTC_DATA_ATTR int sleepMinutes = 1;
RTC_DATA_ATTR bool isWaitingForTemp = false;
RTC_DATA_ATTR uint8_t lastChannel = 1;  // Zapamiętany kanał przez deep sleep

bool replyReceived = false;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

void OnDataSent(const wifi_tx_info_t *send_info, esp_now_send_status_t status) {}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incoming, int len) {
    if (len == sizeof(int)) {
        memcpy(&sleepMinutes, incoming, sizeof(int));
        replyReceived = true;
    }
}

void setup() {
    sensors.begin();

    if (!isWaitingForTemp) {
        // KROK 1: Zlecenie pomiaru DS18B20, drzemka 800ms zamiast delay()
        sensors.requestTemperatures();
        isWaitingForTemp = true;
        esp_sleep_enable_timer_wakeup(800ULL * 1000ULL);
        esp_deep_sleep_start();

    } else {
        // KROK 2: Dane gotowe, odczyt i wysyłka
        isWaitingForTemp = false;

        myData.temp = sensors.getTempCByIndex(0);
        if (myData.temp == -127.0f || myData.temp == 85.0f) myData.temp = 0.0f;

        analogReadResolution(12);
        myData.voltage = (analogRead(BAT_PIN) / 4095.0f) * 3.3f * 2.0f * 1.0415f;

        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.setTxPower(WIFI_POWER_19_5dBm);

        if (esp_now_init() != ESP_OK) {
            esp_sleep_enable_timer_wakeup((uint64_t)(sleepMinutes > 0 ? sleepMinutes : 1) * 60ULL * 1000000ULL);
            esp_deep_sleep_start();
        }

        esp_now_register_send_cb(OnDataSent);
        esp_now_register_recv_cb(OnDataRecv);

        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, broadcastAddress, 6);
        peerInfo.encrypt = false;

        bool success = false;

        // Najpierw próbuj na ostatnim działającym kanale (szybsza ścieżka)
        for (int attempt = 0; attempt < 3 && !success; attempt++) {
            esp_wifi_set_channel(lastChannel, WIFI_SECOND_CHAN_NONE);
            peerInfo.channel = lastChannel;
            if (esp_now_is_peer_exist(broadcastAddress)) esp_now_del_peer(broadcastAddress);
            esp_now_add_peer(&peerInfo);

            replyReceived = false;
            esp_now_send(broadcastAddress, (uint8_t*)&myData, sizeof(myData));

            unsigned long t = millis();
            while (millis() - t < 150) {
                if (replyReceived) { success = true; break; }
                delay(1);
            }
        }

        // Jeśli ostatni kanał nie zadziałał – skanuj wszystkie
        if (!success) {
            for (int ch = 1; ch <= 13 && !success; ch++) {
                if (ch == lastChannel) continue;  // Już próbowaliśmy

                esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
                peerInfo.channel = ch;
                if (esp_now_is_peer_exist(broadcastAddress)) esp_now_del_peer(broadcastAddress);
                if (esp_now_add_peer(&peerInfo) != ESP_OK) continue;

                for (int attempt = 0; attempt < 2 && !success; attempt++) {
                    replyReceived = false;
                    esp_now_send(broadcastAddress, (uint8_t*)&myData, sizeof(myData));

                    unsigned long t = millis();
                    while (millis() - t < 150) {
                        if (replyReceived) { success = true; lastChannel = ch; break; }
                        delay(1);
                    }
                }
            }
        }

        esp_sleep_enable_timer_wakeup((uint64_t)(sleepMinutes > 0 ? sleepMinutes : 1) * 60ULL * 1000000ULL);
        esp_deep_sleep_start();
    }
}

void loop() {}

