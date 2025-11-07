# 📶 WiFi Configuration Guide - Step by Step

## ⚠️ CRITICAL: You MUST change WiFi settings before uploading to ESP32

Your current sketch has hardcoded WiFi credentials that will not work for others. Follow these steps exactly:

---

## 🚀 Quick Setup (2 Minutes)

### Step 1: Copy Your Firmware File

```bash
# Run this command from your desktop
copy "C:\Users\arazg\Documents\Arduino\sketch_jul15a\sketch_jul15a.ino" "C:\Users\arazg\Desktop\fructstake_bot\devops-portfolio\iot-esp32-dishwasher\firmware\dishwasher_controller.ino"
```

### Step 2: Open the Firmware in Arduino IDE

1. Open Arduino IDE
2. Go to **File → Open**
3. Navigate to: `C:\Users\arazg\Desktop\fructstake_bot\devops-portfolio\iot-esp32-dishwasher\firmware\`
4. Open `dishwasher_controller.ino`

### Step 3: Change WiFi Credentials (LINES 50-51)

Find these lines around line 50-51:

```cpp
// !!! VERY IMPORTANT: REPLACE WITH YOUR WIFI DETAILS !!!
#define WIFI_SSID "Araz's S23 FE"        // <--- نام وای فای خود را اینجا وارد کنید
#define WIFI_PASSWORD "arazghf12"  // <--- رمز وای فای خود را اینجا وارد کنید
```

**Change to YOUR WiFi details:**

```cpp
#define WIFI_SSID "YOUR_WIFI_NAME_HERE"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD_HERE"
```

**Example (if your WiFi is "Home WiFi" with password "mypassword123"):**

```cpp
#define WIFI_SSID "Home WiFi"
#define WIFI_PASSWORD "mypassword123"
```

### Step 4: Update OTA Password (OPTIONAL - Line 2056)

Find this line around line 2056:

```cpp
ArduinoOTA.setPassword("dishwasher123");
```

**Change to a secure password:**

```cpp
ArduinoOTA.setPassword("YOUR_SECURE_PASSWORD");
```

### Step 5: Save the File

- Press `Ctrl+S` or go to **File → Save**

### Step 6: Upload to ESP32

1. Connect ESP32 to your computer via USB
2. Select board: **Tools → Board → ESP32 Dev Module**
3. Select port: **Tools → Port → COM3** (or your ESP32 port)
4. Click the **Upload** button (→)
5. Wait for "Done uploading" message

### Step 7: Check Connection

1. Open Serial Monitor: **Tools → Serial Monitor**
2. Set baud rate to **115200**
3. You should see:
   ```
   Connecting to WiFi...
   WiFi connected! IP: 192.168.1.XXX
   ```

---

## 🔐 Better Approach: Use Config File (For Git Safety)

If you plan to commit code to Git frequently, use this method to keep credentials separate:

### Step A: Create config.h

1. In the `firmware/` folder, create a new file called `config.h`
2. Copy this content:

```cpp
#ifndef CONFIG_H
#define CONFIG_H

// Your WiFi credentials
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// OTA Configuration  
#define OTA_PASSWORD "your_ota_password"
#define OTA_HOSTNAME "Dishwasher-ESP32"

#endif
```

### Step B: Modify dishwasher_controller.ino

Add this line **at the very top** of `dishwasher_controller.ino` (before line 1):

```cpp
#include "config.h"
```

Then **comment out or delete** the hardcoded WiFi lines (lines 50-51):

```cpp
// #define WIFI_SSID "Araz's S23 FE"        // Now using config.h
// #define WIFI_PASSWORD "arazghf12"        // Now using config.h
```

### Step C: Verify config.h is Ignored

Check `.gitignore` includes:
```
firmware/config.h
```

✅ Now `config.h` won't be committed to Git!

---

## 🐛 Troubleshooting

### Problem: "WiFi connection failed"

**Solutions:**
1. **Check SSID spelling** - Must match exactly (case-sensitive!)
2. **Check password** - One wrong character = fails
3. **Verify 2.4GHz network** - ESP32 doesn't support 5GHz
4. **Check WiFi signal** - Move ESP32 closer to router
5. **Restart ESP32** - Unplug and replug USB

### Problem: "Cannot see Serial Monitor output"

**Solutions:**
1. Select correct baud rate: **115200**
2. Select correct COM port
3. Click the **Reset button** on ESP32
4. Check USB cable (try different cable)

### Problem: "IP address shown but can't access OTA"

**Solutions:**
1. Ensure computer and ESP32 on same WiFi network
2. Try pinging the IP: `ping 192.168.1.XXX`
3. Check firewall isn't blocking port 3232
4. Try accessing: `http://192.168.1.XXX/update`

### Problem: "Compilation errors"

**Solutions:**
1. Install required libraries:
   - Adafruit GFX Library
   - Adafruit SSD1306
   - ArduinoOTA  
   - ElegantOTA
2. Select correct board: **ESP32 Dev Module**
3. Check ESP32 board package installed

---

## 📋 Verification Checklist

Before uploading:

- [ ] WiFi SSID changed from "Araz's S23 FE" to yours
- [ ] WiFi password changed from "arazghf12" to yours
- [ ] File saved (Ctrl+S)
- [ ] ESP32 connected via USB
- [ ] Correct board selected (ESP32 Dev Module)
- [ ] Correct port selected (COM3 or similar)
- [ ] Serial Monitor baud rate = 115200

After uploading:

- [ ] Serial Monitor shows "WiFi connected!"
- [ ] IP address displayed (e.g., 192.168.1.105)
- [ ] Can access OTA: http://[IP_ADDRESS]/update
- [ ] No errors in Serial Monitor

---

## 🌐 Accessing OTA Update Interface

Once WiFi is connected:

1. Note the IP address from Serial Monitor
2. Open web browser
3. Go to: `http://192.168.1.XXX/update` (replace XXX with your IP)
4. You'll see the OTA update page
5. Click "Choose File" → Select new firmware
6. Click "Update"

---

## 💡 Tips

1. **Write down your ESP32's IP address** for future OTA updates
2. **Keep a backup** of your config.h file
3. **Test connection** before final hardware installation
4. **Use strong OTA password** if exposed to network
5. **Document your WiFi settings** somewhere safe

---

## 🎯 For Portfolio/GitHub

When pushing to GitHub, make sure:

1. ✅ `config.h` is in `.gitignore`
2. ✅ Only `config.h.example` is committed
3. ✅ No actual WiFi credentials in any committed file
4. ✅ README warns users to update credentials

---

## 📞 Need Help?

If stuck:
1. Check Serial Monitor for error messages
2. Verify WiFi credentials are 100% correct
3. Try a mobile hotspot to rule out router issues
4. Check ESP32 is getting enough power (2A recommended)

**Remember**: Changing WiFi settings is the **#1 thing** you must do before uploading!
