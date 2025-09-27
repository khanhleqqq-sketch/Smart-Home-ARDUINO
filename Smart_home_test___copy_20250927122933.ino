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
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ====== WiFi cấu hình ======
const char* ssid = "FPT APTECH";
const char* password = "coderc++";

// ====== NTP ======
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;

// ====== Web server ======
AsyncWebServer server(80);

// ====== Relay control ======
const int relayPinsA[4] = {16, 17, 18, 19};
bool relayAState[4] = {false, false, false, false};

const int pirPin = 34;
const int relayB = 13;
bool relayBState = false;

unsigned long lastMotionTime = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastDHTRead = 0;

// ====== DHT11 data ======
float lastTemp = 0;
float lastHum = 0;

// ====== DATABASE CRUD ======
struct Device {
  int id;
  String name;
  String type;
  bool status;
  String location;
  String createdAt;
  String updatedAt;
};

Device devices[20];
int deviceCount = 0;
int nextId = 1;

// ====== Utility functions ======
String getCurrentTime() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeStr);
  }
  return "Unknown";
}

void saveDataToFile() {
  File file = SPIFFS.open("/devices.json", FILE_WRITE);
  if (file) {
    DynamicJsonDocument doc(2048);
    doc["nextId"] = nextId;
    doc["deviceCount"] = deviceCount;
    
    JsonArray devicesArray = doc.createNestedArray("devices");
    
    for (int i = 0; i < deviceCount; i++) {
      JsonObject device = devicesArray.createNestedObject();
      device["id"] = devices[i].id;
      device["name"] = devices[i].name;
      device["type"] = devices[i].type;
      device["status"] = devices[i].status;
      device["location"] = devices[i].location;
      device["createdAt"] = devices[i].createdAt;
      device["updatedAt"] = devices[i].updatedAt;
    }
    
    serializeJson(doc, file);
    file.close();
    Serial.println("✅ Database saved");
  }
}

void loadDataFromFile() {
  if (SPIFFS.exists("/devices.json")) {
    File file = SPIFFS.open("/devices.json", FILE_READ);
    if (file) {
      String content = file.readString();
      file.close();
      
      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, content);
      
      if (!error) {
        nextId = doc["nextId"];
        deviceCount = doc["deviceCount"];
        
        JsonArray devicesArray = doc["devices"];
        for (int i = 0; i < deviceCount; i++) {
          devices[i].id = devicesArray[i]["id"];
          devices[i].name = devicesArray[i]["name"].as<String>();
          devices[i].type = devicesArray[i]["type"].as<String>();
          devices[i].status = devicesArray[i]["status"];
          devices[i].location = devicesArray[i]["location"].as<String>();
          devices[i].createdAt = devicesArray[i]["createdAt"].as<String>();
          devices[i].updatedAt = devicesArray[i]["updatedAt"].as<String>();
        }
        Serial.println("✅ Database loaded: " + String(deviceCount) + " devices");
      }
    }
  }
}

int findDeviceById(int id) {
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].id == id) {
      return i;
    }
  }
  return -1;
}

// ====== API Routes ======
void setupDatabaseRoutes() {
  // CREATE Device
  server.on("/api/devices", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (deviceCount >= 20) {
      request->send(400, "application/json", "{\"error\":\"Database full\"}");
      return;
    }
    
    if (!request->hasParam("name") || !request->hasParam("type") || !request->hasParam("location")) {
      request->send(400, "application/json", "{\"error\":\"Missing parameters\"}");
      return;
    }
    
    String name = request->getParam("name")->value();
    String type = request->getParam("type")->value();
    String location = request->getParam("location")->value();
    
    devices[deviceCount].id = nextId++;
    devices[deviceCount].name = name;
    devices[deviceCount].type = type;
    devices[deviceCount].status = false;
    devices[deviceCount].location = location;
    devices[deviceCount].createdAt = getCurrentTime();
    devices[deviceCount].updatedAt = getCurrentTime();
    
    deviceCount++;
    saveDataToFile();
    
    DynamicJsonDocument doc(256);
    doc["success"] = "Device created";
    doc["id"] = devices[deviceCount-1].id;
    
    String json;
    serializeJson(doc, json);
    request->send(201, "application/json", json);
  });
  
  // READ All Devices
  server.on("/api/devices", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(2048);
    doc["success"] = true;
    doc["count"] = deviceCount;
    
    JsonArray devicesArray = doc.createNestedArray("devices");
    
    for (int i = 0; i < deviceCount; i++) {
      JsonObject device = devicesArray.createNestedObject();
      device["id"] = devices[i].id;
      device["name"] = devices[i].name;
      device["type"] = devices[i].type;
      device["status"] = devices[i].status;
      device["location"] = devices[i].location;
      device["createdAt"] = devices[i].createdAt;
      device["updatedAt"] = devices[i].updatedAt;
    }
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
  
  // READ Device by ID
  server.on("/api/devices/*", HTTP_GET, [](AsyncWebServerRequest* request) {
    String url = request->url();
    int id = url.substring(url.lastIndexOf('/') + 1).toInt();
    
    int index = findDeviceById(id);
    if (index == -1) {
      request->send(404, "application/json", "{\"error\":\"Device not found\"}");
      return;
    }
    
    DynamicJsonDocument doc(256);
    doc["success"] = true;
    doc["id"] = devices[index].id;
    doc["name"] = devices[index].name;
    doc["type"] = devices[index].type;
    doc["status"] = devices[index].status;
    doc["location"] = devices[index].location;
    doc["createdAt"] = devices[index].createdAt;
    doc["updatedAt"] = devices[index].updatedAt;
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
  
  // UPDATE Device
  server.on("/api/devices/*", HTTP_PUT, [](AsyncWebServerRequest* request) {
    String url = request->url();
    int id = url.substring(url.lastIndexOf('/') + 1).toInt();
    
    int index = findDeviceById(id);
    if (index == -1) {
      request->send(404, "application/json", "{\"error\":\"Device not found\"}");
      return;
    }
    
    bool hasChanges = false;
    
    if (request->hasParam("name")) {
      String newName = request->getParam("name")->value();
      if (newName.length() > 0) {
        devices[index].name = newName;
        hasChanges = true;
      }
    }
    
    if (request->hasParam("type")) {
      String newType = request->getParam("type")->value();
      if (newType.length() > 0) {
        devices[index].type = newType;
        hasChanges = true;
      }
    }
    
    if (request->hasParam("status")) {
      String statusStr = request->getParam("status")->value();
      bool newStatus = (statusStr == "true" || statusStr == "1");
      devices[index].status = newStatus;
      hasChanges = true;
    }
    
    if (request->hasParam("location")) {
      String newLocation = request->getParam("location")->value();
      if (newLocation.length() > 0) {
        devices[index].location = newLocation;
        hasChanges = true;
      }
    }
    
    if (hasChanges) {
      devices[index].updatedAt = getCurrentTime();
      saveDataToFile();
      
      DynamicJsonDocument doc(256);
      doc["success"] = "Device updated";
      doc["id"] = id;
      
      String json;
      serializeJson(doc, json);
      request->send(200, "application/json", json);
    } else {
      request->send(400, "application/json", "{\"error\":\"No changes\"}");
    }
  });
  
  // DELETE Device
  server.on("/api/devices/*", HTTP_DELETE, [](AsyncWebServerRequest* request) {
    String url = request->url();
    int id = url.substring(url.lastIndexOf('/') + 1).toInt();
    
    int index = findDeviceById(id);
    if (index == -1) {
      request->send(404, "application/json", "{\"error\":\"Device not found\"}");
      return;
    }
    
    for (int i = index; i < deviceCount - 1; i++) {
      devices[i] = devices[i + 1];
    }
    deviceCount--;
    
    saveDataToFile();
    
    DynamicJsonDocument doc(256);
    doc["success"] = "Device deleted";
    doc["id"] = id;
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
  
  // Statistics
  server.on("/api/stats", HTTP_GET, [](AsyncWebServerRequest* request) {
    int activeCount = 0;
    for (int i = 0; i < deviceCount; i++) {
      if (devices[i].status) activeCount++;
    }
    
    DynamicJsonDocument doc(256);
    doc["success"] = true;
    doc["totalDevices"] = deviceCount;
    doc["activeDevices"] = activeCount;
    doc["inactiveDevices"] = deviceCount - activeCount;
    doc["maxDevices"] = 20;
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
}

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
  
  // Load database
  loadDataFromFile();

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
        digitalWrite(relayPinsA[id], relayAState[id] ? HIGH : LOW); // ✅ SỬA LẠI
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

  // ==== Setup Database Routes ====
  setupDatabaseRoutes();

  server.begin();
  Serial.println("🌍 Server started with Database CRUD + LCD + DHT11");
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