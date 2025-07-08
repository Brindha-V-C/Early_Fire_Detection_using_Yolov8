#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>

// WiFi credentials
const char* ssid = " ";
const char* password = " ";

// Firebase Realtime Database URL
const char* firebase_url = "firebase_cam_url";

// Cloud Run image upload endpoint
const char* upload_url = "cloud_upload_url";

// Camera config (AI Thinker module)
void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = 5;
  config.pin_d1       = 18;
  config.pin_d2       = 19;
  config.pin_d3       = 21;
  config.pin_d4       = 36;
  config.pin_d5       = 39;
  config.pin_d6       = 34;
  config.pin_d7       = 35;
  config.pin_xclk     = 0;
  config.pin_pclk     = 22;
  config.pin_vsync    = 25;
  config.pin_href     = 23;
  config.pin_sscb_sda = 26;
  config.pin_sscb_scl = 27;
  config.pin_pwdn     = 32;
  config.pin_reset    = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_CIF;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera init failed: 0x%x\n", err);
    return;
  }
}

WiFiServer server(80);

void publishIPToFirebase() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(firebase_url);
    http.addHeader("Content-Type", "application/json");

    String ipJson = "\"" + WiFi.localIP().toString() + "\"";
    int res = http.PUT(ipJson);
    Serial.println("🌐 Firebase IP upload status: " + String(res));
    http.end();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ ESP32-CAM connected. IP: " + WiFi.localIP().toString());

  publishIPToFirebase(); // Send IP to Firebase

  setupCamera();
  server.begin();
}

void loop() {
  WiFiClient client = server.available();
  if (!client) return;

  Serial.println("📡 Client connected");

  String request = "";
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      request += c;
      if (request.endsWith("\r\n\r\n")) break;
    }
  }

  Serial.println("➡️ HTTP Request:\n" + request);

  if (request.indexOf("GET /capture") >= 0) {
    Serial.println("📸 Capture requested");

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("❌ Capture failed");
      client.println("HTTP/1.1 500 Internal Server Error\r\n\r\nCamera capture failed");
      client.stop();
      return;
    }

    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(upload_url);
      http.addHeader("Content-Type", "image/jpeg");
      int res = http.POST(fb->buf, fb->len);
      Serial.println("📤 Upload response code: " + String(res));
      http.end();
    }

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain\r\n");
    client.println("Image captured and sent to server.");
    esp_camera_fb_return(fb);

  } else {
    client.println("HTTP/1.1 404 Not Found\r\n\r\n");
    Serial.println("❌ Invalid request received");
  }

  delay(10);
  client.stop();
  Serial.println("📴 Client disconnected");
}