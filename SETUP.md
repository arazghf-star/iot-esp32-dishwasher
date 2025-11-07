# Setup Instructions

## ⚠️ IMPORTANT: Update WiFi Credentials Before Compiling

The firmware contains hardcoded WiFi credentials that **MUST** be changed before use.

### Steps:

1. **Open the firmware file:**
   ```
   firmware/dishwasher_controller.ino
   ```

2. **Find these lines (around line 50-51):**
   ```cpp
   #define WIFI_SSID "Araz's S23 FE"
   #define WIFI_PASSWORD "arazghf12"
   ```

3. **Replace with your WiFi details:**
   ```cpp
   #define WIFI_SSID "YOUR_WIFI_NAME"
   #define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
   ```

4. **Optional: Update OTA hostname and password (around line 2056):**
   ```cpp
   ArduinoOTA.setHostname("Dishwasher");          // Change to your preferred name
   ArduinoOTA.setPassword("dishwasher123");       // Change to your OTA password
   ```

5. **Save the file and compile**

## Alternative: Use config.h (Recommended for Git)

For better security when working with git:

1. Copy `config.h.example` to `config.h`:
   ```bash
   cp firmware/config.h.example firmware/config.h
   ```

2. Edit `config.h` with your credentials

3. Modify the `.ino` file to include `config.h` at the top:
   ```cpp
   #include "config.h"  // Add this line at the very top
   ```

4. Comment out or remove the hardcoded defines in the `.ino` file

Note: `config.h` is already in `.gitignore` so your credentials won't be committed.

## Quick Start

See [README.md](README.md) for complete setup instructions including:
- Hardware requirements
- Library dependencies
- Upload procedures
- Testing and debugging
