# 07 · Final Fan Controller

The complete, integrated controller firmware with all features from tests 01-06 plus post-session statistics (Betaflight-style flight stats).

## Files

- `07-fan-controller.ino` — final integrated firmware for the 128×64 SSD1309 OLED

## Prerequisites

- All previous tests (01-06) verified working
- SSD1309 128×64 OLED installed (or any SSD1306-compatible 128×64 display)
- Voltage divider calibrated for your specific ESP32-C3

## What's New in This Sketch

Compared to test 05/06, this version adds:

### Bigger Display Layout

Designed for the SSD1309 128×64 screen. Full layout takes advantage of the doubled vertical space:

- **Live screen** (when armed) — VBAT + per-cell, throttle %, PWM, average throttle, and progress bar
- **Stats screen** (when disarmed after a session) — last session summary with time, average, peak, min VBAT
- **Fault screen** (VBAT cutoff) — clear message explaining the cutoff and required action
- **Disarmed screen** (first boot, no session yet) — arming instructions with progress bar

### Session Statistics

Tracks 4 metrics per "session" (defined as the time between arming and the next power cycle):

| Metric | Description |
|--------|-------------|
| **Time** | Total armed duration, in mm:ss |
| **Avg throttle** | Mean throttle % across the session |
| **Peak throttle** | Highest throttle % reached |
| **Min VBAT** | Lowest battery voltage observed |

Stats are sampled at 10Hz while armed. They reset at the moment of arming (the transition from disarmed to armed).

### Workflow

1. Power on → splash screen for 3 seconds while ESC arms
2. **DISARMED** screen prompts user to move pot to MIN
3. Hold pot at MIN for 500ms → motor arms, **LIVE** screen shows
4. **LIVE** screen shows throttle, VBAT, time, average throttle, and bar
5. To stop the fan and see stats: **hold pot at MIN for 3 seconds** while armed → **STATS** screen
6. Stats stay on screen for at least 10 seconds and remain until you re-arm
7. To re-arm: move pot away from MIN, then hold it back at MIN for 500ms → new session, stats reset
8. VBAT < 12.8V at any time → **FAULT** screen (requires power-cycle)

### State Machine

The controller runs an explicit state machine to prevent edge-case bugs (like instantly re-arming after a disarm):

| State | Description | Exit condition |
|-------|-------------|----------------|
| **BOOT** | Splash during ESC init | Auto → DISARMED after 3s |
| **DISARMED** | Waiting for first arm | Hold MIN 500ms → ARMED |
| **ARMED** | Motor live, throttle active | Hold MIN 3s → STATS |
| **STATS** | Showing last session | Re-arm (see below) → ARMED |
| **FAULT** | VBAT cutoff latched | Power cycle only |

### Re-Arm Guard (prevents the disarm/re-arm loop)

When you disarm, the pot is sitting at MIN — which would normally satisfy the arm condition and instantly re-arm. To prevent this, an **`armReady` guard** requires the pot to **move away from MIN at least once** before a new arm is accepted.

So the re-arm sequence is:

1. Disarm by holding MIN for 3s → STATS screen
2. Stats display for minimum 10 seconds
3. **Move pot up** (away from MIN) — this "unlocks" arming
4. **Bring pot back to MIN** and hold 500ms → re-arms, new session

### First-Arm Behaviour

- On first boot, `armReady` is seeded `true`, so the very first arm works immediately (no need to wiggle the pot first).
- The disarm gesture only becomes active **after the throttle has been moved above MIN at least once** following an arm (`throttleEngaged` guard). This prevents an instant disarm countdown if you keep holding MIN right after arming.

### Disarm Gesture Detail

To stop the fan and view session stats without unplugging the battery:

1. While armed (and after having used some throttle), turn pot to MIN (0%)
2. **Hold at MIN for 3 seconds**
3. The LIVE screen shows "Disarming in X.Xs" with a filling progress bar replacing the throttle bar
4. After 3 seconds → motor disarms, STATS screen appears
5. If the pot moves away from MIN before 3 seconds, the countdown cancels

The 3-second hold is deliberately longer than the 500ms arming hold — arming is rare and intentional, but the pot might briefly hit 0% during normal use, so disarm is made slower to avoid accidental triggering.

## Screen Layouts

### Live Screen (armed)

```
┌──────────────────────────────┐
│ 15.42V              12:34    │  VBAT (big) + time
│ 3.86V/c  OK                  │  per-cell + status
├──────────────────────────────┤
│ THR 65%                      │  throttle (big)
│ PWM 1650  AVG 42%            │  PWM + average
│                              │
│ ████████████░░░░░░░ ARM      │  progress bar + state
└──────────────────────────────┘
```

### Stats Screen (disarmed after session)

```
┌──────────────────────────────┐
│ LAST SESSION                 │
│ ─────────────────────────    │
│ Time     12:34               │
│ Avg      42%                 │
│ Peak     89%                 │
│ Min VBAT 14.62V              │
│ ─────────────────────────    │
│ Pot to MIN to re-arm         │
└──────────────────────────────┘
```

### Fault Screen (VBAT cutoff)

```
┌──────────────────────────────┐
│ CUTOFF                       │
│                              │
│ Battery below 12.8V          │
│ (3.2V/cell). Motor           │
│ locked. Power-cycle.         │
│                              │
└──────────────────────────────┘
```

### Disarmed Screen (first boot, no session)

```
┌──────────────────────────────┐
│ DISARMED                     │
│                              │
│ Move pot to MIN              │
│ to arm motor.                │
│                              │
│ ████████████░░░░░░░  52%     │
└──────────────────────────────┘
```

## Configuration

All tunable constants are at the top of the sketch:

```cpp
// Pot dead zones — tune to your specific pot
#define POT_RAW_MIN       200
#define POT_RAW_MAX       4000

// Arming
#define ARM_HOLD_TIME     500

// VBAT calibration (from 04-voltage-divider procedure)
#define VDIV_RATIO        5.25f
#define CAL_V_LOW         12.00f
#define CAL_V_HIGH        16.80f
#define CAL_R_LOW         12.59f
#define CAL_R_HIGH        17.16f

// 4S LiPo thresholds
#define VBAT_LOW          13.20f  // 3.30V/cell warning
#define VBAT_CUTOFF       12.80f  // 3.20V/cell hard stop
#define VBAT_USB_MODE     5.00f   // below = USB-only mode

// LED brightness
#define LED_BRI_NORM      80
#define LED_BRI_LOW       30
#define LED_BRI_FAULT     200
```

## Required Libraries

- Adafruit SSD1306 (compatible with SSD1309)
- Adafruit GFX Library
- Adafruit NeoPixel
- ESP32Servo

## Serial Output

Logs at 10 Hz with rich state info:

```
raw=2240 pct=51 us=1510 vbat=14.83V [ARMED 124s avg=48% peak=82%]
```

State flags shown in brackets:
- `[DISARMED]` — system not armed
- `[ARMED Xs avg=Y% peak=Z%]` — armed with live session stats
- `[FAULT]` — VBAT cutoff triggered (latched)
- `[USB]` — USB-only mode (no battery connected)

## Potential Extensions

Things you might add later:

- **Energy estimate (mWh)** — multiply avg throttle × time × VBAT to estimate battery used
- **Cumulative runtime** — persist total runtime across power cycles to flash
- **Multiple session history** — store last 5 sessions in flash
- **Audio buzzer** — startup melody, low-battery warning beeps

## Notes

This is the final standalone sketch. After verification, it becomes the production firmware for the fan.
