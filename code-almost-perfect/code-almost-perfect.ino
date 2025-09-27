#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "time.h"
#include <ArduinoJson.h>
#include "FS.h"
#include "SPIFFS.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

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

  // ==== SPIFFS ====
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS lỗi!");
    return;
  }

  File f = SPIFFS.open("/data.txt", FILE_WRITE);
  if (f) {
    f.println("Hello ESP32");
    f.close();
  }

  f = SPIFFS.open("/data.txt");
  if (f) {
    while (f.available()) {
      Serial.write(f.read());
    }
    f.close();
  }

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

  // ==== API điều khiển relay ====
  server.on("/relay", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(256);

    if (request->hasParam("id") && request->hasParam("state")) {
      String idStr = request->getParam("id")->value();
      String stateStr = request->getParam("state")->value();

      JsonArray errors = doc.createNestedArray("errors");

      if (idStr.length() != 1 || !isDigit(idStr[0])) {
        errors.add("Invalid ID: must be a digit (0-9)");
      }
      if (stateStr.length() != 1 || !isDigit(stateStr[0])) {
        errors.add("Invalid state: must be between 0 and 1");
      }

      if (errors.size() > 0) {
        String json;
        serializeJson(doc, json);
        request->send(400, "application/json", json);
        return;
      }

      int id = idStr.toInt();
      int state = stateStr.toInt();

      if (state != 0 && state != 1) {
        doc["error"] = "Invalid state: must be 0 or 1";
        doc["code"] = 422;
        String json;
        serializeJson(doc, json);
        request->send(422, "application/json", json);
        return;
      }

      if (id >= 0 && id < 4) {  // Nhóm A
        relayAState[id] = state;
        digitalWrite(relayPinsA[id], relayAState[id] ? LOW : HIGH);
        doc["success"] = "Relay A updated";
        doc["relay"] = id;
        doc["state"] = state;
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
        return;
      }

      if (id == 5) {  // Nhóm B
        relayBState = state;
        digitalWrite(relayB, relayBState ? HIGH : LOW);
        doc["success"] = "Relay B updated";
        doc["relay"] = id;
        doc["state"] = state;
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
        return;
      }

      doc["error"] = "Relay ID not found";
      String json;
      serializeJson(doc, json);
      request->send(404, "application/json", json);
      return;
    }

    doc["error"] = "Missing parameters: require id & state";
    String json;
    serializeJson(doc, json);
    request->send(400, "application/json", json);
  });

  // ==== API đọc DHT11 ====
  server.on("/dht", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(128);
    doc["temperature"] = lastTemp;
    doc["humidity"] = lastHum;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

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
