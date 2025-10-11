#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// ==== API điều khiển relay ====
void setupRelayAPI(AsyncWebServer& server) {
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
        digitalWrite(relayPinsA[id], relayAState[id] ? HIGH : LOW);
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
}

// ==== API đọc DHT11 ====
void setupDHTAPI(AsyncWebServer& server, float& lastTemp, float& lastHum) {
  server.on("/dht", HTTP_GET, [&lastTemp, &lastHum](AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(128);
    doc["temperature"] = lastTemp;
    doc["humidity"] = lastHum;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
}

// ==== API demo ====
void setupDemoAPI(AsyncWebServer& server) {
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
}

// ==== Initialize all APIs (version without DHT) ====
void setupAllAPIs(AsyncWebServer& server) {
  setupRelayAPI(server);
  setupDemoAPI(server);
}

// ==== Initialize all APIs (version with DHT) ====
void setupAllAPIs(AsyncWebServer& server, float& lastTemp, float& lastHum) {
  setupRelayAPI(server);
  setupDHTAPI(server, lastTemp, lastHum);
}
