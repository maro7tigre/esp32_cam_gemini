
# Setup Instructions

This project requires that you install the [ArduinoJson](https://github.com/bblanchon/ArduinoJson) library and create a separate credentials file to store sensitive data (such as your WiFi credentials and Gemini API key). Follow the steps below:

## 1. Install ArduinoJson Library

### Using Arduino IDE:
1. Open the Arduino IDE or PlatformIO.
2. Go to **Sketch > Include Library > Manage Libraries…** or **Home > libraries**
3. search for **"ArduinoJson"** and **"Base64"**.
4. Find **ArduinoJson by Benoit Blanchon** and **Base64 by Densaugeo** then click **Install** or **Add to Project** and select your project.

## 2. Create a Credentials File

To keep sensitive data out of your main source code and avoid committing them to version control, create a separate header file called `credentials.h` in `src/` directory.

### Template for `credentials.h`:

```cpp
#ifndef CREDENTIALS_H
#define CREDENTIALS_H

// WiFi credentials
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

// Gemini API key
const char* GEMINI_API_KEY = "YOUR_GEMINI_API_KEY";

#endif // CREDENTIALS_H
```

