#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include "Arduino.h"
#include "soc/soc.h"           // Disable brownout problems
#include "soc/rtc_cntl_reg.h"    // Disable brownout problems
#include "SD_MMC.h"            // SD Card library
#include "Base64.h"            // Base64 encoding library
#include "credentials.h"       // Include your WiFi credentials and API key

// Camera pins for ESP32-CAM
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// API settings
const char* host = "generativelanguage.googleapis.com";
const int httpsPort = 443;
const String PROMPT = "I want a short answer for what do you see in the image";

// Global variables for unique image filenames
int photoCounter = 1;
String currentPhotoFilePath = "";
String geminiReply = "";  // Variable to store Gemini's reply

// Flush the camera buffer by capturing and discarding one frame
void flushCameraBuffer() {
  camera_fb_t *dummy = esp_camera_fb_get();
  if (dummy) {
    esp_camera_fb_return(dummy);
    delay(50);  // Short delay to allow the camera to update
  }
}

// Initialize the camera with lower resolution for memory efficiency
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // Using CIF resolution (400x296) to save memory
  config.frame_size = FRAMESIZE_CIF;
  config.jpeg_quality = 30;
  config.fb_count = 1;
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }
  
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 1);
    s->set_contrast(s, 1);
    s->set_saturation(s, 0);
    s->set_special_effect(s, 0);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);
    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
  }
  
  return true;
}

// Capture an image and save it to SD card using the current unique filename
bool captureImage() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Capture failed");
    return false;
  }
  
  File file = SD_MMC.open(currentPhotoFilePath.c_str(), FILE_WRITE);
  if (!file) {
    Serial.println("File open failed");
    esp_camera_fb_return(fb);
    return false;
  }
  
  file.write(fb->buf, fb->len);
  file.close();
  esp_camera_fb_return(fb);
  
  return true;
}

// Analyze image with the Gemini API using the current image file
void analyzeImage() {
  WiFiClientSecure client;
  client.setInsecure();

  if (!SD_MMC.exists(currentPhotoFilePath.c_str())) {
    geminiReply = "Image file not found";
    return;
  }

  File imageFile = SD_MMC.open(currentPhotoFilePath.c_str());
  if (!imageFile) {
    geminiReply = "Failed to open image file";
    return;
  }
  
  size_t fileSize = imageFile.size();
  uint8_t *fileBuffer = (uint8_t *)malloc(fileSize);
  if (!fileBuffer) {
    geminiReply = "Memory allocation failed";
    imageFile.close();
    return;
  }
  
  imageFile.read(fileBuffer, fileSize);
  imageFile.close();
  
  String base64Image = base64::encode(fileBuffer, fileSize);
  free(fileBuffer);
  
  client.setTimeout(10000);
  if (!client.connect(host, httpsPort)) {
    geminiReply = "Connection failed";
    return;
  }
  
  {
    DynamicJsonDocument doc(40000);
    doc["contents"][0]["parts"][0]["text"] = PROMPT;
    doc["contents"][0]["parts"][1]["inline_data"]["mime_type"] = "image/jpeg";
    doc["contents"][0]["parts"][1]["inline_data"]["data"] = base64Image;
    doc["generationConfig"]["maxOutputTokens"] = 100;
    
    String payload;
    serializeJson(doc, payload);
    
    String url = "/v1beta/models/gemini-1.5-flash:generateContent?key=" + String(GEMINI_API_KEY);
    
    client.println("POST " + url + " HTTP/1.1");
    client.println("Host: " + String(host));
    client.println("Connection: close");
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(payload.length());
    client.println();
    client.println(payload);
  }
  
  // Delete the image file to free space
  SD_MMC.remove(currentPhotoFilePath.c_str());
  
  // Read response headers (timeout after 20 sec)
  bool headerEnd = false;
  unsigned long timeout = millis();
  while (!headerEnd && millis() - timeout < 20000) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") {
        headerEnd = true;
      }
    } else {
      delay(100);
    }
  }
  
  if (!headerEnd) {
    geminiReply = "Timeout waiting for API headers";
    client.stop();
    return;
  }
  
  // Read response body
  String response = "";
  timeout = millis();
  while (client.connected() && millis() - timeout < 30000) {
    if (client.available()) {
      response += (char)client.read();
      timeout = millis();
    } else {
      delay(100);
    }
  }
  
  if (response.length() == 0) {
    geminiReply = "Empty response";
    client.stop();
    return;
  }
  
  // Extract JSON from the response
  int jsonStart = response.indexOf('{');
  if (jsonStart >= 0) {
    response = response.substring(jsonStart);
    int braceCount = 1;
    int i = 1;
    while (braceCount > 0 && i < response.length()) {
      if (response.charAt(i) == '{') braceCount++;
      else if (response.charAt(i) == '}') braceCount--;
      i++;
    }
    if (braceCount == 0 && i < response.length()) {
      response = response.substring(0, i);
    }
  } else {
    geminiReply = "No JSON found";
    client.stop();
    return;
  }
  
  DynamicJsonDocument responseDoc(8192);
  DeserializationError error = deserializeJson(responseDoc, response);
  
  if (error) {
    geminiReply = "Failed to parse response: " + String(error.c_str());
  } else if (responseDoc.containsKey("error")) {
    geminiReply = "API Error: " + responseDoc["error"]["message"].as<String>();
  } else if (responseDoc.containsKey("candidates")) {
    geminiReply = responseDoc["candidates"][0]["content"]["parts"][0]["text"].as<String>();
  } else {
    geminiReply = "No valid response found";
  }
  
  client.stop();
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  Serial.begin(9600);
  delay(1000);
  Serial.println("ESP32-CAM Trash Detector");
  
  if (!SD_MMC.begin()) {
    Serial.println("SD Card init failed!");
    while (1) delay(1000);
  }
  
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  Serial.println("WiFi connected");
  
  if (!initCamera()) {
    Serial.println("Camera init failed!");
    while (1) delay(1000);
  }
  
  Serial.println("All systems ready");
}

void loop() {
  Serial.println("\nPress Enter to start detection");
  while (Serial.available() == 0) {
    delay(10);
  }
  while (Serial.available() > 0) {
    Serial.read();  // Clear serial buffer
  }
  
  // Flush the camera buffer so the next capture is fresh
  flushCameraBuffer();
  
  currentPhotoFilePath = "/photo" + String(photoCounter) + ".jpg";
  photoCounter++;
  
  Serial.println("--- Starting detection ---");
  if (captureImage()) {
    analyzeImage();
    Serial.println("Result: " + geminiReply);
  } else {
    Serial.println("Capture failed");
  }
}
