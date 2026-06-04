/*
 * Turbine Brick v2 — Motor + VBAT Integration Test
 * WITH SAFETY ARMING SEQUENCE
 * 
 * Hardware:
 *   - ESP32-C3 (USB CDC enabled in Arduino IDE)
 *   - SSD1306 128x32 OLED (I2C, addr 0x3C)
 *   - 10kΩ potentiometer on GPIO3
 *   - Voltage divider on GPIO4 (10kΩ + 2kΩ + 100nF cap)
 *   - Skywalker 40A ESC on GPIO2
 *   - 4S LiPo battery (or bench PSU)
 * 
 * Features:
 *   - ADC attenuation set for full 0-3.3V input range
 *   - Two-point voltage calibration
 *   - EMA filter on VBAT for stable readings
 *   - Dead zones on pot for clean 0% and 100%
 *   - 100nF cap on GPIO4 for noise filtering
 *   - 3.2V/cell cutoff for 4S LiPo safety (12.8V total)
 *   - ARMING SEQUENCE: motor won't respond until pot held at MIN for 500ms
 *   - Fault latching: once VBAT cutoff triggers, stays cut off until power cycle
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>

// ───────── PIN CONFIG ─────────
#define PIN_POT       3
#define PIN_ESC       2
#define PIN_VBAT      4

// ───────── OLED CONFIG ─────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
#define OLED_ADDR     0x3C

// ───────── POT CONFIG ─────────
#define RAW_MIN       200    // dead zone below this = 0%
#define RAW_MAX       4000   // dead zone above this = 100%

// ───────── ESC CONFIG ─────────
#define ESC_MIN_US    1000
#define ESC_MAX_US    2000

// ───────── ARMING CONFIG ─────────
#define ARM_HOLD_TIME 500    // ms pot must stay at MIN before arming

// ───────── VBAT CONFIG ─────────
#define ADC_REF       3.3f
#define ADC_RES       4095.0f
#define VDIV_RATIO    5.25f

// Two-point voltage calibration — UPDATE WITH YOUR MEASUREMENTS
#define CAL_V_LOW     12.00f
#define CAL_V_HIGH    16.80f
#define CAL_R_LOW     12.59f
#define CAL_R_HIGH    17.16f

// 4S LiPo voltage thresholds
#define VBAT_LOW      13.20f  // 3.30V/cell — warning
#define VBAT_CUTOFF   12.80f  // 3.20V/cell — hard stop
#define VBAT_USB_MODE 5.00f   // below this = USB-only mode

// ───────── GLOBALS ─────────
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Servo esc;

float vbatFiltered = 15.0f;
unsigned long lastDisplay = 0;
unsigned long lastVbat = 0;
unsigned long lastSerial = 0;
unsigned long armingTimer = 0;

bool armed = false;
bool faultLatched = false;

// ───────── POT FUNCTIONS ─────────
int readPotRaw() {
  long sum = 0;
  for (int i = 0; i < 32; i++) {
    sum += analogRead(PIN_POT);
    delayMicroseconds(100);
  }
  return sum / 32;
}

int rawToPct(int raw) {
  if (raw < RAW_MIN) return 0;
  if (raw > RAW_MAX) return 100;
  return map(raw, RAW_MIN, RAW_MAX, 0, 100);
}

// ───────── VBAT FUNCTIONS ─────────
float readVbatRaw() {
  long sum = 0;
  for (int i = 0; i < 64; i++) {
    sum += analogRead(PIN_VBAT);
    delayMicroseconds(100);
  }
  float vadc = (sum / 64.0f / ADC_RES) * ADC_REF;
  return vadc * VDIV_RATIO;
}

float readVbat() {
  float vbat_raw = readVbatRaw();
  
  // Two-point linear calibration
  float scale = (CAL_V_HIGH - CAL_V_LOW) / (CAL_R_HIGH - CAL_R_LOW);
  float offset = CAL_V_LOW - (CAL_R_LOW * scale);
  float vbat_corrected = (vbat_raw * scale) + offset;
  
  // EMA filter
  vbatFiltered = vbatFiltered * 0.8f + vbat_corrected * 0.2f;
  return vbatFiltered;
}

// ───────── SETUP ─────────
void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed");
    while (1);
  }
  
  // ADC config — CRITICAL for correct readings
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_POT, ADC_11db);
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);
  
  // Splash screen
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("TURBINE BRICK v2");
  display.println("Initialising ESC...");
  display.display();
  
  // ESC arming pulse — sends MIN throttle for 3 seconds
  esc.attach(PIN_ESC, ESC_MIN_US, ESC_MAX_US);
  esc.writeMicroseconds(ESC_MIN_US);
  delay(3000);
}

// ───────── LOOP ─────────
void loop() {
  unsigned long now = millis();
  
  // Read pot continuously
  int raw = readPotRaw();
  int pct = rawToPct(raw);
  
  // Read voltage every 200ms
  if (now - lastVbat >= 200) {
    lastVbat = now;
    readVbat();
  }
  
  bool usbMode = (vbatFiltered < VBAT_USB_MODE);
  
  // ───────── SAFETY CHECKS ─────────
  
  // 1. VBAT cutoff (only when battery connected)
  if (!usbMode && vbatFiltered < VBAT_CUTOFF) {
    faultLatched = true;
  }
  
  // 2. ARMING SEQUENCE — must see pot at MIN held for 500ms before allowing throttle
  if (!armed) {
    if (pct == 0) {
      // Pot is at min — start or continue arming timer
      if (armingTimer == 0) {
        armingTimer = now;
      } else if (now - armingTimer >= ARM_HOLD_TIME) {
        armed = true;
      }
    } else {
      // Pot moved away from min — reset timer
      armingTimer = 0;
    }
  }
  
  // ───────── DETERMINE ESC OUTPUT ─────────
  int pulseUs;
  
  if (faultLatched) {
    pulseUs = ESC_MIN_US;  // VBAT cutoff — forced minimum
  } else if (!armed) {
    pulseUs = ESC_MIN_US;  // Not armed yet — forced minimum
  } else {
    pulseUs = map(pct, 0, 100, ESC_MIN_US, ESC_MAX_US);
  }
  esc.writeMicroseconds(pulseUs);
  
  // ───────── DISPLAY ─────────
  if (now - lastDisplay >= 100) {
    lastDisplay = now;
    
    display.clearDisplay();
    display.setTextSize(1);
    
    // Line 1: VBAT (or USB mode)
    display.setCursor(0, 0);
    if (usbMode) {
      display.print("VBAT: USB-ONLY");
    } else {
      display.print("VBAT: ");
      display.print(vbatFiltered, 2);
      display.print("V ");
      display.print(vbatFiltered / 4.0f, 2);
      display.print("/c");
    }
    
    // Line 2: Status / throttle
    display.setCursor(0, 10);
    if (faultLatched) {
      display.print("!! CUTOFF 3.2V/c !!");
    } else if (!armed) {
      // Show arming progress
      display.print("MOVE POT TO MIN");
      if (pct == 0 && armingTimer > 0) {
        // Show progress bar during arm hold
        int armProgress = ((now - armingTimer) * 100) / ARM_HOLD_TIME;
        display.setCursor(95, 10);
        display.print(armProgress);
        display.print("%");
      } else {
        display.setCursor(95, 10);
        display.print(pct);
        display.print("%");
      }
    } else if (!usbMode && vbatFiltered < VBAT_LOW) {
      display.print("LOW BAT ");
      display.print(pct);
      display.print("%");
    } else {
      display.print("THR: ");
      display.print(pct);
      display.print("% ");
      display.print(pulseUs);
      display.print("us");
    }
    
    // Throttle bar (or arming bar)
    display.drawRect(0, 22, 100, 8, SSD1306_WHITE);
    if (!armed && pct == 0 && armingTimer > 0) {
      // Show arming progress as bar
      int armBarWidth = ((now - armingTimer) * 100) / ARM_HOLD_TIME;
      if (armBarWidth > 100) armBarWidth = 100;
      display.fillRect(0, 22, armBarWidth, 8, SSD1306_WHITE);
    } else if (armed && !faultLatched) {
      int barWidth = map(pct, 0, 100, 0, 100);
      display.fillRect(0, 22, barWidth, 8, SSD1306_WHITE);
    }
    
    display.setCursor(105, 23);
    if (!armed) {
      display.print("ARM");
    } else {
      display.print(pct);
    }
    
    display.display();
  }
  
  // ───────── SERIAL LOG ─────────
  if (now - lastSerial >= 100) {
    lastSerial = now;
    Serial.print("raw=");
    Serial.print(raw);
    Serial.print(" pct=");
    Serial.print(pct);
    Serial.print(" us=");
    Serial.print(pulseUs);
    Serial.print(" vbat=");
    Serial.print(vbatFiltered, 2);
    Serial.print("V");
    if (!armed) Serial.print(" [DISARMED]");
    if (armed) Serial.print(" [ARMED]");
    if (faultLatched) Serial.print(" [FAULT]");
    if (usbMode) Serial.print(" [USB]");
    Serial.println();
  }
  
  delay(20);  // ~50Hz loop
}