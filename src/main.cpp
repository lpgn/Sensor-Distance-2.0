#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <NewPing.h>

#ifndef WIFI_SSID
#define WIFI_SSID "default_ssid"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "default_password"
#endif

#define TRIGGER_PIN D1
#define ECHO_PIN D2
#define MAX_DISTANCE 400

const char* hostname = "water-tank";
ESP8266WebServer server(80);
NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);

unsigned long previousMillis = 0;
const long interval = 1000;

void setup_wifi();
void handleRoot();
void handleData();

void setup() {
  Serial.begin(115200);
  delay(2000); // Allow time for Serial Monitor to connect
  Serial.println("Starting setup...");

  setup_wifi();
  server.begin();
  server.on("/", handleRoot);
  server.on("/data", handleData);

  Serial.println("Setup complete. System ready.");
}

void loop() {
  server.handleClient();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    unsigned int distance = sonar.ping_cm();
    if (distance == 0) {
      Serial.println("Warning: Sensor is returning invalid or no readings.");
    } else {
      Serial.print("Raw sensor distance: ");
      Serial.print(distance);
      Serial.println(" cm");
    }
  }
}

void setup_wifi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void handleRoot() {
  String html = "<html>\
                  <head>\
                    <title>ESP8266 Sensor Monitor</title>\
                  </head>\
                  <body>\
                    <h1>ESP8266 Sensor Monitor</h1>\
                    <p>Distance data available at <a href='/data'>/data</a>.</p>\
                  </body>\
                </html>";
  server.send(200, "text/html", html);
}

void handleData() {
  unsigned int distance = sonar.ping_cm();
  String json = "{";
  json += "\"distance\":" + String(distance);
  json += "}";

  server.send(200, "application/json", json);
}
