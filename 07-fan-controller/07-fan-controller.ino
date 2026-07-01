/*
 * 07 · Final Fan Controller — Turbine Brick
 *
 * Brushless desktop fan controller with full safety logic, LED ring,
 * and post-session statistics (Betaflight-style).
 *
 * Hardware:
 *   - ESP32-C3 (USB CDC enabled)
 *   - SSD1309 128×64 OLED (I2C @ 0x3C)
 *   - 10kΩ linear potentiometer
 *   - Voltage divider 10kΩ + 2kΩ + 100nF cap
 *   - Brushless ESC with BEC
 *   - WS2812B LED ring, 18 LEDs (via 330Ω resistor)
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
 * Screen States:
 *   - SPLASH      — boot, 3 seconds during ESC arming
 *   - FAULT       — VBAT cutoff triggered, must power-cycle
 *   - LIVE        — armed, throttle active
 *   - STATS       — disarmed after a session, shows last session stats
 *   - DISARMED    — disarmed with no session data (first boot)
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <ESP32Servo.h>

// ─── PIN CONFIG ────────────────────────────────────────────────────
#define PIN_ESC       2
#define PIN_POT       3
#define PIN_VBAT      4
#define PIN_OLED_SDA  8
#define PIN_OLED_SCL  9
#define PIN_LED_DATA  10

// ─── OLED ──────────────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDR     0x3C

// ─── LED RING ──────────────────────────────────────────────────────
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
#define ARM_HOLD_TIME     500    // ms pot must hold at MIN before arming
#define DISARM_HOLD_TIME  3000   // ms pot must hold at MIN (while armed) to disarm

// ─── VBAT ──────────────────────────────────────────────────────────
#define ADC_REF       3.3f
#define ADC_RES       4095.0f
#define VDIV_RATIO    5.25f
#define VBAT_SAMPLES  64

// Two-point calibration — see 04-voltage-divider readme for procedure
#define CAL_V_LOW     12.90f   // discharged pack, SkyRC reading
#define CAL_V_HIGH    16.00f   // charged pack, SkyRC reading
#define CAL_R_LOW     12.25f   // firmware raw reading at 12.90V
#define CAL_R_HIGH    15.53f   // firmware raw reading at 16.00V

// 4S LiPo thresholds
#define VBAT_LOW      13.20f
#define VBAT_CUTOFF   12.80f
#define VBAT_USB_MODE 5.00f

// ─── GLOBALS ───────────────────────────────────────────────────────
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_NeoPixel ring(LED_COUNT, PIN_LED_DATA, NEO_GRB + NEO_KHZ800);
Servo esc;

// ─── STATE MACHINE ─────────────────────────────────────────────────
// BOOT       → splash screen during ESC init
// DISARMED   → waiting for first arm (no stats yet); arm by holding MIN
// ARMED      → motor live, throttle active
// STATS      → showing last session stats after a disarm
// FAULT      → VBAT cutoff latched, requires power cycle
//
// Re-arm guard: after disarming, the pot is sitting at MIN. To prevent
// instant re-arm, the user must move the pot AWAY from MIN at least once
// (armReady flag) before a new arming hold is accepted.
enum State { BOOT, DISARMED, ARMED, STATS, FAULT };
State state = BOOT;

float vbatFiltered = 15.0f;
unsigned long lastDisplay = 0;
unsigned long lastVbat = 0;
unsigned long lastSerial = 0;
unsigned long lastLed = 0;
unsigned long lastStatsUpdate = 0;
unsigned long armingTimer = 0;
unsigned long disarmTimer = 0;
unsigned long statsShownAt = 0;       // when STATS screen first displayed
uint16_t ledPhase = 0;

bool armReady = false;                 // pot has moved away from MIN since last disarm
bool throttleEngaged = false;          // pot has moved above MIN since arming (enables disarm)
bool faultLatched = false;

#define STATS_MIN_DISPLAY 10000        // ms — show stats at least this long

// ─── SESSION STATS ─────────────────────────────────────────────────
struct SessionStats {
  unsigned long armedStartMs;     // millis() when armed
  unsigned long durationMs;       // total time armed in this session
  uint32_t throttleSampleSum;     // sum of all throttle % samples
  uint32_t throttleSampleCount;   // number of samples
  uint8_t peakThrottle;           // highest throttle seen
  float minVbat;                  // lowest VBAT seen while armed
  bool hasData;                   // true if a session has completed
};

SessionStats stats = {0, 0, 0, 0, 0, 99.9f, false};

void resetStats() {
  stats.armedStartMs = millis();
  stats.durationMs = 0;
  stats.throttleSampleSum = 0;
  stats.throttleSampleCount = 0;
  stats.peakThrottle = 0;
  stats.minVbat = 99.9f;
  // hasData stays as it was — we still want to show previous session
  //   until a new one has data; will set true once samples accumulate
}

void updateStats(int pct, bool usbMode) {
  if (state != ARMED || faultLatched) return;

  stats.durationMs = millis() - stats.armedStartMs;
  stats.throttleSampleSum += pct;
  stats.throttleSampleCount++;
  if (pct > stats.peakThrottle) stats.peakThrottle = pct;
  if (!usbMode && vbatFiltered < stats.minVbat) stats.minVbat = vbatFiltered;
  stats.hasData = true;
}

uint8_t avgThrottle() {
  if (stats.throttleSampleCount == 0) return 0;
  return stats.throttleSampleSum / stats.throttleSampleCount;
}

void formatTime(char* buf, size_t bufsize, unsigned long ms) {
  unsigned long totalSec = ms / 1000;
  unsigned long min = totalSec / 60;
  unsigned long sec = totalSec % 60;
  snprintf(buf, bufsize, "%02lu:%02lu", min, sec);
}

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
  float vbat_raw = readVbatRaw();
  float scale  = (CAL_V_HIGH - CAL_V_LOW) / (CAL_R_HIGH - CAL_R_LOW);
  float offset = CAL_V_LOW - (CAL_R_LOW * scale);
  float vbat_corrected = (vbat_raw * scale) + offset;
  vbatFiltered = vbatFiltered * 0.8f + vbat_corrected * 0.2f;
  return vbatFiltered;
}

// ─── LED RING ──────────────────────────────────────────────────────
// ledPhase is a free-running counter. For the rainbow we use it mod 256.
// For sin() pulses we scale it into radians; using a uint16_t (wraps at
// 65535) keeps the sine smooth far longer than a uint8_t would.
void updateLedRing(bool usbMode, int throttlePct) {
  uint8_t brightness = LED_BRI_NORM;

  if (faultLatched) {
    // FAULT — urgent red breathing
    brightness = LED_BRI_FAULT;
    uint8_t pulse = (sin(ledPhase * 0.05f) + 1.0f) * 127;
    for (int i = 0; i < LED_COUNT; i++) ring.setPixelColor(i, pulse, 0, 0);
  }
  else if (state == STATS) {
    // STATS — calm cyan breathing to signal "session ended, review stats"
    brightness = LED_BRI_LOW;
    uint8_t pulse = (sin(ledPhase * 0.04f) + 1.0f) * 127;
    for (int i = 0; i < LED_COUNT; i++) ring.setPixelColor(i, 0, pulse, pulse);
  }
  else if (state != ARMED) {
    // DISARMED / BOOT — slow amber rotating rainbow
    brightness = LED_BRI_LOW;
    for (int i = 0; i < LED_COUNT; i++) {
      uint8_t intensity = (((ledPhase >> 1) + i * (256 / LED_COUNT)) % 256);
      ring.setPixelColor(i, intensity, intensity / 2, 0);
    }
  }
  else if (!usbMode && vbatFiltered < VBAT_LOW) {
    // LOW BATTERY (while armed) — amber warning pulse
    uint8_t pulse = (sin(ledPhase * 0.08f) + 1.0f) * 127;
    for (int i = 0; i < LED_COUNT; i++) ring.setPixelColor(i, pulse, pulse / 3, 0);
  }
  else {
    // ARMED, normal — throttle-mapped colour (blue → green → red)
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

// ═══════════════════════════════════════════════════════════════════
//   DISPLAY SCREENS
// ═══════════════════════════════════════════════════════════════════

// LIVE screen — shown while armed and running
void drawLiveScreen(bool usbMode, int pct, int pulseUs, bool lowBat, unsigned long now) {
  display.clearDisplay();
  char timeStr[8];
  formatTime(timeStr, sizeof(timeStr), stats.durationMs);

  // ── Row 1: VBAT (large) + time ──
  display.setTextSize(2);
  display.setCursor(0, 0);
  if (usbMode) {
    display.print("USB");
  } else {
    display.print(vbatFiltered, 2);
    display.print("V");
  }

  display.setTextSize(1);
  display.setCursor(92, 0);
  display.print(timeStr);

  // ── Row 2: per-cell + status ──
  display.setTextSize(1);
  display.setCursor(0, 18);
  if (usbMode) {
    display.print("No battery");
  } else {
    display.print(vbatFiltered / 4.0f, 2);
    display.print("V/c  ");
    if (lowBat) display.print("LOW BAT");
    else        display.print("OK");
  }

  // Divider
  display.drawLine(0, 28, 128, 28, SSD1306_WHITE);

  // ── Row 3: Throttle (large) ──
  display.setTextSize(2);
  display.setCursor(0, 32);
  display.print("THR ");
  display.print(pct);
  display.print("%");

  // ── Row 4: PWM + AVG, or disarm hint if pot at 0% ──
  display.setTextSize(1);
  display.setCursor(0, 50);
  if (pct == 0 && disarmTimer > 0) {
    // Show disarm countdown
    unsigned long elapsed = now - disarmTimer;
    float remaining = (DISARM_HOLD_TIME - elapsed) / 1000.0f;
    if (remaining < 0) remaining = 0;
    display.print("Disarming in ");
    display.print(remaining, 1);
    display.print("s");
  } else {
    display.print("PWM ");
    display.print(pulseUs);
    display.print("  AVG ");
    display.print(avgThrottle());
    display.print("%");
  }

  // ── Row 5: Bar (throttle OR disarm progress) ──
  display.drawRect(0, 58, 100, 6, SSD1306_WHITE);
  if (pct == 0 && disarmTimer > 0) {
    // Show disarm progress filling up
    int w = ((now - disarmTimer) * 100) / DISARM_HOLD_TIME;
    if (w > 100) w = 100;
    display.fillRect(0, 58, w, 6, SSD1306_WHITE);
  } else {
    int barWidth = map(pct, 0, 100, 0, 100);
    display.fillRect(0, 58, barWidth, 6, SSD1306_WHITE);
  }

  // State label
  display.setCursor(105, 56);
  display.print("ARM");

  display.display();
}

// STATS screen — shown when disarmed AFTER a session
void drawStatsScreen() {
  display.clearDisplay();
  char timeStr[8];
  formatTime(timeStr, sizeof(timeStr), stats.durationMs);

  // Title
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("LAST SESSION");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  // Time
  display.setCursor(0, 14);
  display.print("Time     ");
  display.print(timeStr);

  // Average throttle
  display.setCursor(0, 24);
  display.print("Avg      ");
  display.print(avgThrottle());
  display.print("%");

  // Peak throttle
  display.setCursor(0, 34);
  display.print("Peak     ");
  display.print(stats.peakThrottle);
  display.print("%");

  // Minimum VBAT
  display.setCursor(0, 44);
  display.print("Min VBAT ");
  if (stats.minVbat < 99.0f) {
    display.print(stats.minVbat, 2);
    display.print("V");
  } else {
    display.print("--");
  }

  // Bottom hint
  display.drawLine(0, 54, 128, 54, SSD1306_WHITE);
  display.setCursor(0, 56);
  display.print("Pot to MIN to re-arm");

  display.display();
}

// DISARMED screen — first boot, no session data yet
void drawDisarmedScreen(int pct, unsigned long now) {
  display.clearDisplay();

  display.setTextSize(2);
  display.setCursor(10, 6);
  display.println("DISARMED");

  display.setTextSize(1);
  display.setCursor(0, 28);
  display.println("Move pot to MIN");
  display.setCursor(0, 40);
  display.println("to arm motor.");

  // Arming progress bar
  display.drawRect(0, 54, 100, 10, SSD1306_WHITE);
  if (pct == 0 && armingTimer > 0) {
    int w = ((now - armingTimer) * 100) / ARM_HOLD_TIME;
    if (w > 100) w = 100;
    display.fillRect(0, 54, w, 10, SSD1306_WHITE);
  }

  display.setCursor(108, 56);
  display.print(pct);
  display.print("%");

  display.display();
}

// FAULT screen — VBAT cutoff triggered
void drawFaultScreen() {
  display.clearDisplay();

  display.setTextSize(2);
  display.setCursor(10, 4);
  display.println("CUTOFF");

  display.setTextSize(1);
  display.setCursor(0, 26);
  display.print("Battery below ");
  display.print(VBAT_CUTOFF, 1);
  display.println("V");
  display.setCursor(0, 38);
  display.print("(3.2V/cell). Motor");
  display.setCursor(0, 50);
  display.print("locked. Power-cycle.");

  display.display();
}

// ═══════════════════════════════════════════════════════════════════
//   SETUP
// ═══════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed");
    while (1);
  }

  // ADC — CRITICAL: 11dB attenuation required
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_POT, ADC_11db);
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);

  ring.begin();
  ring.setBrightness(LED_BRI_NORM);
  ring.clear();
  ring.show();

  // Splash
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 12);
  display.println("TURBINE");
  display.setCursor(0, 32);
  display.println("BRICK");
  display.setTextSize(1);
  display.setCursor(0, 54);
  display.print("Initialising ESC...");
  display.display();

  esc.attach(PIN_ESC, ESC_MIN_US, ESC_MAX_US);
  esc.writeMicroseconds(ESC_MIN_US);
  delay(3000);

  // Move out of BOOT into DISARMED. First-ever arm doesn't require the
  // "move pot away from MIN first" guard, so seed armReady = true.
  state = DISARMED;
  armReady = true;
}

// ═══════════════════════════════════════════════════════════════════
//   LOOP
// ═══════════════════════════════════════════════════════════════════

void loop() {
  unsigned long now = millis();

  int raw = readPotRaw();
  int pct = rawToPct(raw);

  if (now - lastVbat >= 200) {
    lastVbat = now;
    readVbat();
  }

  bool usbMode = (vbatFiltered < VBAT_USB_MODE);

  // SAFETY: VBAT cutoff — overrides everything, latches until power cycle
  if (!usbMode && vbatFiltered < VBAT_CUTOFF) {
    faultLatched = true;
    state = FAULT;
  }

  // ─── STATE MACHINE ───────────────────────────────────────────────
  switch (state) {

    case BOOT:
      // Handled in setup(); on first loop entry, move to DISARMED.
      state = DISARMED;
      armReady = true;          // first-ever arm doesn't need the away-from-MIN guard
      break;

    case DISARMED:
      // Arm by holding pot at MIN — but only once the pot is "ready"
      // (it has been away from MIN since the last disarm, or this is first boot).
      if (pct > 0) {
        armReady = true;        // pot moved away from MIN; arming now allowed
        armingTimer = 0;
      } else if (armReady) {
        // pot at MIN and we're allowed to arm
        if (armingTimer == 0) armingTimer = now;
        else if (now - armingTimer >= ARM_HOLD_TIME) {
          state = ARMED;
          resetStats();
          throttleEngaged = false;   // must move pot up before disarm is possible
          armingTimer = 0;
          disarmTimer = 0;
        }
      }
      // If !armReady and pct==0, do nothing — wait for pot to move away first.
      break;

    case ARMED:
      // Once the pot moves above MIN after arming, enable the disarm gesture.
      if (pct > 0) throttleEngaged = true;

      // Disarm by holding pot at MIN for DISARM_HOLD_TIME — but only after
      // throttle has been engaged at least once (prevents instant disarm if
      // the user keeps holding MIN right after arming).
      if (pct == 0 && throttleEngaged) {
        if (disarmTimer == 0) disarmTimer = now;
        else if (now - disarmTimer >= DISARM_HOLD_TIME) {
          state = STATS;
          statsShownAt = now;
          armReady = false;     // require pot to leave MIN before re-arming
          disarmTimer = 0;
          armingTimer = 0;
        }
      } else {
        disarmTimer = 0;
      }
      break;

    case STATS:
      // Show stats for at least STATS_MIN_DISPLAY, then allow re-arm.
      // Re-arm requires the pot to first move away from MIN (armReady),
      // which is naturally satisfied since the user must move the pot.
      if (pct > 0) {
        armReady = true;
        armingTimer = 0;
      }
      // After the minimum display time, transition to DISARMED so the
      // normal arming flow can take over (which shows the stats-less
      // disarmed screen only if no stats — here stats persist, so we
      // keep showing STATS until re-armed).
      if (armReady && pct == 0 && (now - statsShownAt >= STATS_MIN_DISPLAY)) {
        if (armingTimer == 0) armingTimer = now;
        else if (now - armingTimer >= ARM_HOLD_TIME) {
          state = ARMED;
          resetStats();
          throttleEngaged = false;
          armingTimer = 0;
          disarmTimer = 0;
        }
      }
      break;

    case FAULT:
      // Latched — nothing resets this except power cycle.
      break;
  }

  // ESC output
  int pulseUs;
  if (state != ARMED) pulseUs = ESC_MIN_US;
  else pulseUs = map(pct, 0, 100, ESC_MIN_US, ESC_MAX_US);
  esc.writeMicroseconds(pulseUs);

  // Update session stats while armed (every 100ms = 10 Hz)
  if (now - lastStatsUpdate >= 100) {
    lastStatsUpdate = now;
    updateStats(pct, usbMode);
  }

  // Display refresh — choose screen based on state
  if (now - lastDisplay >= 100) {
    lastDisplay = now;

    bool lowBat = (!usbMode && vbatFiltered < VBAT_LOW);

    switch (state) {
      case FAULT:    drawFaultScreen();                              break;
      case ARMED:    drawLiveScreen(usbMode, pct, pulseUs, lowBat, now); break;
      case STATS:    drawStatsScreen();                              break;
      case DISARMED: drawDisarmedScreen(pct, now);                   break;
      default:       drawDisarmedScreen(pct, now);                   break;
    }
  }

  // LED ring refresh
  if (now - lastLed >= 50) {
    lastLed = now;
    updateLedRing(usbMode, pct);
  }

  // Serial log
  if (now - lastSerial >= 100) {
    lastSerial = now;
    Serial.print("raw="); Serial.print(raw);
    Serial.print(" pct="); Serial.print(pct);
    Serial.print(" us="); Serial.print(pulseUs);
    Serial.print(" vbat="); Serial.print(vbatFiltered, 2); Serial.print("V");
    switch (state) {
      case BOOT:     Serial.print(" [BOOT]");     break;
      case DISARMED: Serial.print(" [DISARMED]"); break;
      case STATS:    Serial.print(" [STATS]");    break;
      case FAULT:    Serial.print(" [FAULT]");    break;
      case ARMED:
        Serial.print(" [ARMED ");
        Serial.print(stats.durationMs / 1000);
        Serial.print("s avg=");
        Serial.print(avgThrottle());
        Serial.print("% peak=");
        Serial.print(stats.peakThrottle);
        Serial.print("%]");
        break;
    }
    if (usbMode) Serial.print(" [USB]");
    Serial.println();
  }

  delay(20);
}
