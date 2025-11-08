# 🌊 IoT ESP32 Dishwasher Controller

[![Arduino](https://img.shields.io/badge/Arduino-Compatible-00979D.svg)](https://www.arduino.cc/)
[![ESP32](https://img.shields.io/badge/ESP32-IoT-E7352C.svg)](https://www.espressif.com/)
[![OTA](https://img.shields.io/badge/OTA-Enabled-green.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Production-grade IoT controller for an automated dishwasher system built on ESP32 with WiFi, OTA updates, state machine logic, and comprehensive sensor integration.

## 📋 Overview

This project demonstrates **edge computing** and **IoT integration** by implementing a full-featured dishwasher controller with:

- **ESP32 microcontroller** with WiFi connectivity
- **8 different wash programs** with custom temperature profiles
- **Real-time state machine** for cycle management
- **OTA firmware updates** (ElegantOTA + ArduinoOTA)
- **OLED display** with custom graphics and animations
- **Multiple sensors**: Temperature (thermistor), pressure switch, door switch, rinse aid level
- **Relay control**: Water valves, heater, motors, pumps (7 relays)
- **Watchdog timer** for system reliability
- **Serial command interface** for debugging and manual control
- **Calibration modes** for buttons and program selector
- **Error recovery** with state persistence
- **Simulation mode** for testing without hardware

**⚠️ Privacy & Safety:** This is a personal IoT project. All credentials are externalized and git-ignored. Device operates on local WiFi only.

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     ESP32 Controller                         │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │  State       │  │   Display    │  │   Network    │      │
│  │  Machine     │◄─┤   Manager    │◄─┤   (WiFi/OTA) │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
│         │                                                    │
│         ▼                                                    │
│  ┌──────────────────────────────────────────────────┐      │
│  │          Hardware Abstraction Layer              │      │
│  └──────────────────────────────────────────────────┘      │
│    │      │       │       │       │       │        │        │
└────┼──────┼───────┼───────┼───────┼───────┼────────┼────────┘
     │      │       │       │       │       │        │
     ▼      ▼       ▼       ▼       ▼       ▼        ▼
  ┌─────┐┌─────┐┌──────┐┌──────┐┌──────┐┌──────┐┌──────┐
  │Temp ││Door ││Water ││Motor ││Heater││Pump  ││OLED  │
  │Sensor│Switch│Valve ││      ││      ││      ││Display│
  └─────┘└─────┘└──────┘└──────┘└──────┘└──────┘└──────┘
```

### Edge-to-Cloud Potential

While currently operating standalone, this architecture is designed for cloud integration:

```
ESP32 Device → MQTT/HTTP → AWS IoT Core → Lambda → DynamoDB
                                        ↓
                                   CloudWatch
                                        ↓
                                   SNS/Alerts
```

## 🚀 Key Features

### Microcontroller & Connectivity
- ✅ **ESP32** - Dual-core 240MHz, WiFi/Bluetooth capable
- ✅ **WiFi connectivity** for remote access and updates
- ✅ **OTA updates** - Update firmware over-the-air without USB
- ✅ **Watchdog timer** - Automatic recovery from crashes
- ✅ **State persistence** - Resume after power loss

### Control System
- ✅ **Complex state machine** with 13 states
- ✅ **8 wash programs** (Intensive, Eco, Rapid, etc.)
- ✅ **Real-time cycle control** with precise timing
- ✅ **Error handling** with recovery mechanisms
- ✅ **Pause/Resume** functionality
- ✅ **Delayed start** (up to 24 hours)

### Hardware Integration
- ✅ **7 relay outputs** (water valves, heater, motors, pumps)
- ✅ **NTC thermistor** temperature sensing (B-coefficient calculation)
- ✅ **Pressure switch** for water level detection
- ✅ **Door switch** for safety interlock
- ✅ **Rinse aid sensor** with low-level warning
- ✅ **OLED display** (128x64 SSD1306) with animations
- ✅ **Analog button matrix** with debouncing
- ✅ **Program selector** with ADC reading

### Software Quality
- ✅ **Modular C++ design** with classes and separation of concerns
- ✅ **PROGMEM optimization** for flash memory usage
- ✅ **Serial command interface** for diagnostics
- ✅ **Calibration tools** for ADC values
- ✅ **Simulation mode** for code testing
- ✅ **Logging system** (INFO/WARN/ERROR)

## 📁 Project Structure

```
iot-esp32-dishwasher/
├── firmware/
│   ├── dishwasher_controller.ino    # Main firmware
│   └── config.h.example              # Configuration template
├── docs/
│   ├── wiring-diagram.png            # Hardware connections
│   ├── state-machine.png             # State diagram
│   └── bill-of-materials.md          # Component list
├── .github/
│   └── workflows/
│       └── arduino-build.yml         # CI for firmware builds
├── .gitignore
├── LICENSE
└── README.md
```

## 🛠️ Hardware Requirements

| Component | Specification | Purpose |
|-----------|---------------|---------|
| **Microcontroller** | ESP32-WROOM-32 | Main controller |
| **Display** | SSD1306 128x64 OLED | User interface |
| **Relays** | 7x SSR or mechanical relays | Actuator control |
| **Temperature Sensor** | NTC 100kΩ thermistor | Water temperature |
| **Pressure Switch** | Water-level sensor | Fill detection |
| **Door Switch** | Magnetic reed switch | Safety |
| **Power Supply** | 5V 2A | ESP32 + relays |

*See [docs/bill-of-materials.md](docs/bill-of-materials.md) for complete list and part numbers.*

## 🏃 Quick Start

### Prerequisites
- Arduino IDE 2.x or PlatformIO
- ESP32 board package installed
- USB cable for initial programming

### Libraries Required
```cpp
- Adafruit GFX Library
- Adafruit SSD1306
- ArduinoOTA
- ElegantOTA
- WebServer
- Preferences (built-in)
```

### Setup Steps

1. **Clone the repository**
```bash
git clone https://github.com/arazghf-star/iot-esp32-dishwasher.git
cd iot-esp32-dishwasher/firmware
```

2. **Configure WiFi credentials**
```bash
cp config.h.example config.h
# Edit config.h with your WiFi SSID and password
```

3. **Install dependencies**
- Open Arduino IDE
- Go to Tools → Manage Libraries
- Install required libraries listed above

4. **Upload firmware**
```bash
# Select board: ESP32 Dev Module
# Select port: /dev/ttyUSB0 (Linux) or COM3 (Windows)
# Click Upload
```

5. **Access OTA interface**
```
http://<ESP32_IP_ADDRESS>/update
```

## 📡 Serial Commands

Connect via serial monitor (115200 baud) for debugging:

```
STATUS         - Display system status
MEMORY         - Show memory usage
TEST           - Run self-diagnostics
P1-P8          - Select program (1=Intensive, 8=Self Clean)
START          - Start program
HALF           - Toggle half-load mode
DRY            - Toggle extra drying
DELAY <hours>  - Set delay start (0-24 hours)
RESET          - Cancel and reset
SERIAL         - Enter relay control mode
CALIBRATE BUTTONS  - Calibrate button ADC values
CALIBRATE SELECTOR - Calibrate program selector
```

### Example Session
```
>> STATUS
--- SYSTEM STATUS ---
State: IDLE
Program: 1 (Intensive)
Temperature: 22.5 C
Door: Closed
Pressure: Inactive
Rinse Aid: OK
Options: Half=OFF, ExtraDry=OFF, Delay=0h
--------------------

>> P3
Program selected: 3 (Eco)

>> START
Program started.
```

## 🧪 Testing

### Simulation Mode
Enable simulation in firmware for hardware-less testing:
```cpp
#define SIMULATION_MODE 1
```

Then control via serial:
```
SIM_TEMP 65.0      - Set temperature to 65°C
SIM_DOOR_OPEN      - Simulate door opening
SIM_DOOR_CLOSE     - Simulate door closing
SIM_PRESSURE_ON    - Simulate water filled
SIM_RINSE_AID_LOW  - Simulate rinse aid low
```

### Hardware Tests
```
>> TEST
Starting self-test...
Test Relay: Heater    [OK]
Test Relay: Water     [OK]
Test Relay: Motor     [OK]
...
Test Sensors...       [OK]
Self-Test PASSED!
```

## 🔒 Security

- **WiFi credentials** externalized in `config.h` (git-ignored)
- **OTA password** required for firmware updates
- **No hardcoded secrets** in repository
- **Local network only** - no public cloud exposure by default
- **Watchdog timer** prevents system hangs
- **Door interlock** prevents operation when open

## 📊 State Machine

The controller implements a complex state machine with the following states:

| State | Description |
|-------|-------------|
| IDLE | Waiting for user input |
| DELAYED_START | Counting down delay timer |
| RUNNING | Executing wash cycle |
| PAUSED | Cycle paused (door opened) |
| PAUSING_BETWEEN_STEPS | Transition between steps |
| FINISHED | Cycle complete |
| ERROR | Error condition detected |
| AWAITING_RECOVERY | Power loss recovery |
| SERIAL_CONTROL | Manual relay control mode |
| CALIBRATION_BUTTONS | ADC calibration mode |
| CALIBRATION_SELECTOR | Program selector calibration |
| SELF_TEST | Running diagnostics |

## 🌐 Cloud Integration (Future)

Planned enhancements for cloud connectivity:

```python
# Example MQTT telemetry publishing
{
  "device_id": "dishwasher_01",
  "timestamp": "2024-01-15T10:30:00Z",
  "state": "RUNNING",
  "program": "Eco",
  "step": 3,
  "progress_percent": 45,
  "temperature_celsius": 52.3,
  "door_closed": true,
  "rinse_aid_ok": true,
  "error_code": 0
}
```

Potential integrations:
- **AWS IoT Core** - Device shadows, rules engine
- **MQTT broker** - Mosquitto or AWS IoT
- **Time-series DB** - InfluxDB or TimescaleDB
- **Visualization** - Grafana dashboards
- **Alerts** - SNS/Email on errors

## 🔧 Calibration

### Button Calibration
```
>> CALIBRATE BUTTONS
Press: START      → (press START button)
Press: HALF LOAD  → (press HALF LOAD button)
Press: EXTRA DRY  → (press EXTRA DRY button)
Press: DELAY START → (press DELAY button)
Calibration complete!
```

### Selector Calibration
```
>> CALIBRATE SELECTOR
ADC: 85    → Turn selector to position 1
ADC: 405   → Turn selector to position 2
...
>> EXIT
```

Values are saved to non-volatile memory.

## 📈 Performance

- **Boot time**: ~2 seconds
- **OTA update**: ~30 seconds
- **State transition**: <50ms
- **Display refresh**: 100ms
- **Temperature precision**: ±2°C
- **Memory usage**: ~180KB flash, ~20KB RAM
- **Watchdog timeout**: 30 seconds

## 🐛 Troubleshooting

### WiFi Won't Connect
```
- Check SSID and password in config.h
- Verify 2.4GHz network (ESP32 doesn't support 5GHz)
- Check signal strength
```

### Display Shows "TEMP SENSOR FAIL"
```
- Check thermistor wiring
- Verify pull-up resistor value (10kΩ)
- Test ADC reading in SERIAL mode
```

### OTA Update Fails
```
- Ensure device is on same network
- Check OTA password
- Try ArduinoOTA if ElegantOTA fails
- Verify firewall isn't blocking port 3232
```

### Door Interlock Not Working
```
- Test door switch continuity
- Check INPUT_PULLUP configuration
- Verify pin assignment (GPIO 21)
```

## 🤝 Contributing

This is a portfolio project, but improvements are welcome!

1. Fork the repository
2. Create a feature branch
3. Commit your changes
4. Push to the branch
5. Open a Pull Request

## 📄 License

MIT License - See [LICENSE](LICENSE) file for details

## 🎓 Learning Outcomes

This project demonstrates:

1. **Embedded Systems** - Real-time control, interrupt handling, hardware abstraction
2. **IoT Connectivity** - WiFi, OTA updates, remote management
3. **State Machines** - Complex logic flow with error handling
4. **Sensor Integration** - ADC reading, thermistor calculations, debouncing
5. **Display Management** - Graphics rendering, animations, UI design
6. **Power Management** - Watchdog timers, brownout detection
7. **Error Recovery** - State persistence, auto-recovery
8. **Production Practices** - Calibration, diagnostics, logging

## 🔗 Related Projects

- **microservice-k8s-demo**: Cloud-native Python service
- **terraform-aws-examples**: Infrastructure as Code
- **ci-cd-templates**: Reusable GitHub Actions workflows

## 📞 Contact

**arazghf**
- GitHub: [@arazghf-star](https://github.com/arazghf-star)
- Email: araz.ghf@gmail.com

---

**Built with ❤️ for DevOps and IoT**
