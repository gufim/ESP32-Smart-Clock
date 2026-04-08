
#include <esp_now.h>
#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define BAT_PIN 0
#define ONE_WIRE_BUS 1

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // TWÓJ MAC

struct __attribute__((packed)) SensorData {
    float temp;
    float voltage;
};
SensorData myData;

// Pamięć RTC - przetrwa Deep Sleep
RTC_DATA_ATTR int sleepMinutes = 1;
RTC_DATA_ATTR bool isWaitingForTemp = false;
bool replyReceived = false;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

void OnDataSent(const esp_now_send_info_t *send_info, esp_now_send_status_t status) {}
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incoming, int len) {
    if (len == sizeof(int)) {
        memcpy(&sleepMinutes, incoming, sizeof(int));
        replyReceived = true;
    }
}

void setup() {
    // Nie potrzebujemy Serial ani długich delayów przy pracy bateryjnej
    sensors.begin();

    if (!isWaitingForTemp) {
        // --- KROK 1: ROZPOCZĘCIE POMIARU ---
        sensors.requestTemperatures();
        isWaitingForTemp = true;

        // Zasypiamy na 800ms, aby DS18B20 dokończył pomiar
        // Pobór prądu spadnie z ~80mA do ~0.01mA
        esp_sleep_enable_timer_wakeup(800 * 1000);
        esp_deep_sleep_start();
    } else {
        // --- KROK 2: ODCZYT I WYSYŁKA ---
        isWaitingForTemp = false; // Reset na kolejny cykl
        myData.temp = sensors.getTempCByIndex(0);

        // Odczyt napięcia baterii
        int rawADC = analogRead(BAT_PIN);
        myData.voltage = (rawADC / 4095.0) * 3.03 * 2.0;

        // Dopiero teraz włączamy WiFi i ustawiamy moc (największy pobór prądu)
        WiFi.mode(WIFI_STA);
        WiFi.setTxPower((wifi_power_t)72); // Twoje 18 dBm

        if (esp_now_init() == ESP_OK) {
            esp_now_register_send_cb(OnDataSent);
            esp_now_register_recv_cb(OnDataRecv);

            esp_now_peer_info_t peerInfo = {};
            memcpy(peerInfo.peer_addr, broadcastAddress, 6);
            esp_now_add_peer(&peerInfo);

            esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));

            // Krótkie czekanie na odpowiedź z zegara (nowy interwał snu)
            unsigned long waitStart = millis();
            while (millis() - waitStart < 400) { // Skrócone do 400ms
                if (replyReceived) break;
                delay(1);
            }
        }

        // Głęboki sen na czas wyznaczony przez zegar
        esp_sleep_enable_timer_wakeup(sleepMinutes * 60 * 1000000ULL);
        esp_deep_sleep_start();
    }
}

void loop() {
    // Puste - procesor śpi lub wykonuje setup
}
