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
#define WINDOW_SIZE 10

const char* mqtt_server = MQTT_SERVER;
const char* mqtt_user = MQTT_USER;
const char* mqtt_password = MQTT_PASSWORD;
const char* hostname = DEVICE_NAME;

WiFiClient espClient;
PubSubClient client(espClient);
ESP8266WebServer server(80);
NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);

float readings[WINDOW_SIZE];
int readIndex = 0;
float total = 0;
float average = 0;
unsigned long ota_progress_millis = 0;
unsigned long previousMillis = 0;
unsigned long mqttRetryMillis = 0;
const unsigned long mqttRetryInterval = 5000; // Retry every 5 seconds

const long interval = 1000;
bool firstRun = true;

const float tankHeight = 100.0;
const float tankLength = 200.0;
const float tankWidth = 100.0;
const float sensorOffset = 20.0; // Sensor is 20 cm above maximum water level

IPAddress staticIP(192, 168, 1, 98);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

void setup_wifi();
void reconnect();
float getWaterLevel(float distance);
int getVolume(float waterLevel);
void publishData(float distance, float waterLevel, int volume);
void handleRoot();
void handleData();
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

void setup() {
  Serial.begin(115200);
  Serial.println("[Setup] Initializing...");

  setup_wifi();

  client.setServer(mqtt_server, MQTT_PORT);
  client.setCallback([](char* topic, byte* payload, unsigned int length) {
    Serial.print("[MQTT] Message received on topic ");
    Serial.println(topic);
    // Add further handling if needed
  });

  server.begin();
  MDNS.begin(hostname);

  ElegantOTA.begin(&server);
  server.on("/", handleRoot);
  server.on("/data", handleData);

  for (int thisReading = 0; thisReading < WINDOW_SIZE; thisReading++) {
    readings[thisReading] = 0;
  }

  Serial.println("[Setup] Completed");
}

void loop() {
  // Always handle the web server and OTA
  ElegantOTA.loop();
  server.handleClient();

  // Attempt MQTT reconnect if disconnected
  reconnect();

  // Process MQTT messages if connected
  if (client.connected()) {
    client.loop();
  }

  // Periodically perform sensor tasks
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

float getWaterLevel(float distance) {
  return tankHeight - (distance - sensorOffset);
}

int getVolume(float waterLevel) {
  return (tankLength * tankWidth * waterLevel) / 1000; // Convert cubic cm to liters
}

void publishData(float distance, float waterLevel, int volume) {
  String distanceStr = String(distance);
  String waterLevelStr = String(waterLevel, 2);
  String volumeStr = String(volume);

  Serial.println("[MQTT] Publishing data...");
  client.publish("water_tank/distance/state", distanceStr.c_str());
  client.publish("water_tank/water_level/state", waterLevelStr.c_str());
  client.publish("water_tank/volume/state", volumeStr.c_str());
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
  html += "  fetch('/data')";
  html += "    .then(response => response.json())";
  html += "    .then(data => {";
  html += "      document.getElementById('distance').innerText = data.distance;";
  html += "      document.getElementById('waterLevel').innerText = data.waterLevel;";
  html += "      document.getElementById('volume').innerText = data.volume;";
  html += "      document.getElementById('wifiStrength').innerText = data.wifiStrength;";
  html += "      document.getElementById('mqttStatus').innerText = data.mqttStatus;";
  html += "      document.getElementById('ipAddress').innerText = data.ipAddress;";
  html += "    });";
  html += "}";
  html += "setInterval(fetchData, 1000);"; // Fetch data every second
  html += "</script>";
  html += "</head><body onload='fetchData()'>";
  html += "<h1>ESP8266 Water Tank Monitor</h1>";
  html += "<p>Distance: <span id='distance'>Loading...</span> cm</p>";
  html += "<p>Water Level: <span id='waterLevel'>Loading...</span> cm</p>";
  html += "<p>Volume: <span id='volume'>Loading...</span> liters</p>";
  html += "<p>WiFi Signal Strength: <span id='wifiStrength'>Loading...</span> dBm</p>";
  html += "<p>MQTT Status: <span id='mqttStatus'>Loading...</span></p>";
  html += "<p>IP Address: <span id='ipAddress'>Loading...</span></p>";
  html += "<p><a href='/update'>OTA Update</a></p>";
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
