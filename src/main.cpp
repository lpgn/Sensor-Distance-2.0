#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <ESP8266mDNS.h>
#include <NewPing.h>
#include <ElegantOTA.h>

// Ensure the credentials are properly linked
#ifndef WIFI_SSID
#define WIFI_SSID "default_ssid"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "default_password"
#endif

#define TRIGGER_PIN D1
#define ECHO_PIN D2
#define MAX_DISTANCE 400  // Maximum distance we want to ping for (in centimeters)
#define WINDOW_SIZE 10    // Window size for the moving average

const char* mqtt_server = "192.168.1.11";
const char* mqtt_user = "homeassistant";
const char* mqtt_password = "password";
const char* hostname = "water-tank";

WiFiClient espClient;
PubSubClient client(espClient);
ESP8266WebServer server(80);
NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);

float readings[WINDOW_SIZE]; // Array to store the readings
int readIndex = 0;           // The current reading index
float total = 0;             // The running total
float average = 0;           // The calculated average
unsigned long ota_progress_millis = 0; // Variable to track OTA progress
unsigned long previousMillis = 0;  // Variable to track time for millis()

const long interval = 1000;  // Interval for sensor readings
bool firstRun = true;        // Flag to check if it's the first run

// Tank dimensions in cm
const float tankHeight = 100.0;  // Tank height in cm
const float tankLength = 100.0;  // Tank length in cm
const float tankWidth = 200.0;   // Tank width in cm
const float sensorOffset = -10.0; // Sensor is 10cm away from the top of the tank

// Network configuration
IPAddress staticIP(192, 168, 1, 98); // Static IP
IPAddress gateway(192, 168, 1, 1);   // Gateway
IPAddress subnet(255, 255, 255, 0);  // Subnet mask

// Functions declaration
void setup_wifi();
void reconnect();
float getWaterLevel(float distance);
int getVolume(float waterLevel);
void publishData(float distance, float waterLevel, int volume);
void handleRoot();
void handleData();
void publishConfig();

void onOTAStart() {
  Serial.println("OTA update started!");
  // <Add your own code here>
}

void onOTAProgress(size_t current, size_t final) {
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
  }
}

void onOTAEnd(bool success) {
  if (success) {
    Serial.println("OTA update finished successfully!");
  } else {
    Serial.println("There was an error during the OTA update.");
  }
}

void setup() {
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback([](char* topic, byte* payload, unsigned int length) {
    // Handle MQTT messages here if needed
  });
  server.begin();
  MDNS.begin(hostname);

  // ElegantOTA setup
  ElegantOTA.begin(&server);
  server.on("/", handleRoot);
  server.on("/data", handleData); // Endpoint to serve sensor data

  // Initialize readings array
  for (int thisReading = 0; thisReading < WINDOW_SIZE; thisReading++) {
    readings[thisReading] = 0;
  }
  
  Serial.println("Setup completed");
}

void loop() {
  if (!client.connected()) {
    Serial.println("MQTT client not connected, attempting to reconnect...");
    reconnect();
  }
  client.loop();
  ElegantOTA.loop();
  server.handleClient();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    unsigned int distance = sonar.ping_cm();
    Serial.print("Distance measured: ");
    Serial.println(distance);

    float waterLevel = getWaterLevel(distance);
    int volume = getVolume(waterLevel);

    if (firstRun) {
      publishConfig();
      firstRun = false;
    }

    publishData(distance, waterLevel, volume);
  }
}

void setup_wifi() {
  delay(10);
  // Connect to WiFi network
  WiFi.config(staticIP, gateway, subnet);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(WIFI_SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP8266Client", mqtt_user, mqtt_password)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

float getWaterLevel(float distance) {
  // Calculate the water level from the distance measured
  return tankHeight - distance - sensorOffset;
}

int getVolume(float waterLevel) {
  // Calculate the volume based on the water level
  return (tankLength * tankWidth * waterLevel) / 1000; // Volume in liters, integer
}

void publishData(float distance, float waterLevel, int volume) {
  String distanceStr = String(distance);
  String waterLevelStr = String(waterLevel, 2);
  String volumeStr = String(volume);

  Serial.println("Publishing data to MQTT:");
  Serial.print("Distance: ");
  Serial.println(distanceStr);
  Serial.print("Water Level: ");
  Serial.println(waterLevelStr);
  Serial.print("Volume: ");
  Serial.println(volumeStr);

  client.publish("water_tank/distance/state", distanceStr.c_str());
  client.publish("water_tank/water_level/state", waterLevelStr.c_str());
  client.publish("water_tank/volume/state", volumeStr.c_str());
}

void publishConfig() {
  // Configuration for water distance sensor
  String config_distance = "{\"unique_id\": \"water_tank_distance\", \"device_class\": \"distance\", \"unit_of_measurement\": \"cm\", \"name\": \"Water Distance to Sensor\", \"state_topic\": \"water_tank/distance/state\"}";
  client.publish("homeassistant/sensor/water_tank/distance/config", config_distance.c_str(), true);

  // Configuration for water level sensor
  String config_level = "{\"unique_id\": \"water_tank_level\", \"device_class\": \"distance\", \"unit_of_measurement\": \"cm\", \"name\": \"Water Level\", \"state_topic\": \"water_tank/level/state\"}";
  client.publish("homeassistant/sensor/water_tank/level/config", config_level.c_str(), true);

  // Configuration for water volume sensor
  String config_volume = "{\"unique_id\": \"water_tank_volume\", \"device_class\": \"volume\", \"unit_of_measurement\": \"L\", \"name\": \"Volume dos Tanques\", \"state_topic\": \"water_tank/volume/state\"}";
  client.publish("homeassistant/sensor/water_tank/volume/config", config_volume.c_str(), true);
}

void handleRoot() {
  String html = "<html>\
                  <head>\
                    <title>ESP8266 Water Tank Monitor</title>\
                    <script>\
                      function fetchData() {\
                        fetch('/data')\
                          .then(response => response.json())\
                          .then(data => {\
                            document.getElementById('distance').innerText = data.distance;\
                            document.getElementById('waterLevel').innerText = data.waterLevel;\
                            document.getElementById('volume').innerText = data.volume;\
                          });\
                      }\
                      setInterval(fetchData, 5000);\
                    </script>\
                  </head>\
                  <body onload=\"fetchData()\">\
                    <h1>ESP8266 Water Tank Monitor</h1>\
                    <p>Distance: <span id=\"distance\">Loading...</span> cm</p>\
                    <p>Water Level: <span id=\"waterLevel\">Loading...</span> cm</p>\
                    <p>Volume: <span id=\"volume\">Loading...</span> liters</p>\
                    <p>Go to <a href=\"/update\">OTA Update</a> to upload new firmware.</p>\
                  </body>\
                </html>";
  server.send(200, "text/html", html);
}

void handleData() {
  unsigned int distance = sonar.ping_cm();
  float waterLevel = getWaterLevel(distance);
  int volume = getVolume(waterLevel);

  String json = "{";
  json += "\"distance\":" + String(distance) + ",";
  json += "\"waterLevel\":" + String(waterLevel, 2) + ",";
  json += "\"volume\":" + String(volume);
  json += "}";

  server.send(200, "application/json", json);
}
