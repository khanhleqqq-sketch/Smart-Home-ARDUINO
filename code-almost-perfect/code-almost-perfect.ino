 
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "time.h"
#include <ArduinoJson.h>
#include "FS.h"
#include "SPIFFS.h"

// ====== WiFi cấu hình tại đây ======
const char* ssid = "Cá nè";         // đổi thành tên WiFi của bạn
const char* password = "10072004";  // đổi thành mật khẩu WiFi

// ====== NTP (giờ Việt Nam) ======
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;  // múi giờ VN
const int daylightOffset_sec = 0;

// ====== Web server ======
AsyncWebServer server(80);

// ====== Nhóm A: 4 relay điều khiển bằng app ======
const int relayPinsA[4] = {16, 17, 18, 19};
bool relayAState[4] = {false, false, false, false};

// ====== Nhóm B: cảm biến + relay ======
const int pirPin = 34;  // cảm biến PIR
const int relayB = 13;  // relay nhóm B
bool relayBState = false;

unsigned long lastMotionTime = 0;
unsigned long lastWifiCheck = 0;

void setup() {
  Serial.begin(115200);

  // ==== SPIFFS ====// Viết dữ liệu lên flash
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS lỗi!");
    return;
  }
  // Ghi file
  File f = SPIFFS.open("/data.txt", FILE_WRITE);
  if (f) {
    f.println("Hello ESP32");
    f.close();
  }

  // Đọc file
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

  // ==== API demo ====
  server.on("/demo", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (request->hasParam("param1") && request->hasParam("param2")) {
      String param1 = request->getParam("param1")->value();
      String param2 = request->getParam("param2")->value();
      String result = param1 + param2;

      DynamicJsonDocument doc(128);
      doc["result"] = result;
      String json;
      serializeJson(doc, json);
      request->send(200, "application/json", json);
    } else {
      request->send(400, "application/json", "{\"error\":\"Missing parameters\"}");
    }
  });

  server.begin();
}

void loop() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    static unsigned long lastPrint = 0;
    unsigned long now = millis();
    if (now - lastPrint >= 1000) {
      lastPrint = now;
      Serial.println("Không lấy được thời gian");
    }
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
          delay(100);  // đọc cảm biến mỗi 100ms
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
  // Trước 23h: nhóm B chỉ điều khiển bằng app
}

