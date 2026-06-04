/*
 * 06 · LED Ring Test — Turbine Brick build sequence
 *
 * Adds WS2812B LED ring (18 LEDs) to the integrated controller from 05-safety-features.
 * Maps controller states to LED animations.
 *
 * Hardware:
 *   - ESP32-C3 with USB CDC enabled
 *   - SSD1306 128x32 OLED on GPIO8/9
 *   - 10kΩ pot on GPIO3
 *   - Voltage divider on GPIO4
 *   - ESC on GPIO2 (BEC powers ESP32)
 *   - WS2812B 18-LED strip on GPIO10 (via 330Ω resistor, 5V from BEC)
 *
 * LED states:
 *   FAULT      → pulsing red
 *   DISARMED   → rotating amber
 *   LOW BAT    → pulsing orange
 *   ARMED      → blue/green/red gradient based on throttle
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <ESP32Servo.h>

// ─── PINS ──────────────────────────────────────────────────────────
#define PIN_ESC       2
#define PIN_POT       3
#define PIN_VBAT      4
#define PIN_OLED_SDA  8
#define PIN_OLED_SCL  9
#define PIN_LED_DATA  10

// ─── OLED ──────────────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
#define OLED_ADDR     0x3C

// ─── LEDs ──────────────────────────────────────────────────────────
#define LED_COUNT     18
#define LED_BRI_NORM  80
#define LED_BRI_LOW   30
#define LED_BRI_FAULT 200

// ─── POT ───────────────────────────────────────────────────────────
#define POT_RAW_MIN   200
#define POT_RAW_MAX   4000
#define POT_SAMPLES   32

// ─── ESC ───────────────────────────────────────────────────────────
#define ESC_MIN_US    1000
#define ESC_MAX_US    2000

// ─── SAFETY ────────────────────────────────────────────────────────
#define ARM_HOLD_TIME 500

// ─── VBAT ──────────────────────────────────────────────────────────
#define ADC_REF       3.3f
#define ADC_RES       4095.0f
#define VDIV_RATIO    5.25f
#define VBAT_SAMPLES  64

// Update with your calibration values from 04-voltage-divider
#define CAL_V_LOW     12.00f
#define CAL_V_HIGH    16.80f
#define CAL_R_LOW     12.59f
#define CAL_R_HIGH    17.16f

#define VBAT_LOW      13.20f
#define VBAT_CUTOFF   12.80f
#define VBAT_USB_MODE 5.00f

// ─── GLOBALS ───────────────────────────────────────────────────────
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_NeoPixel ring(LED_COUNT, PIN_LED_DATA, NEO_GRB + NEO_KHZ800);
Servo esc;

float vbatFiltered = 15.0f;
unsigned long lastDisplay = 0;
unsigned long lastVbat = 0;
unsigned long lastSerial = 0;
unsigned long lastLed = 0;
unsigned long armingTimer = 0;
uint8_t ledPhase = 0;

bool armed = false;
bool faultLatched = false;

// ─── POT ───────────────────────────────────────────────────────────
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

// ─── VBAT ──────────────────────────────────────────────────────────
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
  float raw = readVbatRaw();
  float scale  = (CAL_V_HIGH - CAL_V_LOW) / (CAL_R_HIGH - CAL_R_LOW);
  float offset = CAL_V_LOW - (CAL_R_LOW * scale);
  float corrected = (raw * scale) + offset;
  vbatFiltered = vbatFiltered * 0.8f + corrected * 0.2f;
  return vbatFiltered;
}

// ─── LED STARTUP TEST ──────────────────────────────────────────────
void ledStartupTest() {
  // Cycle all LEDs through R, G, B at startup
  uint32_t colours[] = {
    ring.Color(255, 0, 0),
    ring.Color(0, 255, 0),
    ring.Color(0, 0, 255)
  };
  for (int c = 0; c < 3; c++) {
    for (int i = 0; i < LED_COUNT; i++) ring.setPixelColor(i, colours[c]);
    ring.show();
    delay(300);
  }
  ring.clear();
  ring.show();
}

// ─── LED RING ──────────────────────────────────────────────────────
void updateLedRing(bool usbMode, int throttlePct) {
  uint8_t brightness = LED_BRI_NORM;

  if (faultLatched) {
    brightness = LED_BRI_FAULT;
    uint8_t pulse = (sin(ledPhase * 0.05) + 1.0) * 127;
    for (int i = 0; i < LED_COUNT; i++) ring.setPixelColor(i, pulse, 0, 0);
  }
  else if (!armed) {
    brightness = LED_BRI_LOW;
    for (int i = 0; i < LED_COUNT; i++) {
      uint8_t intensity = ((ledPhase + i * (256 / LED_COUNT)) % 256);
      ring.setPixelColor(i, intensity, intensity / 2, 0);
    }
  }
  else if (!usbMode && vbatFiltered < VBAT_LOW) {
    uint8_t pulse = (sin(ledPhase * 0.08) + 1.0) * 127;
    for (int i = 0; i < LED_COUNT; i++) ring.setPixelColor(i, pulse, pulse / 3, 0);
  }
  else {
    uint8_t r, g, b;
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

// ─── DISPLAY ───────────────────────────────────────────────────────
void updateDisplay(bool usbMode, int pct, int pulseUs, unsigned long now) {
  display.clearDisplay();
  display.setTextSize(1);

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

  display.drawRect(0, 22, 100, 8, SSD1306_WHITE);
  if (!armed && pct == 0 && armingTimer > 0) {
    int w = ((now - armingTimer) * 100) / ARM_HOLD_TIME;
    if (w > 100) w = 100;
    display.fillRect(0, 22, w, 8, SSD1306_WHITE);
  } else if (armed && !faultLatched) {
    int w = map(pct, 0, 100, 0, 100);
    display.fillRect(0, 22, w, 8, SSD1306_WHITE);
  }

  display.setCursor(105, 23);
  if (!armed) display.print("ARM");
  else display.print(pct);

  display.display();
}

// ─── SETUP ─────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed");
    while (1);
  }

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_POT, ADC_11db);
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);

  ring.begin();
  ring.setBrightness(LED_BRI_NORM);
  ring.clear();
  ring.show();

  // Visual startup confirmation
  ledStartupTest();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("LED RING TEST");
  display.println("Initialising ESC...");
  display.display();

  esc.attach(PIN_ESC, ESC_MIN_US, ESC_MAX_US);
  esc.writeMicroseconds(ESC_MIN_US);
  delay(3000);
}

// ─── LOOP ──────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  int raw = readPotRaw();
  int pct = rawToPct(raw);

  if (now - lastVbat >= 200) {
    lastVbat = now;
    readVbat();
  }

  bool usbMode = (vbatFiltered < VBAT_USB_MODE);

  if (!usbMode && vbatFiltered < VBAT_CUTOFF) {
    faultLatched = true;
  }

  if (!armed) {
    if (pct == 0) {
      if (armingTimer == 0) armingTimer = now;
      else if (now - armingTimer >= ARM_HOLD_TIME) armed = true;
    } else {
      armingTimer = 0;
    }
  }

  int pulseUs;
  if (faultLatched || !armed) pulseUs = ESC_MIN_US;
  else pulseUs = map(pct, 0, 100, ESC_MIN_US, ESC_MAX_US);
  esc.writeMicroseconds(pulseUs);

  if (now - lastDisplay >= 100) {
    lastDisplay = now;
    updateDisplay(usbMode, pct, pulseUs, now);
  }

  if (now - lastLed >= 50) {
    lastLed = now;
    updateLedRing(usbMode, pct);
  }

  if (now - lastSerial >= 100) {
    lastSerial = now;
    Serial.print("raw="); Serial.print(raw);
    Serial.print(" pct="); Serial.print(pct);
    Serial.print(" us="); Serial.print(pulseUs);
    Serial.print(" vbat="); Serial.print(vbatFiltered, 2); Serial.print("V");
    if (!armed) Serial.print(" [DISARMED]");
    if (armed) Serial.print(" [ARMED]");
    if (faultLatched) Serial.print(" [FAULT]");
    if (usbMode) Serial.print(" [USB]");
    Serial.println();
  }

  delay(20);
}
