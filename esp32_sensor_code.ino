#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "DHT.h"

// Wi-Fi credentials
const char* ssid = "YOUR_NETWORK_SSID";
const char* password = "YOUR_NETWORK_PASSWORD";

// Google Geolocation API key
const char* API_KEY = "YOUR_API_KEY";
const char* GEOLOCATION_URL = "GEOLOCATION_URL";

// Cloud Run endpoint
const char* serverName = "CLOUD_SERVER_URL";

// DHT Sensor settings
#define DHTPIN 14
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Gas Sensor
const int gasSensorPin = 34;

// Global variables to store location
String latitude = "";
String longitude = "";

void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi!");
    
    getLocation();
}
void loop() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int gasLevel = analogRead(gasSensorPin);

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Failed to read from DHT sensor!");
    delay(2000);
    return;
  }


  Serial.println("Temp: " + String(temperature));
  Serial.println("Humidity: " + String(humidity));
  Serial.println("Gas: " + String(gasLevel));
  Serial.println("Lat: " + latitude);
  Serial.println("Lon: " + longitude);

  StaticJsonDocument<256> doc;
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["gas_level"] = gasLevel;
  doc["latitude"] = latitude;
  doc["longitude"] = longitude;

  String jsonData;
  serializeJson(doc, jsonData);

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST(jsonData);
    if (httpResponseCode > 0) {
      Serial.println("Response code: " + String(httpResponseCode));
      Serial.println(http.getString());
    } else {
      Serial.println("Error sending POST: " + String(httpResponseCode));
    }
    http.end();
  } else {
    Serial.println("WiFi disconnected.");
  }

  delay(60000);
}

void getLocation() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String fullUrl = String(GEOLOCATION_URL) + String(API_KEY);

        http.begin(fullUrl);
        http.addHeader("Content-Type", "application/json");

        String requestBody = "{\"considerIp\": \"true\"}";
        int httpResponseCode = http.POST(requestBody);

        if (httpResponseCode > 0) {
            String response = http.getString();
            Serial.println("Received response:");
            Serial.println(response);

            DynamicJsonDocument doc(1024);
            deserializeJson(doc, response);

            latitude = String(doc["location"]["lat"].as<float>(), 6);
            longitude = String(doc["location"]["lng"].as<float>(), 6);

            Serial.print("Latitude: ");
            Serial.println(latitude);
            Serial.print("Longitude: ");
            Serial.println(longitude);
        } else {
            Serial.print("Error on HTTP request: ");
            Serial.println(httpResponseCode);
        }

        http.end();
    } else {
        Serial.println("WiFi not connected!");
    }
}