#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <ESP8266mDNS.h>
#include <NewPing.h>
#include <ElegantOTA.h>

#define TRIGGER_PIN D1
#define ECHO_PIN D2
#define MAX_DISTANCE 400

char mqtt_server[40] = MQTT_SERVER;
char mqtt_user[40] = MQTT_USER;
char mqtt_password[40] = MQTT_PASSWORD;
float tankHeight = 100.0;
float tankLength = 200.0;
float tankWidth = 100.0;
float sensorOffset = 20.0;

const char* hostname = DEVICE_NAME;
WiFiClient espClient;
PubSubClient client(espClient);
ESP8266WebServer server(80);
NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);

unsigned long ota_progress_millis = 0;
unsigned long previousMillis = 0;
unsigned long mqttRetryMillis = 0;
const unsigned long mqttRetryInterval = 5000; // Retry every 5 seconds
const long interval = 1000;
bool firstRun = true;

IPAddress staticIP(192, 168, 1, 98);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

bool sendDistance = true;
bool sendWaterLevel = true;
bool sendVolume = true;

void setup_wifi();
void reconnect();
float getWaterLevel(float distance);
int getVolume(float waterLevel);
void publishData(float distance, float waterLevel, int volume);
void handleRoot();
void handleData();
void saveSettings();
void handleToggleOption();
void publishConfig();

void onOTAStart() {
  Serial.println("[OTA] Update started");
}

void onOTAProgress(size_t current, size_t final) {
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("[OTA] Progress: %u / %u bytes\n", current, final);
  }
}

void onOTAEnd(bool success) {
  if (success) {
    Serial.println("[OTA] Update finished successfully");
  } else {
    Serial.println("[OTA] Update failed");
  }
}

float getWaterLevel(float distance) {
  if (distance < sensorOffset) {
    distance = sensorOffset;
  }
  if (distance > tankHeight + sensorOffset) {
    distance = tankHeight + sensorOffset;
  }

  float waterLevel = tankHeight - (distance - sensorOffset);
  return waterLevel > 0 ? waterLevel : 0;
}

int getVolume(float waterLevel) {
  int volume = (tankLength * tankWidth * waterLevel) / 1000;
  return volume > 2000 ? 2000 : (volume < 0 ? 0 : volume);
}

void setup() {
  Serial.begin(115200);
  Serial.println("[Setup] Initializing...");

  setup_wifi();

  client.setServer(mqtt_server, MQTT_PORT);
  client.setCallback([](char* topic, byte* payload, unsigned int length) {
    Serial.print("[MQTT] Message received on topic ");
    Serial.println(topic);
  });

  server.begin();
  MDNS.begin(hostname);

  ElegantOTA.begin(&server);
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/save_settings", HTTP_POST, saveSettings);
  server.on("/toggle_option", HTTP_POST, handleToggleOption);

  Serial.println("[Setup] Completed");
}

void loop() {
  ElegantOTA.loop();
  server.handleClient();
  reconnect();

  if (client.connected()) {
    client.loop();
  }

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    unsigned int distance = sonar.ping_cm();
    Serial.printf("[Sensor] Distance: %u cm\n", distance);

    float waterLevel = getWaterLevel(distance);
    Serial.printf("[Sensor] Water Level: %.2f cm\n", waterLevel);

    int volume = getVolume(waterLevel);
    Serial.printf("[Sensor] Volume: %d liters\n", volume);

    if (firstRun) {
      Serial.println("[MQTT] Publishing config");
      publishConfig();
      firstRun = false;
    }

    if (client.connected()) {
      publishData(distance, waterLevel, volume);
    }
  }
}

void setup_wifi() {
  delay(10);
  WiFi.config(staticIP, gateway, subnet);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n[WiFi] Connected");
  Serial.print("[WiFi] IP: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  if (millis() - mqttRetryMillis > mqttRetryInterval) {
    mqttRetryMillis = millis();
    if (!client.connected()) {
      Serial.print("[MQTT] Connecting...");
      if (client.connect("ESP8266Client", mqtt_user, mqtt_password)) {
        Serial.println("Connected");
      } else {
        Serial.printf("[MQTT] Failed, rc=%d. Will retry...\n", client.state());
      }
    }
  }
}

void publishData(float distance, float waterLevel, int volume) {
  if (sendDistance) {
    String distanceStr = String(distance);
    client.publish("water_tank/distance/state", distanceStr.c_str());
  }

  if (sendWaterLevel) {
    String waterLevelStr = String(waterLevel, 2);
    client.publish("water_tank/water_level/state", waterLevelStr.c_str());
  }

  if (sendVolume) {
    String volumeStr = String(volume);
    client.publish("water_tank/volume/state", volumeStr.c_str());
  }

  Serial.println("[MQTT] Data published");
}

void publishConfig() {
  String config_distance = "{\"unique_id\": \"water_tank_distance\", \"device_class\": \"distance\", \"unit_of_measurement\": \"cm\", \"name\": \"Water Distance\", \"state_topic\": \"water_tank/distance/state\"}";
  String config_level = "{\"unique_id\": \"water_tank_level\", \"device_class\": \"distance\", \"unit_of_measurement\": \"cm\", \"name\": \"Water Level\", \"state_topic\": \"water_tank/level/state\"}";
  String config_volume = "{\"unique_id\": \"water_tank_volume\", \"device_class\": \"volume\", \"unit_of_measurement\": \"L\", \"name\": \"Tank Volume\", \"state_topic\": \"water_tank/volume/state\"}";

  client.publish("homeassistant/sensor/water_tank/distance/config", config_distance.c_str(), true);
  client.publish("homeassistant/sensor/water_tank/level/config", config_level.c_str(), true);
  client.publish("homeassistant/sensor/water_tank/volume/config", config_volume.c_str(), true);
}

void handleRoot() {
  String html = "<html><head><title>ESP8266 Water Tank Monitor</title>";
  html += "<script>";
  html += "function fetchData() {";
  html += "  fetch('/data').then(response => response.json()).then(data => {";
  html += "    document.getElementById('distance').innerText = data.distance + ' cm';";
  html += "    document.getElementById('waterLevel').innerText = data.waterLevel + ' cm';";
  html += "    document.getElementById('volume').innerText = data.volume + ' liters';";
  html += "    document.getElementById('wifiStrength').innerText = data.wifiStrength + ' dBm';";
  html += "    document.getElementById('mqttStatus').innerText = data.mqttStatus;";
  html += "    document.getElementById('ipAddress').innerText = data.ipAddress;";
  html += "  });";
  html += "}";
  html += "setInterval(fetchData, 1000);"; // Refresh data every second
  html += "</script>";
  html += "</head><body onload='fetchData()'>";
  html += "<h1>ESP8266 Water Tank Monitor</h1>";
  html += "<p>Distance: <span id='distance'>Loading...</span></p>";
  html += "<p>Water Level: <span id='waterLevel'>Loading...</span></p>";
  html += "<p>Volume: <span id='volume'>Loading...</span></p>";
  html += "<p>WiFi Signal Strength: <span id='wifiStrength'>Loading...</span></p>";
  html += "<p>MQTT Status: <span id='mqttStatus'>Loading...</span></p>";
  html += "<p>IP Address: <span id='ipAddress'>Loading...</span></p>";
  html += "<h2>Settings</h2>";
  html += "<form method='POST' action='/save_settings'>";
  html += "<label>MQTT Server: </label><input name='mqtt_server' value='" + String(mqtt_server) + "'><br>";
  html += "<label>MQTT User: </label><input name='mqtt_user' value='" + String(mqtt_user) + "'><br>";
  html += "<label>MQTT Password: </label><input name='mqtt_password' type='password' value='" + String(mqtt_password) + "'><br>";
  html += "<label>Tank Height (cm): </label><input name='tank_height' value='" + String(tankHeight) + "'><br>";
  html += "<label>Tank Length (cm): </label><input name='tank_length' value='" + String(tankLength) + "'><br>";
  html += "<label>Tank Width (cm): </label><input name='tank_width' value='" + String(tankWidth) + "'><br>";
  html += "<label>Sensor Offset (cm): </label><input name='sensor_offset' value='" + String(sensorOffset) + "'><br>";
  html += "<h3>MQTT Data Options</h3>";
  html += "<input type='checkbox' id='sendDistance' onchange='toggleOption(this)' checked> Send Distance<br>";
  html += "<input type='checkbox' id='sendWaterLevel' onchange='toggleOption(this)' checked> Send Water Level<br>";
  html += "<input type='checkbox' id='sendVolume' onchange='toggleOption(this)' checked> Send Volume<br>";
  html += "<button type='submit'>Save</button>";
  html += "<h2>ElegantOTA</h2>";
  html += "<p>Upload new firmware using <a href='/update'>ElegantOTA</a></p>";
  html += "</form>";
  html += "<script>";
  html += "function toggleOption(checkbox) {";
  html += "  fetch('/toggle_option', { method: 'POST', body: JSON.stringify({ option: checkbox.id, value: checkbox.checked }) });";
  html += "}";
  html += "</script>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleData() {
  unsigned int distance = sonar.ping_cm();
  float waterLevel = getWaterLevel(distance);
  int volume = getVolume(waterLevel);
  int32_t wifiStrength = WiFi.RSSI();
  String mqttStatus = client.connected() ? "Connected" : "Disconnected";
  String ipAddress = WiFi.localIP().toString();

  String json = "{";
  json += "\"distance\":" + String(distance) + ",";
  json += "\"waterLevel\":" + String(waterLevel, 2) + ",";
  json += "\"volume\":" + String(volume) + ",";
  json += "\"wifiStrength\":" + String(wifiStrength) + ",";
  json += "\"mqttStatus\":\"" + mqttStatus + "\",";
  json += "\"ipAddress\":\"" + ipAddress + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void saveSettings() {
  if (server.hasArg("mqtt_server")) {
    server.arg("mqtt_server").toCharArray(mqtt_server, 40);
  }
  if (server.hasArg("mqtt_user")) {
    server.arg("mqtt_user").toCharArray(mqtt_user, 40);
  }
  if (server.hasArg("mqtt_password")) {
    server.arg("mqtt_password").toCharArray(mqtt_password, 40);
  }
  if (server.hasArg("tank_height")) {
    tankHeight = server.arg("tank_height").toFloat();
  }
  if (server.hasArg("tank_length")) {
    tankLength = server.arg("tank_length").toFloat();
  }
  if (server.hasArg("tank_width")) {
    tankWidth = server.arg("tank_width").toFloat();
  }
  if (server.hasArg("sensor_offset")) {
    sensorOffset = server.arg("sensor_offset").toFloat();
  }

  server.send(200, "text/html", "<html><body><h1>Settings Saved</h1><a href='/'>Back to Home</a></body></html>");
}

void handleToggleOption() {
  if (server.hasArg("plain")) {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    String option = doc["option"].as<String>();
    bool value = doc["value"].as<bool>();

    if (option == "sendDistance") {
      sendDistance = value;
    } else if (option == "sendWaterLevel") {
      sendWaterLevel = value;
    } else if (option == "sendVolume") {
      sendVolume = value;
    }
  }

  server.send(200, "application/json", "{\"status\":\"success\"}");
}
