/*
 * Turbine Brick — Final Controller Firmware
 *
 * Brushless desktop fan controller with full safety logic.
 *
 * Hardware:
 *   - ESP32-C3 (USB CDC enabled in Arduino IDE)
 *   - SSD1306 128x32 OLED (I2C @ 0x3C)
 *   - 10kΩ linear potentiometer (GPIO3)
 *   - Voltage divider 10kΩ + 2kΩ + 100nF cap (GPIO4)
 *   - Brushless ESC with BEC (PWM signal on GPIO2)
 *   - WS2812B LED ring, 18 LEDs (data on GPIO10 via 330Ω resistor)
 *   - 4S LiPo battery
 *
 * Pin Assignments:
 *   GPIO2  - ESC PWM signal
 *   GPIO3  - POT wiper (ADC)
 *   GPIO4  - VBAT divider midpoint (ADC)
 *   GPIO8  - OLED SDA
 *   GPIO9  - OLED SCL
 *   GPIO10 - WS2812B data
 *
 * Arduino IDE Settings:
 *   Board: ESP32C3 Dev Module
 *   USB CDC On Boot: Enabled
 *   JTAG Adapter: Disabled
 *   Upload Speed: 921600
 *
 * Features:
 *   - ADC attenuation set for full 0-3.3V input range
 *   - Two-point voltage calibration with EMA smoothing
 *   - Dead zones on pot for clean 0% and 100%
 *   - ARMING SEQUENCE: motor disabled until pot held at MIN for 500ms
 *   - VBAT cutoff with fault latching (protects LiPo cells)
 *   - LOW BAT warning state
 *   - USB-only test mode detection
 *   - LED ring status indication
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <ESP32Servo.h>

// ============================================================================
//  PIN CONFIGURATION
// ============================================================================
#define PIN_ESC       2
#define PIN_POT       3
#define PIN_VBAT      4
#define PIN_OLED_SDA  8
#define PIN_OLED_SCL  9
#define PIN_LED_DATA  10

// ============================================================================
//  OLED
// ============================================================================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
#define OLED_ADDR     0x3C

// ============================================================================
//  LED RING
// ============================================================================
#define LED_COUNT     18
#define LED_BRIGHTNESS_NORMAL  80   // 0-255
#define LED_BRIGHTNESS_LOW     30
#define LED_BRIGHTNESS_FAULT   200

// ============================================================================
//  POT
// ============================================================================
#define POT_RAW_MIN   200    // dead zone below this = 0%
#define POT_RAW_MAX   4000   // dead zone above this = 100%
#define POT_SAMPLES   32

// ============================================================================
//  ESC
// ============================================================================
#define ESC_MIN_US    1000
#define ESC_MAX_US    2000

// ============================================================================
//  SAFETY
// ============================================================================
#define ARM_HOLD_TIME 500    // ms pot must stay at MIN before arming

// ============================================================================
//  VBAT
// ============================================================================
#define ADC_REF       3.3f
#define ADC_RES       4095.0f
#define VDIV_RATIO    5.25f
#define VBAT_SAMPLES  64

// Two-point voltage calibration — update with your measurements
// See 04-voltage-divider readme for calibration procedure
#define CAL_V_LOW     12.00f
#define CAL_V_HIGH    16.80f
#define CAL_R_LOW     12.59f
#define CAL_R_HIGH    17.16f

// 4S LiPo thresholds
#define VBAT_LOW      13.20f  // 3.30V/cell - warning
#define VBAT_CUTOFF   12.80f  // 3.20V/cell - hard stop
#define VBAT_USB_MODE 5.00f   // below this = USB-only mode (skip safety checks)

// ============================================================================
//  GLOBALS
// ============================================================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_NeoPixel ring(LED_COUNT, PIN_LED_DATA, NEO_GRB + NEO_KHZ800);
Servo esc;

float vbatFiltered = 15.0f;
unsigned long lastDisplay = 0;
unsigned long lastVbat = 0;
unsigned long lastSerial = 0;
unsigned long lastLedUpdate = 0;
unsigned long armingTimer = 0;
uint8_t ledPhase = 0;

bool armed = false;
bool faultLatched = false;

// ============================================================================
//  POT FUNCTIONS
// ============================================================================
int readPotRaw() {
  long sum = 0;
  for (int i = 0; i < POT_SAMPLES; i++) {
    sum += analogRead(PIN_POT);
    delayMicroseconds(100);
  }
  return sum / POT_SAMPLES;
}

int rawToPct(int raw) {
  if (raw < POT_RAW_MIN) return 0;
  if (raw > POT_RAW_MAX) return 100;
  return map(raw, POT_RAW_MIN, POT_RAW_MAX, 0, 100);
}

// ============================================================================
//  VBAT FUNCTIONS
// ============================================================================
float readVbatRaw() {
  long sum = 0;
  for (int i = 0; i < VBAT_SAMPLES; i++) {
    sum += analogRead(PIN_VBAT);
    delayMicroseconds(100);
  }
  float vadc = (sum / (float)VBAT_SAMPLES / ADC_RES) * ADC_REF;
  return vadc * VDIV_RATIO;
}

float readVbat() {
  float vbat_raw = readVbatRaw();

  // Two-point linear calibration
  float scale = (CAL_V_HIGH - CAL_V_LOW) / (CAL_R_HIGH - CAL_R_LOW);
  float offset = CAL_V_LOW - (CAL_R_LOW * scale);
  float vbat_corrected = (vbat_raw * scale) + offset;

  // EMA smoothing
  vbatFiltered = vbatFiltered * 0.8f + vbat_corrected * 0.2f;
  return vbatFiltered;
}

// ============================================================================
//  LED RING
// ============================================================================
void updateLedRing(bool usbMode, int throttlePct) {
  uint8_t r, g, b;
  uint8_t brightness = LED_BRIGHTNESS_NORMAL;

  if (faultLatched) {
    // FAULT — pulsing red
    brightness = LED_BRIGHTNESS_FAULT;
    uint8_t pulse = (sin(ledPhase * 0.05) + 1.0) * 127;
    r = pulse; g = 0; b = 0;
    for (int i = 0; i < LED_COUNT; i++) ring.setPixelColor(i, r, g, b);
  }
  else if (!armed) {
    // DISARMED — slow rotating amber
    brightness = LED_BRIGHTNESS_LOW;
    for (int i = 0; i < LED_COUNT; i++) {
      uint8_t intensity = ((ledPhase + i * (256 / LED_COUNT)) % 256);
      ring.setPixelColor(i, intensity, intensity / 2, 0);
    }
  }
  else if (!usbMode && vbatFiltered < VBAT_LOW) {
    // LOW BAT — orange pulse
    uint8_t pulse = (sin(ledPhase * 0.08) + 1.0) * 127;
    for (int i = 0; i < LED_COUNT; i++) ring.setPixelColor(i, pulse, pulse / 3, 0);
  }
  else {
    // ARMED — colour shifts with throttle (blue -> green -> red)
    if (throttlePct < 50) {
      r = 0;
      g = map(throttlePct, 0, 50, 0, 255);
      b = map(throttlePct, 0, 50, 255, 0);
    } else {
      r = map(throttlePct, 50, 100, 0, 255);
      g = map(throttlePct, 50, 100, 255, 0);
      b = 0;
    }
    for (int i = 0; i < LED_COUNT; i++) ring.setPixelColor(i, r, g, b);
  }

  ring.setBrightness(brightness);
  ring.show();
  ledPhase++;
}

// ============================================================================
//  DISPLAY
// ============================================================================
void updateDisplay(bool usbMode, int pct, int pulseUs, unsigned long now) {
  display.clearDisplay();
  display.setTextSize(1);

  // Line 1: VBAT or USB mode
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

  // Line 2: status
  display.setCursor(0, 10);
  if (faultLatched) {
    display.print("!! CUTOFF 3.2V/c !!");
  } else if (!armed) {
    display.print("MOVE POT TO MIN");
    display.setCursor(95, 10);
    display.print(pct);
    display.print("%");
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

  // Bar
  display.drawRect(0, 22, 100, 8, SSD1306_WHITE);
  if (!armed && pct == 0 && armingTimer > 0) {
    // Arming progress
    int armBarWidth = ((now - armingTimer) * 100) / ARM_HOLD_TIME;
    if (armBarWidth > 100) armBarWidth = 100;
    display.fillRect(0, 22, armBarWidth, 8, SSD1306_WHITE);
  } else if (armed && !faultLatched) {
    int barWidth = map(pct, 0, 100, 0, 100);
    display.fillRect(0, 22, barWidth, 8, SSD1306_WHITE);
  }

  display.setCursor(105, 23);
  if (!armed) display.print("ARM");
  else display.print(pct);

  display.display();
}

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);

  // OLED init
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed");
    while (1);
  }

  // ADC config — CRITICAL: without 11dB attenuation readings are non-linear
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_POT, ADC_11db);
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);

  // LED ring init
  ring.begin();
  ring.setBrightness(LED_BRIGHTNESS_NORMAL);
  ring.clear();
  ring.show();

  // Splash
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("TURBINE BRICK");
  display.println("Initialising ESC...");
  display.display();

  // ESC arming pulse — send MIN throttle for 3 seconds so ESC sees the signal
  esc.attach(PIN_ESC, ESC_MIN_US, ESC_MAX_US);
  esc.writeMicroseconds(ESC_MIN_US);
  delay(3000);
}

// ============================================================================
//  LOOP
// ============================================================================
void loop() {
  unsigned long now = millis();

  // Read inputs
  int raw = readPotRaw();
  int pct = rawToPct(raw);

  if (now - lastVbat >= 200) {
    lastVbat = now;
    readVbat();
  }

  bool usbMode = (vbatFiltered < VBAT_USB_MODE);

  // SAFETY: VBAT cutoff (only when battery connected)
  if (!usbMode && vbatFiltered < VBAT_CUTOFF) {
    faultLatched = true;
  }

  // SAFETY: arming sequence — pot must be at MIN for ARM_HOLD_TIME ms
  if (!armed) {
    if (pct == 0) {
      if (armingTimer == 0) {
        armingTimer = now;
      } else if (now - armingTimer >= ARM_HOLD_TIME) {
        armed = true;
      }
    } else {
      armingTimer = 0;  // reset timer if pot moves
    }
  }

  // Determine ESC output
  int pulseUs;
  if (faultLatched || !armed) {
    pulseUs = ESC_MIN_US;
  } else {
    pulseUs = map(pct, 0, 100, ESC_MIN_US, ESC_MAX_US);
  }
  esc.writeMicroseconds(pulseUs);

  // Display refresh (100ms)
  if (now - lastDisplay >= 100) {
    lastDisplay = now;
    updateDisplay(usbMode, pct, pulseUs, now);
  }

  // LED ring refresh (50ms — smooth animation)
  if (now - lastLedUpdate >= 50) {
    lastLedUpdate = now;
    updateLedRing(usbMode, pct);
  }

  // Serial log (100ms)
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

  delay(20);  // ~50Hz loop rate
}
