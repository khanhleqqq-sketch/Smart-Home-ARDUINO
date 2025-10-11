#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "time.h"
#include <ArduinoJson.h>
#include "FS.h"
#include "SPIFFS.h"

// ====== Database Utility Functions ======

// Initialize database (SPIFFS)
bool initDatabase() {
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
    return false;
  }
  Serial.println("Database (SPIFFS) initialized");
  return true;
}

// Initialize default database files
void initDefaultData() {
  // Create default "house" data if it doesn't exist
  if (!dataExists("house")) {
    DynamicJsonDocument houseDoc(512);
    houseDoc["name"] = "My Smart Home";
    houseDoc["address"] = "123 Main Street";
    houseDoc["rooms"] = 4;

    JsonArray devices = houseDoc.createNestedArray("devices");
    JsonObject relay1 = devices.createNestedObject();
    relay1["id"] = 0;
    relay1["name"] = "Living Room Light";
    relay1["type"] = "relay";

    JsonObject relay2 = devices.createNestedObject();
    relay2["id"] = 1;
    relay2["name"] = "Bedroom Light";
    relay2["type"] = "relay";

    JsonObject relay3 = devices.createNestedObject();
    relay3["id"] = 2;
    relay3["name"] = "Kitchen Light";
    relay3["type"] = "relay";

    JsonObject relay4 = devices.createNestedObject();
    relay4["id"] = 3;
    relay4["name"] = "Bathroom Light";
    relay4["type"] = "relay";

    JsonObject motionSensor = devices.createNestedObject();
    motionSensor["id"] = 5;
    motionSensor["name"] = "Motion Sensor Light";
    motionSensor["type"] = "pir_relay";

    if (saveData("house", houseDoc.as<JsonVariant>())) {
      Serial.println("Default 'house' data created");
    }
  }

  // Create default "user_info" data if it doesn't exist
  if (!dataExists("user_info")) {
    DynamicJsonDocument userDoc(512);

    JsonArray users = userDoc.createNestedArray("users");

    JsonObject user1 = users.createNestedObject();
    user1["id"] = 1;
    user1["name"] = "Admin";
    user1["email"] = "admin@smarthome.com";
    user1["role"] = "admin";
    user1["created_at"] = "2025-01-01";

    JsonObject user2 = users.createNestedObject();
    user2["id"] = 2;
    user2["name"] = "User";
    user2["email"] = "user@smarthome.com";
    user2["role"] = "user";
    user2["created_at"] = "2025-01-01";

    if (saveData("user_info", userDoc.as<JsonVariant>())) {
      Serial.println("Default 'user_info' data created");
    }
  }

  Serial.println("Default data initialization complete");
}

// Save data to database (supports any JSON type: object, array, string, number, boolean)
bool saveData(const String& key, JsonVariant data) {
  String filepath = "/db_" + key + ".json";

  File file = SPIFFS.open(filepath, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing: " + filepath);
    return false;
  }

  // Serialize JSON data to file
  size_t bytesWritten = serializeJson(data, file);
  file.close();

  if (bytesWritten == 0) {
    Serial.println("Failed to write data to: " + filepath);
    return false;
  }

  Serial.println("Data saved to: " + filepath + " (" + String(bytesWritten) + " bytes)");
  return true;
}

// Load data from database
bool loadData(const String& key, JsonDocument& doc) {
  String filepath = "/db_" + key + ".json";

  if (!SPIFFS.exists(filepath)) {
    Serial.println("File does not exist: " + filepath);
    return false;
  }

  File file = SPIFFS.open(filepath, FILE_READ);
  if (!file) {
    Serial.println("Failed to open file for reading: " + filepath);
    return false;
  }

  // Deserialize JSON data from file
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.println("Failed to parse JSON from: " + filepath);
    Serial.println("Error: " + String(error.c_str()));
    return false;
  }

  Serial.println("Data loaded from: " + filepath);
  return true;
}

// Delete data from database
bool deleteData(const String& key) {
  String filepath = "/db_" + key + ".json";

  if (!SPIFFS.exists(filepath)) {
    Serial.println("File does not exist: " + filepath);
    return false;
  }

  if (SPIFFS.remove(filepath)) {
    Serial.println("Data deleted: " + filepath);
    return true;
  } else {
    Serial.println("Failed to delete: " + filepath);
    return false;
  }
}

// Check if key exists in database
bool dataExists(const String& key) {
  String filepath = "/db_" + key + ".json";
  return SPIFFS.exists(filepath);
}

// List all keys in database
void listAllKeys() {
  File root = SPIFFS.open("/");
  File file = root.openNextFile();

  Serial.println("=== Database Keys ===");
  while (file) {
    String filename = String(file.name());
    if (filename.startsWith("/db_") && filename.endsWith(".json")) {
      String key = filename.substring(4, filename.length() - 5);
      Serial.println("- " + key + " (" + String(file.size()) + " bytes)");
    }
    file = root.openNextFile();
  }
  Serial.println("====================");
}

// Get database info (total size, free space, etc.)
void getDatabaseInfo() {
  size_t totalBytes = SPIFFS.totalBytes();
  size_t usedBytes = SPIFFS.usedBytes();
  size_t freeBytes = totalBytes - usedBytes;

  Serial.println("=== Database Info ===");
  Serial.println("Total: " + String(totalBytes) + " bytes");
  Serial.println("Used: " + String(usedBytes) + " bytes");
  Serial.println("Free: " + String(freeBytes) + " bytes");
  Serial.println("====================");
}

// ====== Database API Endpoints ======

void setupDatabaseAPI(AsyncWebServer& server) {

  // POST /db/save - Save data
  server.on("/db/save", HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    NULL,
    [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
      DynamicJsonDocument responseDoc(256);

      if (!request->hasParam("key", true)) {
        responseDoc["error"] = "Missing 'key' parameter";
        String json;
        serializeJson(responseDoc, json);
        request->send(400, "application/json", json);
        return;
      }

      String key = request->getParam("key", true)->value();

      // Parse incoming JSON data
      DynamicJsonDocument inputDoc(4096);
      DeserializationError error = deserializeJson(inputDoc, data, len);

      if (error) {
        responseDoc["error"] = "Invalid JSON data";
        responseDoc["details"] = error.c_str();
        String json;
        serializeJson(responseDoc, json);
        request->send(400, "application/json", json);
        return;
      }

      // Save data
      if (saveData(key, inputDoc.as<JsonVariant>())) {
        responseDoc["success"] = true;
        responseDoc["message"] = "Data saved successfully";
        responseDoc["key"] = key;
        String json;
        serializeJson(responseDoc, json);
        request->send(200, "application/json", json);
      } else {
        responseDoc["error"] = "Failed to save data";
        String json;
        serializeJson(responseDoc, json);
        request->send(500, "application/json", json);
      }
    }
  );

  // GET /db/load?key=<key> - Load data
  server.on("/db/load", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument responseDoc(256);

    if (!request->hasParam("key")) {
      responseDoc["error"] = "Missing 'key' parameter";
      String json;
      serializeJson(responseDoc, json);
      request->send(400, "application/json", json);
      return;
    }

    String key = request->getParam("key")->value();
    DynamicJsonDocument dataDoc(4096);

    if (loadData(key, dataDoc)) {
      String json;
      serializeJson(dataDoc, json);
      request->send(200, "application/json", json);
    } else {
      responseDoc["error"] = "Failed to load data";
      responseDoc["key"] = key;
      String json;
      serializeJson(responseDoc, json);
      request->send(404, "application/json", json);
    }
  });

  // DELETE /db/delete?key=<key> - Delete data
  server.on("/db/delete", HTTP_DELETE, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument responseDoc(256);

    if (!request->hasParam("key")) {
      responseDoc["error"] = "Missing 'key' parameter";
      String json;
      serializeJson(responseDoc, json);
      request->send(400, "application/json", json);
      return;
    }

    String key = request->getParam("key")->value();

    if (deleteData(key)) {
      responseDoc["success"] = true;
      responseDoc["message"] = "Data deleted successfully";
      responseDoc["key"] = key;
      String json;
      serializeJson(responseDoc, json);
      request->send(200, "application/json", json);
    } else {
      responseDoc["error"] = "Failed to delete data";
      responseDoc["key"] = key;
      String json;
      serializeJson(responseDoc, json);
      request->send(404, "application/json", json);
    }
  });

  // GET /db/exists?key=<key> - Check if key exists
  server.on("/db/exists", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument responseDoc(128);

    if (!request->hasParam("key")) {
      responseDoc["error"] = "Missing 'key' parameter";
      String json;
      serializeJson(responseDoc, json);
      request->send(400, "application/json", json);
      return;
    }

    String key = request->getParam("key")->value();
    responseDoc["key"] = key;
    responseDoc["exists"] = dataExists(key);

    String json;
    serializeJson(responseDoc, json);
    request->send(200, "application/json", json);
  });

  // GET /db/list - List all keys
  server.on("/db/list", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument responseDoc(2048);
    JsonArray keys = responseDoc.createNestedArray("keys");

    File root = SPIFFS.open("/");
    File file = root.openNextFile();

    while (file) {
      String filename = String(file.name());
      if (filename.startsWith("/db_") && filename.endsWith(".json")) {
        String key = filename.substring(4, filename.length() - 5);
        JsonObject keyObj = keys.createNestedObject();
        keyObj["key"] = key;
        keyObj["size"] = file.size();
      }
      file = root.openNextFile();
    }

    String json;
    serializeJson(responseDoc, json);
    request->send(200, "application/json", json);
  });

  // GET /db/info - Get database info
  server.on("/db/info", HTTP_GET, [](AsyncWebServerRequest* request) {
    DynamicJsonDocument responseDoc(256);

    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    size_t freeBytes = totalBytes - usedBytes;

    responseDoc["total_bytes"] = totalBytes;
    responseDoc["used_bytes"] = usedBytes;
    responseDoc["free_bytes"] = freeBytes;
    responseDoc["usage_percent"] = (usedBytes * 100) / totalBytes;

    String json;
    serializeJson(responseDoc, json);
    request->send(200, "application/json", json);
  });
}
