/*
 * Turbine Brick v2 — Motor + VBAT Integration Test
 * 
 * Hardware:
 *   - ESP32-C3 (USB CDC enabled)
 *   - SSD1306 128x32 OLED (I2C, addr 0x3C)
 *   - 10kΩ potentiometer on GPIO3
 *   - Voltage divider on GPIO4 (10kΩ + 2kΩ)
 *   - Skywalker 40A ESC on GPIO2
 *   - 4S LiPo battery (or bench PSU)
 * 
 * Features:
 *   - ADC attenuation set for full 0-3.3V input range
 *   - Two-point voltage calibration
 *   - EMA filter on VBAT for stable readings
 *   - Dead zones on pot for clean 0% and 100%
 *   - 100nF cap on GPIO4 recommended for noise filtering
 *   - 3.2V/cell cutoff for 4S LiPo safety
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

// ───────── VBAT CONFIG ─────────
#define ADC_REF       3.3f
#define ADC_RES       4095.0f
#define VDIV_RATIO    5.25f   // base divider ratio (R1+R2)/R2

// Two-point voltage calibration — UPDATE THESE WITH YOUR MEASUREMENTS
#define CAL_V_LOW     12.90f   // discharged pack, SkyRC reading
#define CAL_V_HIGH    16.00f   // charged pack, SkyRC reading
#define CAL_R_LOW     12.25f   // firmware raw reading at 12.90V
#define CAL_R_HIGH    15.53f   // firmware raw reading at 16.00V

// 4S LiPo voltage thresholds
#define VBAT_LOW      13.20f  // 3.30V/cell — warning
#define VBAT_CUTOFF   12.80f  // 3.20V/cell — hard stop
#define VBAT_USB_MODE 5.00f   // below this = USB-only, skip safety checks

// ───────── GLOBALS ─────────
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Servo esc;

float vbatFiltered = 15.0f;   // EMA filter state
unsigned long lastDisplay = 0;
unsigned long lastVbat = 0;
bool faultLatched = false;    // once cutoff triggers, stay cut off

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
  
  // EMA filter (heavy smoothing for stable display)
  vbatFiltered = vbatFiltered * 0.8f + vbat_corrected * 0.2f;
  return vbatFiltered;
}

// ───────── SETUP ─────────
void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);
  
  // OLED init
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
  display.println("MOTOR + VBAT TEST");
  display.println("Pot to MIN to arm");
  display.display();
  
  // ESC arming
  esc.attach(PIN_ESC, ESC_MIN_US, ESC_MAX_US);
  esc.writeMicroseconds(ESC_MIN_US);
  delay(3000);  // ESC arming tones play during this time
}

// ───────── LOOP ─────────
void loop() {
  unsigned long now = millis();
  
  // Read pot continuously
  int raw = readPotRaw();
  int pct = rawToPct(raw);
  
  // Read voltage every 200ms (EMA filter handles the smoothing)
  if (now - lastVbat >= 200) {
    lastVbat = now;
    readVbat();  // updates vbatFiltered
  }
  
  // Determine if we're in USB-only mode (no battery)
  bool usbMode = (vbatFiltered < VBAT_USB_MODE);
  
  // Safety checks (only if battery connected)
  if (!usbMode) {
    if (vbatFiltered < VBAT_CUTOFF) {
      faultLatched = true;
    }
  }
  
  // Determine throttle output
  int pulseUs;
  if (faultLatched) {
    pulseUs = ESC_MIN_US;  // forced minimum
  } else {
    pulseUs = map(pct, 0, 100, ESC_MIN_US, ESC_MAX_US);
  }
  esc.writeMicroseconds(pulseUs);
  
  // Update display every 100ms
  if (now - lastDisplay >= 100) {
    lastDisplay = now;
    
    display.clearDisplay();
    display.setTextSize(1);
    
    // Line 1: VBAT
    display.setCursor(0, 0);
    if (usbMode) {
      display.print("VBAT: USB-ONLY");
    } else {
      display.print("VBAT: ");
      display.print(vbatFiltered, 2);
      display.print("V ");
      // Per-cell voltage (4S)
      display.print(vbatFiltered / 4.0f, 2);
      display.print("/c");
    }
    
    // Line 2: status / throttle
    display.setCursor(0, 10);
    if (faultLatched) {
      display.print("!! CUTOFF - 3.2V/c !!");
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
    
    // Throttle bar
    display.drawRect(0, 22, 100, 8, SSD1306_WHITE);
    int barWidth = map(pct, 0, 100, 0, 100);
    if (!faultLatched) {
      display.fillRect(0, 22, barWidth, 8, SSD1306_WHITE);
    }
    display.setCursor(105, 23);
    display.print(pct);
    
    display.display();
  }
  
  // Serial log (every loop iteration is too much — log every 100ms)
  static unsigned long lastSerial = 0;
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
    if (faultLatched) Serial.print(" [FAULT]");
    if (usbMode) Serial.print(" [USB]");
    Serial.println();
  }
  
  delay(20);  // ~50Hz loop — appropriate for ESC PWM
}