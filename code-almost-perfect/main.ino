#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "time.h"
#include <ArduinoJson.h>
#include "FS.h"
#include "SPIFFS.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include "../Util/Database_Util/Database_Util.ino"
#include "../Util/Restfull_util/API_Handler.ino"

// ====== LCD I2C ======
LiquidCrystal_I2C lcd(0x27, 16, 2);  

// ====== Cảm biến DHT11 ======
#define DHTPIN 4      // chân DHT11
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ====== WiFi cấu hình tại đây ======
const char* ssid = "FPT APTECH";
const char* password = "coderc++";

// ====== NTP (giờ Việt Nam) ======
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;

// ====== Web server ======
AsyncWebServer server(80);

// ====== Nhóm A: 4 relay điều khiển bằng app ======
const int relayPinsA[4] = {16, 17, 18, 19};
bool relayAState[4] = {false, false, false, false};

// ====== Nhóm B: cảm biến + relay ======
const int pirPin = 34;
const int relayB = 13;
bool relayBState = false;

unsigned long lastMotionTime = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastDHTRead = 0;

// ====== Biến lưu DHT11 ======
float lastTemp = 0;
float lastHum = 0;

void setup() {
  Serial.begin(115200);

  // ==== LCD + DHT11 ====
  Wire.begin(2, 5); // SDA=2, SCL=5
  lcd.init();
  lcd.backlight();
  dht.begin();

  lcd.setCursor(0, 0);
  lcd.print("ESP32 SmartHome");
  lcd.setCursor(0, 1);
  lcd.print("Dang khoi dong");
  delay(2000);
  lcd.clear();

  // ==== Initialize Database ====
  if (!initDatabase()) {
    Serial.println("Database initialization failed!");
    return;
  }

  // ==== Create default data files ====
  initDefaultData();

  // ==== Relay nhóm A ====
  for (int i = 0; i < 4; i++) {
    pinMode(relayPinsA[i], OUTPUT);
    digitalWrite(relayPinsA[i], LOW);
  }

  // ==== Nhóm B ====
  pinMode(pirPin, INPUT);
  pinMode(relayB, OUTPUT);
  digitalWrite(relayB, LOW);

  // ==== WiFi ====
  WiFi.begin(ssid, password);
  Serial.print("Đang kết nối WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - lastWifiCheck >= 500) {
      Serial.print(".");
      lastWifiCheck = now;
    }
  }
  Serial.println("\nĐã kết nối WiFi");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // ==== NTP ====
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // ==== Setup all API endpoints ====
  setupAllAPIs(server, lastTemp, lastHum);
  setupDatabaseAPI(server);

  server.begin();
}

void loop() {
  // ==== Đọc DHT11 mỗi 2 giây ====
  unsigned long now = millis();
  if (now - lastDHTRead >= 2000) {
    lastDHTRead = now;
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (!isnan(h) && !isnan(t)) {
      lastTemp = t;
      lastHum = h;

      Serial.printf("Nhiet do: %.1f*C - Do am: %.1f%%\n", t, h);

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Nhiet do: ");
      lcd.print(t);
      lcd.print((char)223);
      lcd.print("C");

      lcd.setCursor(0, 1);
      lcd.print("Do am: ");
      lcd.print(h);
      lcd.print("%");
    } else {
      Serial.println("❌ Loi doc DHT11!");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Loi DHT11!");
    }
  }

  // ==== Xử lý giờ + cảm biến PIR ====
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Không lấy được thời gian");
    delay(1000);
    return;
  }

  int hour = timeinfo.tm_hour;

  // Sau 23h: bật chế độ cảm biến
  if (hour >= 23) {
    int pirValue = digitalRead(pirPin);

    if (pirValue == HIGH) {
      lastMotionTime = millis();
      digitalWrite(relayB, HIGH);
      relayBState = true;
    }

    if (relayBState) {
      if (millis() - lastMotionTime > 5UL * 60UL * 1000UL) {  // hết 5 phút
        unsigned long checkStart = millis();
        bool stillMotion = false;

        while (millis() - checkStart < 60UL * 1000UL) {  // kiểm tra thêm 1 phút
          if (digitalRead(pirPin) == HIGH) {
            stillMotion = true;
            break;
          }
          delay(100);
        }

        if (stillMotion) {
          lastMotionTime = millis();
        } else {
          digitalWrite(relayB, LOW);
          relayBState = false;
        }
      }
    }
  }
}
