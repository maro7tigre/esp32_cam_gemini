#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include "Arduino.h"
#include "soc/soc.h"           // Disable brownout problems
#include "soc/rtc_cntl_reg.h"  // Disable brownout problems
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
const String PROMPT = "I want a short answer for which trash type do you see in the image [cardboard, glass, metal, paper, plastic or other]";

// Global variables for unique image filenames
int photoCounter = 1;
String currentPhotoFilePath = "";
String geminiReply = "";  // Variable to store Gemini's reply
String base64Image = "";  // Variable to store the base64 encoded image

// Create a web server on port 80
WebServer server(80);

// Buffer to hold the current image for sharing
uint8_t* lastImageBuffer = NULL;
size_t lastImageSize = 0;
bool newImageAvailable = false;

// Default camera resolution (can be changed via serial input)
framesize_t currentResolution = FRAMESIZE_SVGA; // Default resolution

// Resolution mapping based on numeric input (1-8)
framesize_t resolutionMap[] = {
  FRAMESIZE_QQVGA,   // 1: 160x120
  FRAMESIZE_QVGA,    // 2: 320x240
  FRAMESIZE_CIF,     // 3: 400x296
  FRAMESIZE_VGA,     // 4: 640x480
  FRAMESIZE_SVGA,    // 5: 800x600
  FRAMESIZE_XGA,     // 6: 1024x768
  FRAMESIZE_SXGA,    // 7: 1280x1024
  FRAMESIZE_UXGA     // 8: 1600x1200
};

// Resolution names for diagnostic output
const char* resolutionNames[] = {
  "QQVGA (160x120)",
  "QVGA (320x240)",
  "CIF (400x296)",
  "VGA (640x480)",
  "SVGA (800x600)",
  "XGA (1024x768)",
  "SXGA (1280x1024)",
  "UXGA (1600x1200)"
};

// Flush the camera buffer by capturing and discarding one frame
void flushCameraBuffer() {
  camera_fb_t *dummy = esp_camera_fb_get();
  if (dummy) {
    esp_camera_fb_return(dummy);
    delay(50);  // Short delay to allow the camera to update
  }
}

// Initialize the camera with specified resolution
bool initCamera(framesize_t resolution = FRAMESIZE_SVGA) {
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
  

    /* 
   * AVAILABLE RESOLUTION OPTIONS (from lowest to highest):
   * ------------------------------------------------------
   * FRAMESIZE_QQVGA    160x120     Lowest resolution, minimal memory usage
   * FRAMESIZE_QVGA     320x240     Quarter VGA, good for previews, low memory
   * FRAMESIZE_CIF      400x296     Common Intermediate Format
   * FRAMESIZE_VGA      640x480     Standard VGA, good balance of quality/memory
   * FRAMESIZE_SVGA     800x600     Super VGA, better quality
   * FRAMESIZE_XGA      1024x768    XGA, high quality
   * FRAMESIZE_SXGA     1280x1024   Super XGA, very high quality
   * FRAMESIZE_UXGA     1600x1200   Ultra XGA, maximum resolution for ESP32-CAM
   *
   * QUALITY SETTINGS:
   * ----------------
   * jpeg_quality range: 0-63, lower numbers = higher quality
   * Recommended ranges:
   * 10-20: High quality (larger file size)
   * 20-30: Medium quality
   * 30-40: Low quality (smaller file size)
   */
  config.frame_size = resolution;
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

// Clean up previous image buffer if it exists
void cleanupImageBuffer() {
  if (lastImageBuffer != NULL) {
    free(lastImageBuffer);
    lastImageBuffer = NULL;
    lastImageSize = 0;
  }
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
  
  // Store the image in memory for sharing
  cleanupImageBuffer();
  lastImageBuffer = (uint8_t*)malloc(fb->len);
  if (lastImageBuffer) {
    memcpy(lastImageBuffer, fb->buf, fb->len);
    lastImageSize = fb->len;
    
    // Generate base64 from the image data
    base64Image = base64::encode(fb->buf, fb->len);
    
    newImageAvailable = true;
    Serial.println("Image stored in memory for sharing");
  } else {
    Serial.println("Failed to allocate memory for image sharing");
  }
  
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
  
  // Generate base64 from file data
  base64Image = base64::encode(fileBuffer, fileSize);
  free(fileBuffer);
  
  // Print a short confirmation that base64 is ready
  Serial.println("Base64 encoding completed. Length: " + String(base64Image.length()));
  
  client.setTimeout(10000);
  if (!client.connect(host, httpsPort)) {
    geminiReply = "Connection failed";
    return;
  }
  
  String url = "/v1beta/models/gemini-2.0-flash-lite:generateContent?key=" + String(GEMINI_API_KEY);
  
  // Prepare request body
  DynamicJsonDocument doc(40000);
  doc["contents"][0]["parts"][0]["text"] = PROMPT;
  doc["contents"][0]["parts"][1]["inline_data"]["mime_type"] = "image/jpeg";
  doc["contents"][0]["parts"][1]["inline_data"]["data"] = base64Image;
  doc["generationConfig"]["maxOutputTokens"] = 100;
  
  String payload;
  serializeJson(doc, payload);
  
  // Print request info for diagnostics
  Serial.println("Sending request to: " + String(host) + url);
  Serial.println("Prompt: " + PROMPT);
  
  client.println("POST " + url + " HTTP/1.1");
  client.println("Host: " + String(host));
  client.println("Connection: close");
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(payload.length());
  client.println();
  client.println(payload);
  
  // Delete the image file to free space (still have it in memory)
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
  
  // Print response for diagnostics
  Serial.println("Raw API response (first 300 chars):");
  Serial.println(response.substring(0, 300) + "...");
  
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

// HTTP Server handlers
void handleRoot() {
  String html = "<html><body>";
  html += "<h1>ESP32 Camera Server</h1>";
  html += "<p>Last analysis: " + geminiReply + "</p>";
  html += "<p><a href='/capture'>Capture New Image</a></p>";
  html += "<p><a href='/latest'>View Latest Image</a></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleCapture() {
  // Create a unique filename
  currentPhotoFilePath = "/photo" + String(photoCounter) + ".jpg";
  photoCounter++;
  
  // Flush the camera buffer so the next capture is fresh
  flushCameraBuffer();
  
  String result;
  if (captureImage()) {
    analyzeImage();
    result = "Image captured and analyzed. Result: " + geminiReply;
  } else {
    result = "Capture failed";
  }

  String html = "<html><body>";
  html += "<h1>Image Capture</h1>";
  html += "<p>" + result + "</p>";
  html += "<p><a href='/'>Back to home</a></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleLatestImage() {
  if (lastImageBuffer != NULL && lastImageSize > 0) {
    server.sendHeader("Content-Disposition", "inline; filename=latest.jpg");
    server.send_P(200, "image/jpeg", (const char*)lastImageBuffer, lastImageSize);
  } else {
    server.send(404, "text/plain", "No image available");
  }
}

// API endpoint to provide base64 encoded image
void handleGetBase64() {
  if (base64Image.length() > 0) {
    server.send(200, "text/plain", base64Image);
  } else {
    server.send(404, "text/plain", "No base64 image available");
  }
}

// API endpoint to check if a new image is available
void handleCheckNewImage() {
  String json = "{\"newImage\":" + String(newImageAvailable ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

// API endpoint to reset the new image flag after fetching
void handleResetNewImageFlag() {
  newImageAvailable = false;
  server.send(200, "text/plain", "Flag reset");
}

// Update camera resolution based on user input
bool updateResolution(int resolutionIndex) {
  if (resolutionIndex < 1 || resolutionIndex > 8) {
    return false;
  }
  
  // Adjust for 0-based array index
  resolutionIndex--;
  
  // Only update if resolution changed
  if (currentResolution != resolutionMap[resolutionIndex]) {
    currentResolution = resolutionMap[resolutionIndex];
    
    // Reinitialize camera with new resolution
    esp_camera_deinit();
    if (!initCamera(currentResolution)) {
      Serial.println("Camera reinitialization failed!");
      return false;
    }
    
    Serial.print("Resolution changed to: ");
    Serial.println(resolutionNames[resolutionIndex]);
  }
  
  return true;
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
  
  // Wait for WiFi connection with timeout
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    // Setup server routes
    server.on("/", handleRoot);
    server.on("/capture", handleCapture);
    server.on("/latest", handleLatestImage);
    server.on("/base64", handleGetBase64);  // New endpoint for base64 data
    server.on("/check", handleCheckNewImage);
    server.on("/reset", handleResetNewImageFlag);
    
    // Start server
    server.begin();
    Serial.println("HTTP server started");
  } else {
    Serial.println("\nWiFi connection failed! Running without server.");
  }
  
  if (!initCamera(currentResolution)) {
    Serial.println("Camera init failed!");
    while (1) delay(1000);
  }
  
  Serial.println("All systems ready");
  Serial.println("Press Enter to capture with current resolution");
  Serial.println("Or press 1-8 to change resolution and capture immediately:");
  Serial.println("1: QQVGA (160x120)   2: QVGA (320x240)    3: CIF (400x296)");
  Serial.println("4: VGA (640x480)     5: SVGA (800x600)    6: XGA (1024x768)");
  Serial.println("7: SXGA (1280x1024)  8: UXGA (1600x1200)");
}

void loop() {
  // Handle HTTP requests
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }
  
  // Check for serial input
  if (Serial.available() > 0) {
    char input = Serial.read();
    
    // Process numeric input for resolution change and immediately capture
    if (input >= '1' && input <= '8') {
      int resolutionChoice = input - '0';
      if (updateResolution(resolutionChoice)) {
        // Clear remaining input
        while (Serial.available() > 0) {
          Serial.read();
        }
        
        // Proceed to capture immediately
        // Create unique filename
        currentPhotoFilePath = "/photo" + String(photoCounter) + ".jpg";
        photoCounter++;
        
        Serial.println("--- Starting detection ---");
        Serial.print("Using resolution: ");
        // Find the current resolution name
        for (int i = 0; i < 8; i++) {
          if (resolutionMap[i] == currentResolution) {
            Serial.println(resolutionNames[i]);
            break;
          }
        }
        
        // Flush the camera buffer so the next capture is fresh
        flushCameraBuffer();
        
        if (captureImage()) {
          analyzeImage();
          Serial.println("Result: " + geminiReply);
        } else {
          Serial.println("Capture failed");
        }
        
        Serial.println("\nPress Enter to start detection with current resolution");
        Serial.println("Or press 1-8 to change resolution and capture");
      }
    }
    // Process Enter key for capture
    else if (input == '\r' || input == '\n') {
      // Clear remaining input
      while (Serial.available() > 0) {
        Serial.read();
      }
      
      // Create unique filename
      currentPhotoFilePath = "/photo" + String(photoCounter) + ".jpg";
      photoCounter++;
      
      Serial.println("--- Starting detection ---");
      Serial.print("Using resolution: ");
      // Find the current resolution name
      for (int i = 0; i < 8; i++) {
        if (resolutionMap[i] == currentResolution) {
          Serial.println(resolutionNames[i]);
          break;
        }
      }
      
      // Flush the camera buffer so the next capture is fresh
      flushCameraBuffer();
      
      if (captureImage()) {
        analyzeImage();
        Serial.println("Result: " + geminiReply);
      } else {
        Serial.println("Capture failed");
      }
      
      Serial.println("\nPress Enter to start detection with current resolution");
      Serial.println("Or press 1-8 to change resolution");
    }
    // Ignore other characters
  }
  
  delay(10); // Small delay to prevent CPU hogging
}