#include <Arduino.h>
#include <SPI.h>              // کتابخانه جدید برای SPI
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h> // *** FIX: Changed to SSD1306 library as it works better with the hardware ***
#include <Preferences.h>
#include <pgmspace.h>
#include <esp_task_wdt.h>
#include <math.h>             // برای توابع sin و cos در انیمیشن‌ها
#include <WiFi.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include <ArduinoOTA.h>


// =================================================================================
// --- بخش ۱: پیکربندی و ثابت‌های اصلی ---
// =================================================================================

// --- تنظیمات WiFi و OTA ---
// !!! VERY IMPORTANT: REPLACE WITH YOUR WIFI DETAILS !!!
#define WIFI_SSID "Araz's S23 FE"        // <--- نام وای فای خود را اینجا وارد کنید
#define WIFI_PASSWORD "arazghf12"  // <--- رمز وای فای خود را اینجا وارد کنید

// --- حالت شبیه‌سازی ---
#define SIMULATION_MODE 0

// --- ثابت‌های سیگنال رله ---
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// --- پین‌های سخت‌افزاری ---
// *** Using Hardware SPI pins for stability ***
#define OLED_CLK_PIN  18 // Hardware SPI Clock
#define OLED_MOSI_PIN 23 // Hardware SPI MOSI
#define OLED_CS_PIN   5
#define OLED_DC_PIN   15
#define OLED_RST_PIN  4

// --- ابعاد نمایشگر ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
// --- پین‌های رله و سنسورها ---
#define HEATER_PIN          25
#define WATER_VALVE_PIN     12
#define WASH_MOTOR_PIN      26
#define HALF_LOAD_VALVE_PIN 13
#define DRAIN_PUMP_PIN      27
#define REGEN_VALVE_PIN     14
#define DETERGENT_PIN       33
#define SELECTOR_ADC_PIN    32
#define BUTTONS_ADC_PIN     36 // *** FIX: Changed to a safe ADC1 pin (GPIO36) to avoid conflict with Temp Sensor ***
#define TEMP_SENSOR_PIN     34
#define PRESSURE_SWITCH_PIN 35
#define DOOR_SWITCH_PIN     21 // Door switch connected to GPIO 17
#define RINSE_AID_PIN       19 // *** NEW: Input for Rinse Aid level switch ***
#define LED_BUILTIN         2
#define BUZZER_PIN          22

// --- آرایه پین‌های رله برای کنترل عددی ---
const int relayPins[] = {
    HEATER_PIN, WATER_VALVE_PIN, WASH_MOTOR_PIN, HALF_LOAD_VALVE_PIN,
    DRAIN_PUMP_PIN, REGEN_VALVE_PIN, DETERGENT_PIN
};
const char* const relayNames[] PROGMEM = { "Heater", "Water", "Motor", "Half", "Pump", "Regen", "Detergent" };
const int NUM_RELAYS = sizeof(relayPins) / sizeof(relayPins[0]);


// --- ثابت‌های کالیبره شده ---
// *** UPDATED: New thresholds for 8 programs based on user values (ascending order) ***
const int programThresholds[] = { 85, 405, 875, 1365, 1860, 2690, 3687, 4095 };
const int numPrograms = sizeof(programThresholds) / sizeof(programThresholds[0]);

// --- آستانه‌های ADC دکمه‌ها ---
#define ADC_TOLERANCE           100
int startBtnAdcVal = 3526; // مقدار محاسبه شده برای مقاومت 62k اهم
int halfLoadBtnAdcVal = 1000;
int extraDryBtnAdcVal = 1500;
int delayStartBtnAdcVal = 2000;
#define NO_BUTTON_THRESHOLD     4000

// --- ثابت‌های ترمیستور ---
#define SERIES_RESISTOR     10000
#define THERMISTOR_NOMINAL  100
#define TEMPERATURE_NOMINAL 25
#define B_COEFFICIENT       3950
#define ADC_MAX             4095.0

// --- ثابت‌های زمان‌بندی و عملکردی ---
#define FILL_DURATION_S             120
#define FILL_TIMEOUT_MS             180000UL
#define HEAT_TIMEOUT_MS             900000UL
#define WASH_PULSE_ON_S             240
#define WASH_PULSE_OFF_S            30
#define EXTRA_DRY_DURATION_S        900
#define TEMP_HYSTERESIS_BAND        5
#define DETERGENT_PULSE_MS          200
#define BUTTON_DEBOUNCE_DELAY_MS    300
#define SELF_CLEAN_HOLD_DURATION_MS 10000
#define CANCEL_HOLD_DURATION_MS     3000 // *** UPDATED: Reduced cancel hold time to 3 seconds ***
#define PAUSE_BETWEEN_STEPS_MS      5000
#define RESUME_DELAY_MS             3000
#define FINISH_SCREEN_DURATION_MS   10000
#define AUTO_RECOVERY_TIMEOUT_MS    5000
#define DISPLAY_TOGGLE_INTERVAL_MS  20000
#define SELECTOR_DEBOUNCE_MS        50   // *** NEW: Debounce delay for program selector ***

// --- سیستم لاگ ---
#define LOG_INFO(msg)  Serial.printf("[INFO] %s\n", msg)
#define LOG_WARN(msg)  Serial.printf("[WARN] %s\n", msg)
#define LOG_ERROR(msg) Serial.printf("[ERROR] %s\n", msg)

// =================================================================================
// --- بخش ۲: تعریف ساختارها و حالت‌های برنامه ---
// =================================================================================

enum Action { FILL, HEAT, WASH, DRAIN, DRY, WASH_WITH_DETERGENT, END };
enum MachineState { IDLE, FADING_OUT, DELAYED_START, RUNNING, PAUSING_BETWEEN_STEPS, PAUSED, FINISHED, ERROR, SERIAL_CONTROL, AWAITING_RECOVERY, SELF_TEST, CALIBRATION_BUTTONS, CALIBRATION_SELECTOR };
enum Button { BTN_NONE, BTN_START, BTN_HALF_LOAD, BTN_EXTRA_DRY, BTN_DELAY };
enum ErrorCode { NO_ERROR, E_FILL_TIMEOUT, E_HEAT_TIMEOUT, E_DOOR_OPEN, E_TEMP_SENSOR };
// NEW: Enum for simplified program steps for symbol display
enum SimpleAction { SIMPLE_NONE, PRE_WASH, MAIN_WASH, RINSE, FINAL_RINSE, DRYING };


struct ProgramStep { Action action; int duration_seconds; int target_temp; };

// --- متغیرهای وضعیت کلی ---
MachineState currentState = IDLE;
MachineState previousState = IDLE;
ErrorCode currentError = NO_ERROR;
int selectedProgram = 1;
int currentStepIndex = 0;
unsigned long stepStartTime = 0;
unsigned long delayStartTime = 0;
unsigned long pauseStartTime = 0;
bool motorIsOn = false;
unsigned long lastMotorToggleTime = 0;
bool halfLoadOption = false;
bool extraDryingOption = false;
int  delayHours = 0;
bool isExtraDryingActive = false;
unsigned long lastButtonPressTime = 0;
unsigned long elapsedTimeOnPause = 0;
unsigned long resumeDelayStartTime = 0;
bool doorIsClosedForResume = false;
unsigned long finishTime = 0;
unsigned long recoveryStartTime = 0;
unsigned long totalProgramDurationSeconds = 0;
bool detergentReleased = false;
bool detergentPulseStarted = false;
unsigned long detergentPulseStartTime = 0;
unsigned long extraDryButtonHoldStartTime = 0;
bool isExtraDryButtonHeld = false;
unsigned long startButtonHoldStartTime = 0;
bool isStartButtonHeld = false;
bool cancelActionDone = false; // *** NEW: Flag to prevent tap after hold ***
unsigned long fadeOutStartTime = 0;
bool showTimeOnDisplay = true;
unsigned long lastDisplayToggleTime = 0;
unsigned long currentStepProgressAtResumeMs = 0;
int calibrationButtonIndex = 0;
const char* const calibrationButtonNames[] PROGMEM = {"START", "HALF LOAD", "EXTRA DRY", "DELAY START"};
enum CalibrateButtonType { CAL_START, CAL_HALF_LOAD, CAL_EXTRA_DRY, CAL_DELAY, NUM_CAL_BUTTONS };
bool otaActive = false; // فلگ برای حالت آپدیت OTA


// --- متغیرهای حالت شبیه‌سازی ---
#if SIMULATION_MODE
float sim_temperature = 43.0;
bool sim_pressure_switch = false;
bool sim_door_switch = true;
int sim_selector_adc = 600;
int sim_buttons_adc = NO_BUTTON_THRESHOLD;
bool sim_rinse_aid_low = true; // For simulation
#endif

// =================================================================================
// --- بخش ۳: تعریف برنامه‌های شستشو با PROGMEM ---
// =================================================================================

const char prog1_name[] PROGMEM = "Intensive";
const char prog2_name[] PROGMEM = "Time 4 You";
const char prog3_name[] PROGMEM = "Eco";
const char prog4_name[] PROGMEM = "Rapid 25'";
const char prog5_name[] PROGMEM = "Soak";
const char prog6_name[] PROGMEM = "Crystal Care";
const char prog7_name[] PROGMEM = "Just You";
const char prog8_name[] PROGMEM = "Self Clean"; // *** UPDATED: Demo removed, Self Clean is now P8

const char* const programNames[] PROGMEM = {
    prog1_name, prog2_name, prog3_name, prog4_name, prog5_name,
    prog6_name, prog7_name, prog8_name
};

const ProgramStep intensiveProgram[] = { {FILL, 120, 0}, {HEAT, 300, 40}, {WASH, 300, 40}, {DRAIN, 120, 0}, {FILL, 120, 0}, {WASH_WITH_DETERGENT, 5400, 65}, {DRAIN, 120, 0}, {FILL, 120, 0}, {WASH, 300, 0}, {DRAIN, 120, 0}, {FILL, 120, 0}, {HEAT, 300, 65}, {WASH, 300, 65}, {DRAIN, 120, 0}, {DRY, 900, 0}, {END, 0, 0} };
const ProgramStep time4YouProgram[] = { {FILL, 120, 0}, {WASH_WITH_DETERGENT, 3000, 50}, {DRAIN, 120, 0}, {FILL, 120, 0}, {WASH, 300, 0}, {DRAIN, 120, 0}, {FILL, 120, 0}, {HEAT, 300, 70}, {WASH, 300, 70}, {DRAIN, 120, 0}, {DRY, 900, 0}, {END, 0, 0} };
const ProgramStep ecoProgram[] = { {FILL, 120, 0}, {WASH, 300, 0}, {DRAIN, 120, 0}, {FILL, 120, 0}, {WASH, 300, 0}, {DRAIN, 120, 0}, {FILL, 120, 0}, {WASH_WITH_DETERGENT, 5400, 50}, {DRAIN, 120, 0}, {FILL, 120, 0}, {HEAT, 300, 65}, {WASH, 300, 65}, {DRAIN, 120, 0}, {DRY, 900, 0}, {END, 0, 0} };
const ProgramStep rapid25Program[] = { {FILL, 120, 0}, {HEAT, 300, 50}, {WASH, 900, 50}, {DRAIN, 120, 0}, {FILL, 120, 0}, {HEAT, 300, 62}, {WASH, 180, 62}, {DRAIN, 120, 0}, {END, 0, 0} };
const ProgramStep soakProgram[] = { {FILL, 120, 0}, {WASH, 600, 0}, {DRAIN, 120, 0}, {END, 0, 0} };
const ProgramStep crystalProgram[] = { {FILL, 120, 0}, {WASH_WITH_DETERGENT, 4200, 52}, {DRAIN, 120, 0}, {FILL, 120, 0}, {HEAT, 300, 45}, {WASH, 300, 45}, {DRAIN, 120, 0}, {FILL, 120, 0}, {HEAT, 300, 65}, {WASH, 300, 65}, {DRAIN, 120, 0}, {DRY, 900, 0}, {END, 0, 0} };
const ProgramStep justYouProgram[] = { {FILL, 120, 0}, {WASH_WITH_DETERGENT, 1500, 50}, {DRAIN, 120, 0}, {FILL, 120, 0}, {HEAT, 300, 67}, {WASH, 300, 67}, {DRAIN, 120, 0}, {DRY, 600, 0}, {END, 0, 0} };
const ProgramStep selfCleanProgram[] = { {FILL, 120, 0}, {HEAT, 600, 70}, {WASH, 1200, 70}, {DRAIN, 120, 0}, {END, 0, 0} };

const ProgramStep* const programs[] = {
    intensiveProgram, time4YouProgram, ecoProgram, rapid25Program,
    soakProgram, crystalProgram, justYouProgram, selfCleanProgram
};

// NEW: Simplified sequences for symbol display
const SimpleAction seq_intensive[] = {PRE_WASH, MAIN_WASH, RINSE, FINAL_RINSE, DRYING, SIMPLE_NONE};
const int map_intensive[] = {0, 0, 0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4};
const SimpleAction seq_time4you[] = {MAIN_WASH, RINSE, FINAL_RINSE, DRYING, SIMPLE_NONE};
const int map_time4you[] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 3};
const SimpleAction seq_eco[] = {PRE_WASH, MAIN_WASH, FINAL_RINSE, DRYING, SIMPLE_NONE};
const int map_eco[] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 3};
const SimpleAction seq_rapid[] = {MAIN_WASH, FINAL_RINSE, SIMPLE_NONE};
const int map_rapid[] = {0, 0, 0, 0, 1, 1, 1, 1};
const SimpleAction seq_soak[] = {PRE_WASH, SIMPLE_NONE};
const int map_soak[] = {0, 0, 0};
const SimpleAction seq_crystal[] = {MAIN_WASH, RINSE, FINAL_RINSE, DRYING, SIMPLE_NONE};
const int map_crystal[] = {0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3};
const SimpleAction seq_justyou[] = {MAIN_WASH, FINAL_RINSE, DRYING, SIMPLE_NONE};
const int map_justyou[] = {0, 0, 0, 1, 1, 1, 1, 2};
const SimpleAction seq_selfclean[] = {MAIN_WASH, SIMPLE_NONE};
const int map_selfclean[] = {0, 0, 0, 0};

const SimpleAction* const simplifiedProgramSequences[] = {seq_intensive, seq_time4you, seq_eco, seq_rapid, seq_soak, seq_crystal, seq_justyou, seq_selfclean};
const int* const simplifiedStepMaps[] = {map_intensive, map_time4you, map_eco, map_rapid, map_soak, map_crystal, map_justyou, map_selfclean};


// =================================================================================
// --- بخش ۴: کلاس‌ها و مدیران سیستم ---
// =================================================================================

// --- نمونه‌های سراسری ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_DC_PIN, OLED_RST_PIN, OLED_CS_PIN);
Preferences preferences;
WebServer server(80);

// --- توابع کمکی سراسری ---
void allRelaysOff();
float readTemperature();
bool isDoorClosed();
bool isPressureSwitchActive();
class SelfTest;
void playBeep(int frequency, int duration);
void playStartupSound();
void setupOTA();
int readADCWithValidation(int pin); // *** FIX: Forward declaration to fix compilation error ***
bool isRinseAidLow(); // Forward declaration for Rinse Aid function
void pauseProgram();
void resumeProgram();

// --- کلاس مدیریت تنظیمات ---
class SettingsManager {
public:
    struct Settings {
        int fillDuration = FILL_DURATION_S;
        int washPulseOn = WASH_PULSE_ON_S;
        int washPulseOff = WASH_PULSE_OFF_S;
        int tempHysteresis = TEMP_HYSTERESIS_BAND;
        int startBtnAdc = startBtnAdcVal;
        int halfLoadBtnAdc = halfLoadBtnAdcVal;
        int extraDryBtnAdc = extraDryBtnAdcVal;
        int delayStartBtnAdc = delayStartBtnAdcVal;
    } settings;

    void load() {
        preferences.begin("settings", true);
        settings.fillDuration = preferences.getInt("fillDur", FILL_DURATION_S);
        settings.washPulseOn = preferences.getInt("washOn", WASH_PULSE_ON_S);
        settings.washPulseOff = preferences.getInt("washOff", WASH_PULSE_OFF_S);
        settings.tempHysteresis = preferences.getInt("tempHys", TEMP_HYSTERESIS_BAND);
        settings.startBtnAdc = preferences.getInt("startAdc", startBtnAdcVal);
        settings.halfLoadBtnAdc = preferences.getInt("halfAdc", halfLoadBtnAdcVal);
        settings.extraDryBtnAdc = preferences.getInt("extraDryAdc", extraDryBtnAdcVal);
        settings.delayStartBtnAdc = preferences.getInt("delayAdc", delayStartBtnAdcVal);
        preferences.end();
        LOG_INFO("Settings loaded.");
    }

    void save() {
        preferences.begin("settings", false);
        preferences.putInt("fillDur", settings.fillDuration);
        preferences.putInt("washOn", settings.washPulseOn);
        preferences.putInt("washOff", settings.washPulseOff);
        preferences.putInt("tempHys", settings.tempHysteresis);
        preferences.putInt("startAdc", settings.startBtnAdc);
        preferences.putInt("halfAdc", settings.halfLoadBtnAdc);
        preferences.putInt("extraDryAdc", settings.extraDryBtnAdc);
        preferences.putInt("delayAdc", settings.delayStartBtnAdc);
        preferences.end();
        LOG_INFO("Settings saved.");
    }

    Settings& get() { return settings; }
};
SettingsManager settingsManager;

// --- کلاس مدیریت برنامه‌ها ---
class ProgramManager {
public:
    static unsigned long calculateTotalDuration(int progIndex) {
        if (progIndex < 1 || progIndex > numPrograms) return 0;
        unsigned long totalDuration = 0;
        const ProgramStep* program = programs[progIndex - 1];
        int i = 0;
        while (program[i].action != END) {
            totalDuration += program[i].duration_seconds;
            if (program[i+1].action != END) {
                totalDuration += (PAUSE_BETWEEN_STEPS_MS / 1000);
            }
            i++;
        }
        if (extraDryingOption && progIndex != 8) { // Self clean is now P8
            totalDuration += EXTRA_DRY_DURATION_S;
        }
        return totalDuration;
    }

    static int findMainWashTemp(int progIndex) {
        if (progIndex < 1 || progIndex > numPrograms) return 0;
        int maxTemp = 0;
        const ProgramStep* program = programs[progIndex - 1];
        int i = 0;
        while (program[i].action != END) {
            if (program[i].action == WASH || program[i].action == WASH_WITH_DETERGENT) {
                if (program[i].target_temp > maxTemp) {
                    maxTemp = program[i].target_temp;
                }
            }
            i++;
        }
        return maxTemp;
    }

    static void getProgramName(int index, char* buffer, size_t bufSize) {
        char* pgm_ptr = (char*)pgm_read_ptr(&(programNames[index]));
        strncpy_P(buffer, pgm_ptr, bufSize);
        buffer[bufSize - 1] = '\0';
    }

    static unsigned long getElapsedTime() {
        unsigned long completedDurationOfPreviousSteps = 0;
        for (int i = 0; i < currentStepIndex; i++) {
            if (selectedProgram >= 1 && selectedProgram <= numPrograms) {
                completedDurationOfPreviousSteps += programs[selectedProgram - 1][i].duration_seconds;
                if (programs[selectedProgram - 1][i+1].action != END) {
                    completedDurationOfPreviousSteps += (PAUSE_BETWEEN_STEPS_MS / 1000);
                }
            }
        }
        unsigned long currentStepActiveElapsedMs = 0;
        if (currentState == RUNNING) {
            if (millis() >= stepStartTime) {
                currentStepActiveElapsedMs = millis() - stepStartTime;
            }
        } else if (currentState == PAUSED) {
            currentStepActiveElapsedMs = elapsedTimeOnPause;
        }
        return completedDurationOfPreviousSteps + (currentStepProgressAtResumeMs / 1000) + (currentStepActiveElapsedMs / 1000);
    }

    static int getProgressPercent() {
        if (totalProgramDurationSeconds == 0) return 0;
        return constrain((getElapsedTime() * 100) / totalProgramDurationSeconds, 0, 100);
    }

    static int getStepCount(int progIndex) {
        if (progIndex < 1 || progIndex > numPrograms) return 0;
        const ProgramStep* program = programs[progIndex - 1];
        int count = 0;
        while (program[count].action != END) {
            count++;
        }
        return count;
    }
};

// --- کلاس مدیریت خطا ---
const char error_msg_0[] PROGMEM = "No Error";
const char error_msg_1[] PROGMEM = "Fill Timeout";
const char error_msg_2[] PROGMEM = "Heat Timeout";
const char error_msg_3[] PROGMEM = "Door Open";
const char error_msg_4[] PROGMEM = "Temp Sensor Fail";
const char* const errorMessages[] PROGMEM = { error_msg_0, error_msg_1, error_msg_2, error_msg_3, error_msg_4 };

class ErrorManager {
public:
    static void handleError(ErrorCode error) {
        if (currentState == ERROR && currentError == error) return;
        currentState = ERROR;
        currentError = error;
        allRelaysOff();
        char buffer[20];
        char* pgm_ptr = (char*)pgm_read_ptr(&(errorMessages[error]));
        strncpy_P(buffer, pgm_ptr, sizeof(buffer));
        buffer[sizeof(buffer)-1] = '\0';
        LOG_ERROR(buffer);
        playBeep(3500, 1200);
    }

    static bool checkSensors() {
        if (currentState == ERROR) return false;
        if (!isDoorClosed() && (currentState == RUNNING || currentState == DELAYED_START || currentState == PAUSED)) {
            handleError(E_DOOR_OPEN);
            return false;
        }
        if (currentState == RUNNING || currentState == PAUSED || currentState == AWAITING_RECOVERY) {
            float temp = readTemperature();
            if (temp < -50 || temp > 120) {
                handleError(E_TEMP_SENSOR);
                return false;
            }
        }
        return true;
    }
};

// --- داده‌های بیت‌مپ لوگوی جدید ---
const unsigned char epd_bitmap_Bitmap [] PROGMEM = {
	// 'logo', 128x32px
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x01, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x01, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0xc1, 0xe1, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x01, 0xc0, 0x01, 0xf0, 0x00, 0xe3, 0xff, 0x81, 0xff, 0x00, 0x3f, 0x80, 0x7f, 0x87, 0x1f, 0xf8, 
	0x03, 0x80, 0x00, 0xf8, 0x00, 0xe3, 0xff, 0xc1, 0xff, 0xc0, 0x7f, 0xc0, 0xff, 0x87, 0x1f, 0xf8, 
	0x07, 0x00, 0x00, 0x3c, 0x00, 0xe3, 0xff, 0xe1, 0xff, 0xe0, 0xf1, 0xe1, 0xe0, 0x87, 0x1f, 0xf8, 
	0x0e, 0x00, 0x00, 0x1e, 0x00, 0xe3, 0xc0, 0xf1, 0xc1, 0xe0, 0xe0, 0xe1, 0xc0, 0x07, 0x03, 0xc0, 
	0x1c, 0x01, 0xe0, 0x0e, 0x00, 0xe3, 0xc0, 0x71, 0xc0, 0xf1, 0xc0, 0x71, 0xc0, 0x07, 0x03, 0xc0, 
	0x38, 0x01, 0xe0, 0x0f, 0x00, 0xe3, 0xc0, 0x71, 0xc0, 0x71, 0xc0, 0x71, 0xe0, 0x07, 0x03, 0xc0, 
	0x38, 0x01, 0xe0, 0x07, 0x00, 0xe3, 0xc0, 0x71, 0xc0, 0x71, 0xff, 0xf1, 0xfe, 0x07, 0x03, 0xc0, 
	0x70, 0x01, 0xe0, 0x03, 0x80, 0xe3, 0xc0, 0x71, 0xc0, 0x71, 0xff, 0xf0, 0xff, 0x07, 0x03, 0xc0, 
	0x70, 0x01, 0xe0, 0x03, 0x80, 0xe3, 0xc0, 0x71, 0xc0, 0x71, 0xff, 0xf0, 0x7f, 0x87, 0x03, 0xc0, 
	0x60, 0x01, 0xe0, 0x03, 0x80, 0xe3, 0xc0, 0x71, 0xc0, 0x71, 0xc0, 0x00, 0x03, 0xc7, 0x03, 0xc0, 
	0x60, 0x01, 0xe0, 0x01, 0xc0, 0xe3, 0xc0, 0x71, 0xc0, 0xf1, 0xc0, 0x00, 0x01, 0xc7, 0x03, 0xc0, 
	0x60, 0x01, 0xe0, 0x01, 0xc0, 0xe3, 0xc0, 0x71, 0xc1, 0xf0, 0xe0, 0x00, 0x01, 0xc7, 0x03, 0xc0, 
	0x60, 0x01, 0xe0, 0x01, 0xc0, 0xe3, 0xc0, 0x71, 0xff, 0xe0, 0xf8, 0x61, 0x83, 0xc7, 0x03, 0xc0, 
	0x60, 0x01, 0xe0, 0x01, 0xc0, 0xe3, 0xc0, 0x71, 0xff, 0xc0, 0x7f, 0xe1, 0xff, 0x87, 0x03, 0xc0, 
	0x60, 0x01, 0xe0, 0x01, 0xc0, 0xe3, 0xc0, 0x71, 0xff, 0x00, 0x3f, 0xe1, 0xff, 0x07, 0x03, 0xc0, 
	0x60, 0x01, 0xe0, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x70, 0x01, 0xe0, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x70, 0x01, 0xe0, 0x03, 0x80, 0xfc, 0x00, 0x00, 0x0e, 0x00, 0x1f, 0x18, 0xcf, 0xc0, 0x00, 0x00, 
	0x30, 0x01, 0xe0, 0x07, 0x80, 0xc6, 0x00, 0x00, 0x0e, 0x00, 0x31, 0x98, 0xcc, 0x00, 0x00, 0x00, 
	0x38, 0x01, 0xe0, 0x07, 0x00, 0xc6, 0x00, 0x60, 0x0e, 0x00, 0x30, 0x18, 0xcc, 0x00, 0x00, 0x00, 
	0x1c, 0x01, 0xe0, 0x0f, 0x00, 0xc6, 0x66, 0x60, 0x1b, 0x00, 0x30, 0x18, 0xcc, 0x00, 0x00, 0x00, 
	0x1c, 0x01, 0xe0, 0x1e, 0x00, 0xc6, 0x66, 0x60, 0x1b, 0x00, 0x30, 0x18, 0xcc, 0x00, 0x00, 0x00, 
	0x0e, 0x00, 0xc0, 0x3c, 0x00, 0xfc, 0x66, 0x00, 0x1b, 0x0f, 0x37, 0x9f, 0xcf, 0xc0, 0x00, 0x00, 
	0x0f, 0x80, 0x00, 0x78, 0x00, 0xc6, 0x3e, 0x60, 0x3f, 0x80, 0x31, 0x98, 0xcc, 0x00, 0x00, 0x00, 
	0x07, 0xc0, 0x00, 0xf0, 0x00, 0xc6, 0x06, 0x60, 0x31, 0x80, 0x31, 0x98, 0xcc, 0x00, 0x00, 0x00, 
	0x03, 0xf0, 0x03, 0xe0, 0x00, 0xc6, 0x06, 0x60, 0x31, 0x80, 0x31, 0x98, 0xcc, 0x00, 0x00, 0x00, 
	0x00, 0xff, 0xff, 0xc0, 0x00, 0xfc, 0x3c, 0x00, 0x31, 0x80, 0x1f, 0x98, 0xcc, 0x00, 0x00, 0x00, 
	0x00, 0x3f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


// *** UPDATED DisplayManager Class ***
class DisplayManager {
private:
    bool isInitialized = false;
    unsigned long lastUpdateTime = 0;
public:
    bool init() {
        SPI.begin(OLED_CLK_PIN, -1, OLED_MOSI_PIN, -1);
        SPI.setFrequency(4000000); // کاهش سرعت به 4MHz برای پایداری

        if(!display.begin(SSD1306_SWITCHCAPVCC)) {
            LOG_ERROR("SSD1306 allocation failed");
            isInitialized = false;
            return false;
        }

        // *** بازگرداندن تنظیمات اختصاصی نمایشگر شما ***
        display.ssd1306_command(0xA0); // Normal column address
        display.ssd1306_command(0xC8); // Remapped mode (COM63 to COM0)
        display.ssd1306_command(0xDA); // SETCOMPINS
        display.ssd1306_command(0x02); // Sequential COM pin config for 128x32
        display.ssd1306_command(0x81); // SETCONTRAST
        display.ssd1306_command(0xCF); 
        display.ssd1306_command(0xA8); // SETMULTIPLEX
        display.ssd1306_command(0x3F); // 64-1
        display.ssd1306_command(0xD3); // SETDISPLAYOFFSET
        display.ssd1306_command(0x00); // no offset

        display.clearDisplay();
        display.display();
        isInitialized = true;
        LOG_INFO("SSD1306 Display Initialized with custom 128x32 settings");
        return true;
    }

    bool isReady() {
        return isInitialized;
    }
    
    void showStartupAnimation() {
        if (!isReady()) {
            LOG_ERROR("Display not ready for startup animation");
            return;
        }
        
        display.clearDisplay();
        
        // --- نمایش لوگوی جدید ---
        display.drawBitmap(0, 0, epd_bitmap_Bitmap, 128, 32, SSD1306_WHITE);
        display.display();
        delay(2500);

        // --- انیمیشن محو شدن صفحه ---
        for(int i = 0; i < 32/2; i+=2) {
            display.fillRect(0, i, SCREEN_WIDTH, 2, SSD1306_BLACK);
            display.fillRect(0, 32 - i - 2, SCREEN_WIDTH, 2, SSD1306_BLACK);
            display.display();
            delay(30);
        }
    }

    void drawPauseIcon(int16_t x, int16_t y) {
        display.fillRect(x+1, y, 2, 8, SSD1306_WHITE);
        display.fillRect(x+5, y, 2, 8, SSD1306_WHITE);
    }

    void drawPreWashSymbol(int16_t x, int16_t y, bool inverted) {
        uint16_t color = inverted ? SSD1306_BLACK : SSD1306_WHITE;
        if(inverted) display.fillRect(x, y, 22, 22, SSD1306_WHITE);
        
        for(int i = 0; i < 3; i++) {
            int dropX = x + 6 + i * 5;
            display.drawLine(dropX, y + 3, dropX, y + 8, color);
            display.fillCircle(dropX, y + 9, 1, color);
        }
        
        display.drawRect(x + 1, y + 1, 20, 20, color);
    }

    void drawMainWashSymbol(int16_t x, int16_t y, bool inverted) {
        uint16_t color = inverted ? SSD1306_BLACK : SSD1306_WHITE;
        if(inverted) display.fillRect(x, y, 22, 22, SSD1306_WHITE);
        
        display.drawCircle(x + 11, y + 11, 6, color);
        
        unsigned long time = millis();
        for(int i = 0; i < 4; i++) {
            float angle = radians(i * 90 + (time / 20)); 
            int x1 = x + 11 + 3 * cos(angle);
            int y1 = y + 11 + 3 * sin(angle);
            int x2 = x + 11 + 8 * cos(angle);
            int y2 = y + 11 + 8 * sin(angle);
            display.drawLine(x1, y1, x2, y2, color);
        }
        
        display.drawRect(x + 1, y + 1, 20, 20, color);
    }
    
    void drawRinseSymbol(int16_t x, int16_t y, bool inverted) {
      drawPreWashSymbol(x,y,inverted);
    }

    void drawFinalRinseSymbol(int16_t x, int16_t y, bool inverted) {
        uint16_t color = inverted ? SSD1306_BLACK : SSD1306_WHITE;
        if(inverted) display.fillRect(x, y, 22, 22, SSD1306_WHITE);
        drawPreWashSymbol(x,y,inverted);
        for(int i=0; i<3; ++i) {
             display.drawPixel(x + 8 + i*2, y + 17, color);
             display.drawPixel(x + 8 + i*2, y + 19, color);
        }
    }

    void drawDryingSymbol(int16_t x, int16_t y, bool inverted) {
        uint16_t color = inverted ? SSD1306_BLACK : SSD1306_WHITE;
        if(inverted) display.fillRect(x, y, 22, 22, SSD1306_WHITE);

        for(int i=0; i<3; ++i) {
             display.drawPixel(x + 6 + i*4, y+8, color);
             display.drawPixel(x + 6 + i*4, y+10, color);
             display.drawPixel(x + 6 + i*4, y+12, color);
             display.drawPixel(x + 6 + i*4, y+14, color);
        }
         display.drawRect(x+1, y+1, 20, 20, color);
    }
    
    void drawProgressArrow(int16_t x, int16_t y) {
        display.fillTriangle(x + 8, y + 7, x + 14, y + 11, x + 8, y + 15, SSD1306_WHITE);
    }
    
    void drawSymbol(SimpleAction action, int16_t x, int16_t y, bool inverted) {
        switch(action) {
            case PRE_WASH:    drawPreWashSymbol(x, y, inverted); break;
            case MAIN_WASH:   drawMainWashSymbol(x, y, inverted); break;
            case RINSE:       drawRinseSymbol(x, y, inverted); break;
            case FINAL_RINSE: drawFinalRinseSymbol(x, y, inverted); break;
            case DRYING:      drawDryingSymbol(x, y, inverted); break;
            default: break;
        }
    }

    void update() {
        if (!isReady()) return;
        
        bool needsFrequentUpdate = (currentState == RUNNING || 
                                    currentState == PAUSED || 
                                    currentState == PAUSING_BETWEEN_STEPS ||
                                    currentState == AWAITING_RECOVERY);
        
        if (needsFrequentUpdate && (millis() - lastUpdateTime < 100)) {
            return;
        }
        lastUpdateTime = millis();

        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);

        char progNameBuffer[17];
        if (selectedProgram >= 1 && selectedProgram <= numPrograms) {
            ProgramManager::getProgramName(selectedProgram - 1, progNameBuffer, sizeof(progNameBuffer));
        } else {
            strncpy(progNameBuffer, "UNKNOWN", sizeof(progNameBuffer));
        }

        switch (currentState) {
            case IDLE: {
                display.setCursor(0, 0);
                display.print(progNameBuffer);

                unsigned long duration = ProgramManager::calculateTotalDuration(selectedProgram);
                
                display.setTextSize(2);
                char timeStr[6];
                snprintf(timeStr, sizeof(timeStr), "%lu:%02lu", (duration / 3600), (duration % 3600) / 60);
                int16_t x1, y1;
                uint16_t w, h;
                display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
                display.setCursor((SCREEN_WIDTH - w) / 2, 8); 
                display.print(timeStr);
                
                display.setTextSize(1);
                char optionsBuffer[40] = {0};
                int offset = 0;
                if(halfLoadOption) offset += snprintf(optionsBuffer + offset, sizeof(optionsBuffer) - offset, "1/2 ");
                if(extraDryingOption) offset += snprintf(optionsBuffer + offset, sizeof(optionsBuffer) - offset, "E.Dry ");
                if(delayHours > 0) offset += snprintf(optionsBuffer + offset, sizeof(optionsBuffer) - offset, "Dly:%dh ", delayHours);
                if(isRinseAidLow()) offset += snprintf(optionsBuffer + offset, sizeof(optionsBuffer) - offset, "R.Aid! ");
                display.setCursor(0, 24); 
                display.print(optionsBuffer);
                break;
            }
            case DELAYED_START: {
                display.setCursor(0, 0);
                display.print(progNameBuffer);
                
                long remaining_ms = (long)delayHours * 3600000UL - (millis() - delayStartTime);
                int remaining_h = (remaining_ms > 0) ? (remaining_ms / 3600000UL) : 0;
                int remaining_m = (remaining_ms > 0) ? ((remaining_ms / 60000UL) % 60) : 0;
                
                display.setTextSize(2);
                char timeStr[6];
                snprintf(timeStr, sizeof(timeStr), "%02d:%02d", remaining_h, remaining_m);
                int16_t x1, y1;
                uint16_t w, h;
                display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
                display.setCursor((SCREEN_WIDTH - w) / 2, 8); 
                display.print(timeStr);
                
                display.setTextSize(1);
                display.setCursor(0, 24);
                display.print("Delay Start");
                break;
            }
            case RUNNING:
            case PAUSED:
            case PAUSING_BETWEEN_STEPS: {
                display.setCursor(0,0);
                display.print(progNameBuffer);

                float currentTemp = readTemperature();
                char tempStr[8];
                snprintf(tempStr, sizeof(tempStr), "%.0fC", currentTemp);
                int16_t x1, y1;
                uint16_t w, h;
                display.getTextBounds(tempStr, 0, 0, &x1, &y1, &w, &h);
                display.setCursor(SCREEN_WIDTH - w -1, 0);
                display.print(tempStr);

                unsigned long elapsedTime = ProgramManager::getElapsedTime();
                unsigned long remainingSeconds = (totalProgramDurationSeconds > elapsedTime) ? totalProgramDurationSeconds - elapsedTime : 0;
                char timeStr[6];
                snprintf(timeStr, sizeof(timeStr), "%lu:%02lu", remainingSeconds / 3600, (remainingSeconds % 3600) / 60);
                display.setTextSize(2);
                display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
                display.setCursor((SCREEN_WIDTH - w) / 2, 8); 
                display.print(timeStr);

                if (currentState == PAUSED) {
                    drawPauseIcon(((SCREEN_WIDTH - w) / 2) - 14, 12);
                }
                break;
            }
            case FINISHED: {
                display.setTextSize(1); 
                const char* msg = "Finished";
                int16_t x1, y1;
                uint16_t w, h;
                display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
                display.setCursor((SCREEN_WIDTH - w) / 2, 8);
                display.print(msg);
                
                const char* msg2 = "Door can be opened";
                display.getTextBounds(msg2, 0, 0, &x1, &y1, &w, &h);
                display.setCursor((SCREEN_WIDTH - w) / 2, 20); 
                display.print(msg2);
                break;
            }
            case ERROR: {
                display.setTextSize(1);
                display.setCursor(25, 0);
                display.print("ERROR");

                display.drawTriangle(5, 8, 15, 8, 10, 0, SSD1306_WHITE);
                display.drawLine(10, 2, 10, 6, SSD1306_WHITE);
                display.drawPixel(10, 7, SSD1306_WHITE);

                char errorBuffer[20];
                char* pgm_ptr = (char*)pgm_read_ptr(&(errorMessages[currentError]));
                strncpy_P(errorBuffer, pgm_ptr, sizeof(errorBuffer));
                errorBuffer[sizeof(errorBuffer)-1] = '\0';
                
                int16_t x1, y1;
                uint16_t w, h;
                display.getTextBounds(errorBuffer, 0, 0, &x1, &y1, &w, &h);
                display.setCursor((SCREEN_WIDTH - w) / 2, 12);
                display.print(errorBuffer);
                
                const char* msg = "Hold START to reset";
                display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
                display.setCursor((SCREEN_WIDTH - w) / 2, 24);
                display.print(msg);
                break;
            }
            case CALIBRATION_BUTTONS: {
                display.setTextSize(1);
                display.setCursor(0, 4);
                display.print("CALIBRATE BUTTONS");
                display.setCursor(0, 20);
                if (calibrationButtonIndex < NUM_CAL_BUTTONS) {
                    char calBtnName[12];
                    char* pgm_ptr = (char*)pgm_read_ptr(&(calibrationButtonNames[calibrationButtonIndex]));
                    strncpy_P(calBtnName, pgm_ptr, sizeof(calBtnName));
                    calBtnName[sizeof(calBtnName)-1] = '\0';
                    display.printf("Press: %s", calBtnName);
                }
                break;
            }
            case CALIBRATION_SELECTOR: {
                display.setTextSize(1);
                display.setCursor(0, 4);
                display.print("CALIBRATE SELECTOR");
                display.setCursor(0, 20);
                display.printf("ADC: %d", readADCWithValidation(SELECTOR_ADC_PIN));
                break;
            }
                  default:
                        break;
        }
        display.display();
    }
};
DisplayManager displayManager;

// --- کلاس تست خودکار ---
class SelfTest {
public:
    static bool runDiagnostics() {
        currentState = SELF_TEST;
        LOG_INFO("Starting self-test...");
        displayManager.update();

        for (int i = 0; i < NUM_RELAYS; i++) {
            char relayName[12];
            char* pgm_ptr = (char*)pgm_read_ptr(&(relayNames[i]));
            strncpy_P(relayName, pgm_ptr, sizeof(relayName));
            relayName[sizeof(relayName)-1] = '\0';
            
            display.clearDisplay();
            display.setTextSize(1);
            display.setTextColor(SSD1306_WHITE);
            display.setCursor(0, 4); 
            display.print("SELF-TEST MODE");
            char testMsg[22];
            snprintf(testMsg, sizeof(testMsg), "Test Relay: %s", relayName);
            display.setCursor(0, 20);
            display.print(testMsg);
            display.display();

            digitalWrite(relayPins[i], RELAY_ON);
            delay(250);
            digitalWrite(relayPins[i], RELAY_OFF);
            delay(100);
        }

        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 4); 
        display.print("SELF-TEST MODE");
        display.setCursor(0, 20); 
        display.print("Test Sensors...");
        display.display();

        float temp = readTemperature();
        if (temp < -50 || temp > 120) {
            ErrorManager::handleError(E_TEMP_SENSOR);
            LOG_ERROR("Temperature sensor failed!");
            return false;
        }
        delay(500);

        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 4);
        display.print("Self-Test");
        display.setCursor(0, 20);
        display.print("PASSED!");
        display.display();
        
        LOG_INFO("Self-test completed successfully.");
        playBeep(3500, 250);
        delay(2000);
        return true;
    }
};

// =================================================================================
// --- بخش ۵: توابع کمکی و خواندن سنسورها ---
// =================================================================================

void allRelaysOff() {
    for (int i = 0; i < NUM_RELAYS; i++) {
        digitalWrite(relayPins[i], RELAY_OFF);
    }
    motorIsOn = false;
    digitalWrite(WASH_MOTOR_PIN, RELAY_OFF);
}

int readADCWithValidation(int pin) {
    long sum = 0;
    for(int i = 0; i < 5; i++) {
        sum += analogRead(pin);
        delayMicroseconds(200);
    }
    return sum / 5;
}

float readTemperature() {
    #if SIMULATION_MODE
        if (currentState == RUNNING && currentStepIndex < ProgramManager::getStepCount(selectedProgram)) {
            Action currentAction = programs[selectedProgram-1][currentStepIndex].action;
            if ((currentAction == HEAT || currentAction == WASH || currentAction == WASH_WITH_DETERGENT) &&
                digitalRead(HEATER_PIN) == RELAY_ON) {
                sim_temperature += 0.2;
            } else {
                sim_temperature -= 0.05;
                if (sim_temperature < 20.0) sim_temperature = 20.0;
            }
        }
        return sim_temperature;
    #else
        int adcValue = readADCWithValidation(TEMP_SENSOR_PIN);
        if (adcValue < 10 || adcValue >= ADC_MAX) {
            return -100.0;
        }
        float resistance = SERIES_RESISTOR / ((float)ADC_MAX / adcValue - 1.0);
        float temperature = log(resistance / THERMISTOR_NOMINAL) / B_COEFFICIENT;
        temperature += 1.0 / (TEMPERATURE_NOMINAL + 273.15);
        temperature = 1.0 / temperature - 273.15;
        return temperature;
    #endif
}

int getSelectedProgram() {
    #if SIMULATION_MODE
        int adcValue = sim_selector_adc;
    #else
        int adcValue = readADCWithValidation(SELECTOR_ADC_PIN);
    #endif
    
    // *** UPDATED: Logic for standard ascending ADC values ***
    for (int i = 0; i < numPrograms; i++) {
        if (adcValue <= programThresholds[i]) {
            return i + 1; // Return program number 1 to 8
        }
    }
    return 1; // Fallback to program 1
}


Button checkActionButtons() {
    #if SIMULATION_MODE
        int adcValue = sim_buttons_adc;
    #else
        int adcValue = readADCWithValidation(BUTTONS_ADC_PIN);
    #endif
    if (adcValue > NO_BUTTON_THRESHOLD) return BTN_NONE;
    if (abs(adcValue - settingsManager.get().startBtnAdc) < ADC_TOLERANCE) return BTN_START;
    if (abs(adcValue - settingsManager.get().halfLoadBtnAdc) < ADC_TOLERANCE) return BTN_HALF_LOAD;
    if (abs(adcValue - settingsManager.get().extraDryBtnAdc) < ADC_TOLERANCE) return BTN_EXTRA_DRY;
    if (abs(adcValue - settingsManager.get().delayStartBtnAdc) < ADC_TOLERANCE) return BTN_DELAY;
    return BTN_NONE;
}

bool isDoorClosed() {
    #if SIMULATION_MODE
        return sim_door_switch;
    #else
        return digitalRead(DOOR_SWITCH_PIN) == LOW;
    #endif
}

bool isPressureSwitchActive() {
    #if SIMULATION_MODE
        return sim_pressure_switch;
    #else
        return digitalRead(PRESSURE_SWITCH_PIN) == HIGH;
    #endif
}

bool isRinseAidLow() {
    #if SIMULATION_MODE
        return sim_rinse_aid_low;
    #else
        return digitalRead(RINSE_AID_PIN) == LOW; // Assuming switch closes (goes LOW) when empty
    #endif
}


void saveState() {
    preferences.begin("dishwasher", false);
    preferences.putInt("state", currentState);
    preferences.putInt("step", currentStepIndex);
    preferences.putInt("prog", selectedProgram);
    preferences.putBool("half", halfLoadOption);
    preferences.putBool("dry", extraDryingOption);
    preferences.putInt("delay", delayHours);
    preferences.putBool("detergent", detergentReleased);
    preferences.putBool("detPulse", detergentPulseStarted);
    preferences.putBool("motorOn", motorIsOn);
    preferences.putBool("extraDryActive", isExtraDryingActive);
    unsigned long elapsedInCurrentStepMs = 0;
    if (currentState == RUNNING) {
        if (millis() >= stepStartTime) {
            elapsedInCurrentStepMs = millis() - stepStartTime;
        }
    } else if (currentState == PAUSED) {
        elapsedInCurrentStepMs = elapsedTimeOnPause;
    }
    preferences.putULong("currentStepProgressAtSaveMs", elapsedInCurrentStepMs);
    preferences.putULong("elapsed", elapsedInCurrentStepMs);
    unsigned long motorElapsed = 0;
    if (stepStartTime > 0 && lastMotorToggleTime >= stepStartTime) {
        motorElapsed = lastMotorToggleTime - stepStartTime;
    }
    preferences.putULong("motorElapsed", motorElapsed);
    preferences.putBool("recovery", true);
    preferences.end();
    LOG_INFO("State saved for recovery.");
}

void clearSavedState() {
    preferences.begin("dishwasher", false);
    preferences.putBool("recovery", false);
    preferences.putInt("state", IDLE);
    preferences.end();
    LOG_INFO("Recovery state cleared.");
}

void softReset() {
    allRelaysOff();
    clearSavedState();
    currentState = IDLE;
    currentError = NO_ERROR;
    halfLoadOption = false;
    extraDryingOption = false;
    delayHours = 0;
    isStartButtonHeld = false;
    isExtraDryingActive = false;
    detergentReleased = false;
    detergentPulseStarted = false;
    motorIsOn = false;
    currentStepProgressAtResumeMs = 0;
    LOG_INFO("Program cancelled by user.");
    playBeep(3500, 300);
}

void restoreState() {
    preferences.begin("dishwasher", true);
    selectedProgram = preferences.getInt("prog", 1);
    halfLoadOption = preferences.getBool("half", false);
    extraDryingOption = preferences.getBool("dry", false);
    delayHours = preferences.getInt("delay", 0);
    detergentReleased = preferences.getBool("detergent", false);
    detergentPulseStarted = preferences.getBool("detPulse", false);
    motorIsOn = preferences.getBool("motorOn", false);
    isExtraDryingActive = preferences.getBool("extraDryActive", false);
    totalProgramDurationSeconds = ProgramManager::calculateTotalDuration(selectedProgram);
    MachineState savedState = (MachineState)preferences.getInt("state", IDLE);
    currentStepIndex = preferences.getInt("step", 0);
    unsigned long savedElapsed = preferences.getULong("elapsed", 0);
    unsigned long motorElapsed = preferences.getULong("motorElapsed", 0);
    currentStepProgressAtResumeMs = preferences.getULong("currentStepProgressAtSaveMs", 0);
    preferences.end();

    unsigned long currentTime = millis();
    if (savedState == PAUSED) {
        elapsedTimeOnPause = savedElapsed;
        currentState = PAUSED;
        doorIsClosedForResume = false;
        LOG_INFO("Restored to PAUSED state.");
    } else if (savedState == RUNNING || savedState == DELAYED_START || savedState == PAUSING_BETWEEN_STEPS) {
        stepStartTime = currentTime;
        currentState = savedState;
        LOG_INFO("Restored to RUNNING/DELAYED_START/PAUSING state.");
    } else {
        currentState = IDLE;
        currentStepProgressAtResumeMs = 0;
        LOG_WARN("Invalid saved state, returning to IDLE.");
        clearSavedState();
        return;
    }
    lastMotorToggleTime = stepStartTime;
    if (motorIsOn) {
        digitalWrite(WASH_MOTOR_PIN, RELAY_ON);
    }
    if (halfLoadOption) {
        digitalWrite(HALF_LOAD_VALVE_PIN, RELAY_ON);
    }
    LOG_INFO("State restored successfully.");
    playBeep(3500, 150);
}

void pauseProgram() {
    if (currentState != RUNNING) return;
    elapsedTimeOnPause = millis() - stepStartTime; // Save progress
    allRelaysOff();
    currentState = PAUSED;
    saveState();
    LOG_INFO("Program PAUSED by user.");
}

void resumeProgram() {
    if (currentState != PAUSED) return;
    stepStartTime = millis() - elapsedTimeOnPause; // Restore progress
    lastMotorToggleTime = millis();
    if (halfLoadOption) { digitalWrite(HALF_LOAD_VALVE_PIN, RELAY_ON); }
    currentState = RUNNING;
    saveState();
    LOG_INFO("Program RESUMED by user.");
}


void startProgram() {
    if (currentState != IDLE) {
        LOG_WARN("Cannot start: Machine is not in IDLE state.");
        playBeep(3500, 150);
        return;
    }
    if (isDoorClosed()) {
        if(isRinseAidLow()){
            playBeep(3000, 150);
            delay(200);
            playBeep(3000, 150);
        }
        preferences.begin("dishwasher", false);
        preferences.putUInt("lastProg", selectedProgram);
        preferences.end();
        totalProgramDurationSeconds = ProgramManager::calculateTotalDuration(selectedProgram);
        
        if (delayHours > 0) {
            currentState = DELAYED_START;
            delayStartTime = millis();
            LOG_INFO("Transition to DELAYED_START.");
        } else {
            currentState = RUNNING;
            currentStepIndex = 0;
            stepStartTime = millis();
            if (halfLoadOption) { digitalWrite(HALF_LOAD_VALVE_PIN, RELAY_ON); }
            LOG_INFO("Transition to RUNNING.");
        }
        currentStepProgressAtResumeMs = 0;
        saveState();
        LOG_INFO("Program started.");
    } else {
        ErrorManager::handleError(E_DOOR_OPEN);
        LOG_WARN("Cannot start: Door is open.");
    }
}

// =================================================================================
// --- بخش ۶: منطق اصلی و ماشین حالت ---
// =================================================================================

void executeCurrentStep();

void handleButtonCalibration() {
    static unsigned long lastPressTime = 0;
    static int lastAdcValue = NO_BUTTON_THRESHOLD;

    int adcValue = readADCWithValidation(BUTTONS_ADC_PIN);

    // Detect a button press (any value significantly lower than NO_BUTTON_THRESHOLD)
    if (adcValue < NO_BUTTON_THRESHOLD && lastAdcValue >= NO_BUTTON_THRESHOLD) {
        if (millis() - lastPressTime > BUTTON_DEBOUNCE_DELAY_MS * 2) { // Longer debounce for calibration
            lastPressTime = millis();
            
            switch(calibrationButtonIndex) {
                case CAL_START:
                    settingsManager.get().startBtnAdc = adcValue;
                    LOG_INFO("Calibrated START button to: " + String(adcValue));
                    break;
                case CAL_HALF_LOAD:
                    settingsManager.get().halfLoadBtnAdc = adcValue;
                    LOG_INFO("Calibrated HALF LOAD button to: " + String(adcValue));
                    break;
                case CAL_EXTRA_DRY:
                    settingsManager.get().extraDryBtnAdc = adcValue;
                    LOG_INFO("Calibrated EXTRA DRY button to: " + String(adcValue));
                    break;
                case CAL_DELAY:
                    settingsManager.get().delayStartBtnAdc = adcValue;
                    LOG_INFO("Calibrated DELAY START button to: " + String(adcValue));
                    break;
            }
            playBeep(3800, 150);
            calibrationButtonIndex++;
            
            if (calibrationButtonIndex >= NUM_CAL_BUTTONS) {
                settingsManager.save();
                LOG_INFO("Button calibration complete. Exiting calibration mode.");
                currentState = IDLE;
                calibrationButtonIndex = 0;
                playBeep(4000, 400);
            }
        }
    }
    lastAdcValue = adcValue;
}


void processButtons() {
    Button pressedButton = checkActionButtons();
    
    // --- Start/Pause/Resume/Cancel Button Logic ---
    if (pressedButton == BTN_START) {
        if (!isStartButtonHeld) { // First moment the button is pressed
            isStartButtonHeld = true;
            startButtonHoldStartTime = millis();
            cancelActionDone = false; // Reset on new press
        }
    } else { // Button is not pressed
        if (isStartButtonHeld && !cancelActionDone) { // Was held, but not long enough for cancel
            // This is a TAP action
         unsigned long holdDuration = millis() - startButtonHoldStartTime;
            if (holdDuration < CANCEL_HOLD_DURATION_MS) {
                // This is a TAP action
                if (currentState == IDLE) startProgram();
                else if (currentState == RUNNING) pauseProgram();
                else if (currentState == PAUSED) resumeProgram();
            }
        }
        isStartButtonHeld = false; // Reset flag on release
    }

    if (isStartButtonHeld && !cancelActionDone) {
        if (millis() - startButtonHoldStartTime > CANCEL_HOLD_DURATION_MS) {
            bool isProgramActive = (currentState == RUNNING || currentState == PAUSED || currentState == DELAYED_START || currentState == PAUSING_BETWEEN_STEPS);
            if(isProgramActive) {
                softReset();
                cancelActionDone = true; // Mark cancel as done to prevent tap on release
            }
        }
    }

    // --- Other Buttons Logic (only in IDLE state) ---
    if (currentState == IDLE) {
        bool buttonJustPressed = false;
        if (pressedButton != BTN_NONE && pressedButton != BTN_START && (millis() - lastButtonPressTime > BUTTON_DEBOUNCE_DELAY_MS)) {
             buttonJustPressed = true;
             lastButtonPressTime = millis();
        }
        
        if (buttonJustPressed) {
            switch(pressedButton) {
                case BTN_HALF_LOAD:
                    halfLoadOption = !halfLoadOption;
                    LOG_INFO("Half Load option toggled");
                    break;
                case BTN_EXTRA_DRY:
                     if (!isExtraDryButtonHeld) {
                             isExtraDryButtonHeld = true;
                             extraDryButtonHoldStartTime = millis();
                     }
                    if (millis() - extraDryButtonHoldStartTime < SELF_CLEAN_HOLD_DURATION_MS) {
                        extraDryingOption = !extraDryingOption;
                        LOG_INFO("Extra Drying option toggled");
                    }
                    break;
                case BTN_DELAY:
                    delayHours = (delayHours + 3) % 25;
                    LOG_INFO("Delay hours set");
                    break;
                default: break;
            }
        }
    
         // SELF CLEAN activation
        if (pressedButton == BTN_EXTRA_DRY) {
            if (!isExtraDryButtonHeld) {
                isExtraDryButtonHeld = true;
                extraDryButtonHoldStartTime = millis();
            }
            if (isExtraDryButtonHeld && (millis() - extraDryButtonHoldStartTime >= SELF_CLEAN_HOLD_DURATION_MS)) {
                selectedProgram = 8; // Self Clean is P8
                startProgram();
                isExtraDryButtonHeld = false;
            }
        } else {
            isExtraDryButtonHeld = false;
        }
    }
    
    // Handle Program Selection in IDLE state
    if(currentState == IDLE) {
        static int lastReadProgram = 1;
        static unsigned long lastSelectorReadTime = 0;

        int currentReadProgram = getSelectedProgram();
        if (currentReadProgram != lastReadProgram) {
            lastSelectorReadTime = millis();
            lastReadProgram = currentReadProgram;
        }

        if (millis() - lastSelectorReadTime > SELECTOR_DEBOUNCE_MS) { 
            if (selectedProgram != lastReadProgram) {
                selectedProgram = lastReadProgram;
                LOG_INFO("Program selected: " + String(selectedProgram));
                playBeep(4000, 30); // Click sound
            }
        }
    }
}



void updateStateMachine() {
    bool recover = false;
    switch (currentState) {
        case IDLE:
            digitalWrite(LED_BUILTIN, (millis() % 2000) < 1000);
            break;

        case FADING_OUT:
            break;

        case AWAITING_RECOVERY:
            digitalWrite(LED_BUILTIN, (millis() % 600) < 300);
            if (checkActionButtons() == BTN_START && (millis() - lastButtonPressTime > BUTTON_DEBOUNCE_DELAY_MS)) {
                recover = true;
                LOG_INFO("Manual recovery initiated (button press).");
            } else if (millis() - recoveryStartTime > AUTO_RECOVERY_TIMEOUT_MS) {
                recover = true;
                LOG_INFO("Auto-recovery initiated (timeout).");
            }

            if (getSelectedProgram() != selectedProgram) {
                clearSavedState();
                currentState = IDLE;
                LOG_WARN("Program selector changed, recovery cancelled.");
            } else if (recover) {
                restoreState();
            }
            break;

        case SERIAL_CONTROL:
            digitalWrite(LED_BUILTIN, (millis() % 1000) < 100 || (millis() % 1000 > 200 && millis() % 1000 < 300));
            break;
        
        case CALIBRATION_BUTTONS:
            digitalWrite(LED_BUILTIN, (millis() % 500) < 250);
            handleButtonCalibration();
            break;

        case CALIBRATION_SELECTOR:
            // Logic is now handled by display update and serial command
            break;

        case DELAYED_START:
            digitalWrite(LED_BUILTIN, (millis() % 1000) < 500);
            if (!ErrorManager::checkSensors()) break;
            if (millis() - delayStartTime >= (unsigned long)delayHours * 3600000UL) {
                currentState = RUNNING;
                currentStepIndex = 0;
                stepStartTime = millis();
                if (halfLoadOption) { digitalWrite(HALF_LOAD_VALVE_PIN, RELAY_ON); }
                saveState();
                LOG_INFO("Delayed start completed. Transition to RUNNING.");
            }
            break;

        case RUNNING:
            if (!ErrorManager::checkSensors()) {
                pauseProgram();
                break;
            }
            digitalWrite(LED_BUILTIN, HIGH);
            executeCurrentStep();
            break;

        case PAUSED:
            digitalWrite(LED_BUILTIN, (millis() % 3000) < 1500);
            if (isDoorClosed()) {
                if (!doorIsClosedForResume) {
                    doorIsClosedForResume = true;
                    resumeDelayStartTime = millis();
                    LOG_INFO("Door closed. Starting resume delay.");
                }
                if (millis() - resumeDelayStartTime > RESUME_DELAY_MS) {
                   resumeProgram();
                }
            } else {
                doorIsClosedForResume = false;
            }
            break;

        case PAUSING_BETWEEN_STEPS:
            digitalWrite(LED_BUILTIN, (millis() % 1000) < 100);
            if (millis() - pauseStartTime > PAUSE_BETWEEN_STEPS_MS) {
                currentStepIndex++;
                detergentReleased = false;
                detergentPulseStarted = false;
                stepStartTime = millis();
                lastMotorToggleTime = millis();
                currentStepProgressAtResumeMs = 0;
                currentState = RUNNING;
                saveState();
                LOG_INFO("Pause between steps finished. Transition to RUNNING.");
            }
            break;

        case FINISHED:
            digitalWrite(LED_BUILTIN, (millis() % 500) < 250);
            if (finishTime == 0) {
                finishTime = millis();
                playBeep(3500, 600);
                LOG_INFO("Program FINISHED.");
            }
            if (millis() - finishTime > FINISH_SCREEN_DURATION_MS) {
                currentState = IDLE;
                allRelaysOff();
                extraDryingOption = false; delayHours = 0; halfLoadOption = false;
                finishTime = 0;
                clearSavedState();
                currentStepProgressAtResumeMs = 0;
                LOG_INFO("Returning to IDLE state from FINISHED.");
            }
            break;

        case ERROR:
            digitalWrite(LED_BUILTIN, (millis() % 200) < 100);
            break;
    }
}

void executeCurrentStep() {
    if (isExtraDryingActive) {
        digitalWrite(HEATER_PIN, RELAY_ON);
        if (millis() - stepStartTime > (EXTRA_DRY_DURATION_S * 1000UL)) {
            digitalWrite(HEATER_PIN, RELAY_OFF);
            isExtraDryingActive = false;
            currentState = FINISHED;
            LOG_INFO("Extra Drying completed.");
        }
        return;
    }

    const ProgramStep* currentProgram = programs[selectedProgram - 1];
    ProgramStep step = currentProgram[currentStepIndex];
    bool stepCompleted = false;

    switch (step.action) {
        case FILL:
            digitalWrite(WATER_VALVE_PIN, RELAY_ON);
            if (isPressureSwitchActive() || (millis() - stepStartTime > ((unsigned long)settingsManager.get().fillDuration * 1000UL))) {
                stepCompleted = true;
                LOG_INFO("Fill step completed.");
            } else if (millis() - stepStartTime > FILL_TIMEOUT_MS) {
                ErrorManager::handleError(E_FILL_TIMEOUT);
                LOG_ERROR("Fill timeout error.");
            }
            break;
        case HEAT:
            if (readTemperature() < step.target_temp) {
                digitalWrite(HEATER_PIN, RELAY_ON);
            } else {
                digitalWrite(HEATER_PIN, RELAY_OFF);
                stepCompleted = true;
                LOG_INFO("Heat step completed.");
            }
            if (millis() - stepStartTime > HEAT_TIMEOUT_MS) {
                ErrorManager::handleError(E_HEAT_TIMEOUT);
                LOG_ERROR("Heat timeout error.");
            }
            break;
        case WASH_WITH_DETERGENT:
            if (!detergentReleased) {
                if (!detergentPulseStarted) {
                    digitalWrite(DETERGENT_PIN, RELAY_ON);
                    detergentPulseStartTime = millis();
                    detergentPulseStarted = true;
                    LOG_INFO("Detergent pulse started.");
                }
                if (detergentPulseStarted && millis() - detergentPulseStartTime > DETERGENT_PULSE_MS) {
                    digitalWrite(DETERGENT_PIN, RELAY_OFF);
                    detergentReleased = true;
                    detergentPulseStarted = false;
                    LOG_INFO("Detergent released.");
                }
                if (!detergentReleased) break;
            }
        // Fallthrough to WASH logic
        case WASH: {
            float currentTemp = readTemperature();
            if (currentTemp < (step.target_temp - settingsManager.get().tempHysteresis)) {
                digitalWrite(HEATER_PIN, RELAY_ON);
            } else if (currentTemp >= step.target_temp) {
                digitalWrite(HEATER_PIN, RELAY_OFF);
            }
            
            unsigned long now = millis();
            if (motorIsOn) {
                if (now - lastMotorToggleTime > ((unsigned long)settingsManager.get().washPulseOn * 1000UL)) {
                    motorIsOn = false;
                    lastMotorToggleTime = now;
                    digitalWrite(WASH_MOTOR_PIN, RELAY_OFF);
                    LOG_INFO("Wash motor OFF.");
                }
            } else {
                if (now - lastMotorToggleTime > ((unsigned long)settingsManager.get().washPulseOff * 1000UL)) {
                    motorIsOn = true;
                    lastMotorToggleTime = now;
                    digitalWrite(WASH_MOTOR_PIN, RELAY_ON);
                    LOG_INFO("Wash motor ON.");
                }
            }
            if (millis() - stepStartTime > (unsigned long)step.duration_seconds * 1000) {
                stepCompleted = true;
                LOG_INFO("Wash step completed.");
            }
            break;
        }
        case DRAIN:
            digitalWrite(DRAIN_PUMP_PIN, RELAY_ON);
            if (millis() - stepStartTime > (unsigned long)step.duration_seconds * 1000) {
                stepCompleted = true;
                LOG_INFO("Drain step completed.");
            }
            break;
        case DRY:
            digitalWrite(HEATER_PIN, RELAY_ON);
            if (millis() - stepStartTime > (unsigned long)step.duration_seconds * 1000) {
                stepCompleted = true;
                LOG_INFO("Dry step completed.");
            }
            break;
        case END:
            if (extraDryingOption) {
                isExtraDryingActive = true;
                stepStartTime = millis();
                LOG_INFO("Program END reached. Activating Extra Drying.");
            } else {
                currentState = FINISHED;
                LOG_INFO("Program END reached. Transition to FINISHED.");
            }
            break;
    }

    if (stepCompleted) {
        allRelaysOff();
        if (currentState != ERROR && step.action != END) {
            currentState = PAUSING_BETWEEN_STEPS;
            pauseStartTime = millis();
            currentStepProgressAtResumeMs = 0;
        }
    }
}

void handleSerialCommands();

// =================================================================================
// --- بخش ۷: توابع Setup و Loop ---
// =================================================================================

void enableWatchdog() {
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 30000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true,
    };
    
    esp_err_t err_init = esp_task_wdt_init(&wdt_config);
    
    if (err_init == ESP_OK) {
        LOG_INFO("Task Watchdog initialized successfully.");
    } else if (err_init == ESP_ERR_INVALID_STATE) {
        LOG_INFO("Task Watchdog was already initialized. Proceeding as normal.");
    } else {
        Serial.printf("CRITICAL ERROR: WDT Init Failed! Code: %d. Halting.\n", err_init);
        while(true) { delay(100); }
    }

    esp_err_t err_add = esp_task_wdt_add(NULL);
    if (err_add == ESP_OK) {
        LOG_INFO("Current task added to Watchdog successfully.");
    } else {
        Serial.printf("CRITICAL ERROR: WDT Add Task Failed! Code: %d. Halting.\n", err_add);
        while(true) { delay(100); }
    }
}

void feedWatchdog() {
    esp_task_wdt_reset();
}

void printMemoryInfo() {
    Serial.println("--- MEMORY STATUS ---");
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Largest free block: %d bytes\n", ESP.getMaxAllocHeap());
    Serial.println("---------------------");
}

void playBeep(int frequency, int duration) {
    tone(BUZZER_PIN, frequency, duration);
}

void playStartupSound() {
    // A simple startup melody
    playBeep(2093, 100); // C7
    delay(120);
    playBeep(2637, 100); // E7
    delay(120);
    playBeep(3136, 100); // G7
    delay(120);
    playBeep(3520, 150); // A7
}

void setupOTA() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
        delay(500);
        Serial.print(".");
        feedWatchdog(); // *** FIX: Added WDT reset during blocking WiFi connection attempt ***
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nFailed to connect to WiFi.");
    }

    // *** FIX: Corrected Lambda function syntax for WebServer library ***
    server.on("/", HTTP_GET, [](){
      server.sendHeader("Location", "/update", true);
      server.send(302, "text/plain", "");
    });

    ElegantOTA.begin(&server);
    server.begin();
    Serial.printf("OTA Ready! Go to: http://%s\n", WiFi.localIP().toString().c_str());

    ArduinoOTA.setHostname("Dishwasher");
    ArduinoOTA.setPassword("dishwasher123");
    ArduinoOTA.onStart([]() {
        otaActive = true;
        allRelaysOff();
        String type = ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem";
        Serial.println("Start updating " + type);
        if (displayManager.isReady()) {
            display.clearDisplay();
            display.setTextSize(1);
            display.setCursor(20, 16);
            display.print("OTA UPDATE...");
            display.display();
        }
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
        otaActive = false;
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
        otaActive = false;
    });
    ArduinoOTA.begin();
}


void setup() {
    delay(1000);
    
    Serial.begin(115200);
    delay(500);
    Serial.println("\n--- Dishwasher Control (V131 - WDT Fix) ---");
    
    enableWatchdog(); // *** FIX: Enable watchdog before any potentially blocking operations ***
    
    setupOTA();
    
    pinMode(BUZZER_PIN, OUTPUT);
    playStartupSound();

    if (!displayManager.init()) {
        LOG_ERROR("Display initialization FAILED! Check wiring. Halting.");
        while(1);
    }
    
    displayManager.showStartupAnimation();
    previousState = currentState;

    for(int i=0; i < NUM_RELAYS; i++) pinMode(relayPins[i], OUTPUT);
    allRelaysOff();

    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(PRESSURE_SWITCH_PIN, INPUT);
    pinMode(DOOR_SWITCH_PIN, INPUT_PULLUP);
    pinMode(RINSE_AID_PIN, INPUT_PULLUP); // *** NEW: Set pin mode for Rinse Aid switch ***

    settingsManager.load();
    startBtnAdcVal = settingsManager.get().startBtnAdc;
    halfLoadBtnAdcVal = settingsManager.get().halfLoadBtnAdc;
    extraDryBtnAdcVal = settingsManager.get().extraDryBtnAdc;
    delayStartBtnAdcVal = settingsManager.get().delayStartBtnAdc;

    Button startupButton = checkActionButtons();
    if (startupButton == BTN_START) {
        delay(BUTTON_DEBOUNCE_DELAY_MS);
        if (checkActionButtons() == BTN_START) {
            SelfTest::runDiagnostics();
            currentState = IDLE;
            clearSavedState();
            currentStepProgressAtResumeMs = 0;
            LOG_INFO("Self-test completed. Returning to IDLE.");
        }
    }
    
    preferences.begin("dishwasher", true);
    if (preferences.getBool("recovery", false)) {
        LOG_WARN("Unfinished program found. Awaiting user action for recovery.");
        currentState = AWAITING_RECOVERY;
        selectedProgram = preferences.getInt("prog", 1);
        recoveryStartTime = millis();
    } else {
        LOG_INFO("No recovery state found. Starting fresh.");
        selectedProgram = preferences.getUInt("lastProg", 1);
        currentState = IDLE;
    }
    preferences.end();
}

void loop() {
    feedWatchdog();
    ArduinoOTA.handle();
    server.handleClient();


    if (otaActive) {
        // اگر در حال آپدیت بود، بقیه منطق را اجرا نکن
        return;
    }

    static unsigned long lastLogicUpdate = 0;
    
    if (millis() - lastLogicUpdate > 20) {
        handleSerialCommands();
        if(currentState != CALIBRATION_BUTTONS && currentState != CALIBRATION_SELECTOR) {
            processButtons();
        }
        updateStateMachine();
        lastLogicUpdate = millis();
    }
    
    // *** FIX: Display should always update to show calibration modes correctly ***
    displayManager.update();
}

// --- پیاده‌سازی کامل رابط سریال ---
void handleSerialCommands() {
    if (Serial.available() > 0) {
        char cmdBuffer[50];
        int bytesRead = Serial.readBytesUntil('\n', cmdBuffer, sizeof(cmdBuffer) - 1);
        cmdBuffer[bytesRead] = '\0';
        String command = String(cmdBuffer);
        command.trim();
        if (command.length() == 0) return;
        
        Serial.print(">> Command: '"); Serial.print(command); Serial.println("'");
        command.toUpperCase();

        if (command == "STATUS") {
            Serial.println("--- SYSTEM STATUS ---");
            const char* stateNames[] = {"IDLE", "FADING_OUT", "DELAYED_START", "RUNNING", "PAUSING", "PAUSED", "FINISHED", "ERROR", "SERIAL_CONTROL", "AWAITING_RECOVERY", "SELF_TEST", "CALIBRATION_BUTTONS", "CALIBRATION_SELECTOR"};
            char progName[17];
            if (selectedProgram >= 1 && selectedProgram <= numPrograms) {
                ProgramManager::getProgramName(selectedProgram - 1, progName, sizeof(progName));
            } else {
                strncpy(progName, "N/A", sizeof(progName));
                progName[sizeof(progName)-1] = '\0';
            }
            
            Serial.printf("State: %s\n", stateNames[currentState]);
            Serial.printf("Program: %d (%s)\n", selectedProgram, progName);
            Serial.printf("Step: %d/%d\n", currentStepIndex + 1, ProgramManager::getStepCount(selectedProgram));
            Serial.printf("Progress: %d%%\n", ProgramManager::getProgressPercent());
            Serial.printf("Temperature: %.1f C\n", readTemperature());
            Serial.printf("Door: %s\n", isDoorClosed() ? "Closed" : "Open");
            Serial.printf("Pressure: %s\n", isPressureSwitchActive() ? "Active" : "Inactive");
            Serial.printf("Rinse Aid: %s\n", isRinseAidLow() ? "LOW" : "OK");
            Serial.printf("Options: Half=%s, ExtraDry=%s, Delay=%dh\n",
                halfLoadOption ? "ON" : "OFF",
                extraDryingOption ? "ON" : "OFF",
                delayHours);
            Serial.printf("Button ADCs: Start=%d, Half=%d, ExtraDry=%d, Delay=%d\n",
                                        settingsManager.get().startBtnAdc, settingsManager.get().halfLoadBtnAdc,
                                        settingsManager.get().extraDryBtnAdc, settingsManager.get().delayStartBtnAdc);
            Serial.print("Relays: ");
            for(int i = 0; i < NUM_RELAYS; i++) {
                char relayName[12];
                char* pgm_ptr = (char*)pgm_read_ptr(&(relayNames[i]));
                strncpy_P(relayName, pgm_ptr, sizeof(relayName));
                relayName[sizeof(relayName)-1] = '\0';
                Serial.printf("%s=%d ", relayName, digitalRead(relayPins[i]) == RELAY_ON ? 1 : 0);
            }
            Serial.println("\n--------------------");
            return;
        }
        
        if (command == "MEMORY") {
            printMemoryInfo();
            return;
        }

        if (command == "RESET") {
            softReset();
            return;
        }
        
        if (command == "TEST" && currentState == IDLE) {
            SelfTest::runDiagnostics();
            currentState = IDLE;
            return;
        }
        
        if (command == "SERIAL" && currentState == IDLE) {
            currentState = SERIAL_CONTROL;
            allRelaysOff();
            Serial.println(">>> SERIAL CONTROL MODE <<<");
            return;
        }

        if (command == "CALIBRATE BUTTONS" && currentState == IDLE) {
            currentState = CALIBRATION_BUTTONS;
            calibrationButtonIndex = 0;
            LOG_INFO("Entering button calibration mode.");
            return;
        }

        if (command == "CALIBRATE SELECTOR" && currentState == IDLE) {
            currentState = CALIBRATION_SELECTOR;
            LOG_INFO("Entering selector calibration mode. Send EXIT to stop.");
            return;
        }

        if (currentState == IDLE) {
            if (command.startsWith("P")) {
                int progNum = command.substring(1).toInt();
                if (progNum >= 1 && progNum <= numPrograms) {
                    selectedProgram = progNum;
                    char progName[17];
                    ProgramManager::getProgramName(selectedProgram - 1, progName, sizeof(progName));
                    Serial.printf("Program selected: %d (%s)\n", selectedProgram, progName);
                } else {
                    Serial.printf("Invalid program number (1-%d).\n", numPrograms);
                }
                return;
            }
            if (command == "START") {
                startProgram();
                return;
            }
            if (command == "HALF") {
                halfLoadOption = !halfLoadOption;
                Serial.printf("Half load: %s\n", halfLoadOption ? "ON" : "OFF");
                return;
            }
            if (command == "DRY") {
                extraDryingOption = !extraDryingOption;
                Serial.printf("Extra dry: %s\n", extraDryingOption ? "ON" : "OFF");
                return;
            }
            if (command.startsWith("DELAY")) {
                int hours = command.substring(5).toInt();
                if (hours >= 0 && hours <= 24) {
                    delayHours = hours;
                    Serial.printf("Delay set to: %d hours\n", delayHours);
                } else {
                    Serial.println("Invalid delay (0-24 hours).");
                }
                return;
            }
            #if SIMULATION_MODE
            if (command == "SIM_DOOR_OPEN") { sim_door_switch = false; Serial.println("SIM: Door OPEN"); return; }
            if (command == "SIM_DOOR_CLOSE") { sim_door_switch = true; Serial.println("SIM: Door CLOSED"); return; }
            if (command == "SIM_PRESSURE_ON") { sim_pressure_switch = true; Serial.println("SIM: Pressure Switch ON"); return; }
            if (command == "SIM_PRESSURE_OFF") { sim_pressure_switch = false; Serial.println("SIM: Pressure Switch OFF"); return; }
            if (command == "SIM_RINSE_AID_LOW") { sim_rinse_aid_low = true; Serial.println("SIM: Rinse Aid LOW"); return; }
            if (command == "SIM_RINSE_AID_OK") { sim_rinse_aid_low = false; Serial.println("SIM: Rinse Aid OK"); return; }
            if (command.startsWith("SIM_TEMP")) {
                sim_temperature = command.substring(8).toFloat();
                Serial.printf("SIM: Temperature set to %.1fC\n", sim_temperature);
                return;
            }
            if (command.startsWith("SIM_SEL")) {
                sim_selector_adc = command.substring(7).toInt();
                Serial.printf("SIM: Selector ADC set to %d\n", sim_selector_adc);
                return;
            }
            if (command.startsWith("SIM_BTN")) {
                sim_buttons_adc = command.substring(7).toInt();
                Serial.printf("SIM: Buttons ADC set to %d\n", sim_buttons_adc);
                return;
            }
            #endif
        }

        if (currentState == SERIAL_CONTROL || currentState == CALIBRATION_BUTTONS || currentState == CALIBRATION_SELECTOR) {
            if (command == "EXIT") {
                currentState = IDLE;
                allRelaysOff();
                LOG_INFO("Exited special mode.");
                return;
            }
            if (currentState == SERIAL_CONTROL) {
                int relayNum;
                char stateStr[4] = "";
                int parsedItems = sscanf(command.c_str(), "%d %s", &relayNum, stateStr);

                if (parsedItems == 2) {
                    if (relayNum >= 1 && relayNum <= NUM_RELAYS) {
                        String state = String(stateStr);
                        char relayName[12];
                        char* pgm_ptr = (char*)pgm_read_ptr(&(relayNames[relayNum - 1]));
                        strncpy_P(relayName, pgm_ptr, sizeof(relayName));
                        relayName[sizeof(relayName)-1] = '\0';

                        if (state == "ON") {
                            digitalWrite(relayPins[relayNum - 1], RELAY_ON);
                            Serial.printf("Relay %d (%s) -> ON\n", relayNum, relayName);
                        } else if (state == "OFF") {
                            digitalWrite(relayPins[relayNum - 1], RELAY_OFF);
                            Serial.printf("Relay %d (%s) -> OFF\n", relayNum, relayName);
                        } else {
                            Serial.println("Invalid state. Use ON or OFF.");
                        }
                    } else {
                        Serial.println("Invalid relay number (1-7).");
                    }
                } else {
                    Serial.println("Unknown command in SERIAL mode.");
                }
            }
            return;
        }
        
        LOG_WARN("Command not applicable in current state.");
    }
}

