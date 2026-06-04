# 05 · Safety Features

Adds production-grade safety logic to the integrated motor + pot + VBAT sketch. This is the final stage before the full controller firmware — verifies all safety mechanisms work together.

## Files

- `safety_features.ino` — Full integration: OLED + pot + ESC + VBAT + arming sequence + fault latching

## Prerequisites

- `04-voltage-divider.md` working — VBAT readings accurate with two-point calibration

## Why This Sketch Exists

The earlier motor sketches will spin the motor immediately when the battery is connected, regardless of pot position. If you plug in the battery with the pot at 50%, the motor jumps to 50% throttle instantly — same as a drone with throttle stuck at half-up.

This is **not safe** for a finished product. Real flight controllers and ESCs require an **arming sequence**: the user must explicitly move the throttle to minimum before the motor will respond. Same principle applied here.

## Safety Features Added

### 1. ESC Arming Sequence

**Behaviour:** On power-on, the motor stays at minimum throttle regardless of pot position. The user must:

1. Move pot to MINIMUM (0%)
2. Hold it at MIN for **500ms**
3. After this, throttle control unlocks

Once armed, the system stays armed until power is cycled (or a fault is triggered).

**Implementation:** A boolean `armed` flag and an `armingTimer` that counts up while pot is at 0%. If the pot moves above 0% before 500ms passes, the timer resets.

```cpp
if (!armed) {
  if (pct == 0) {
    if (armingTimer == 0) armingTimer = now;
    else if (now - armingTimer >= ARM_HOLD_TIME) armed = true;
  } else {
    armingTimer = 0;  // reset if pot moves
  }
}
```

### 2. VBAT Cutoff with Fault Latching

If battery voltage drops below 12.8V (3.2V/cell on 4S), the `faultLatched` flag is set permanently. Motor stops and stays stopped until power is cycled. This protects LiPo cells from over-discharge damage.

```cpp
if (!usbMode && vbatFiltered < VBAT_CUTOFF) {
  faultLatched = true;
}
```

Once latched, no amount of pot movement or voltage recovery will resume motor operation — by design. The user must investigate, swap the battery, and power-cycle.

### 3. Low Battery Warning

Between 12.8V (cutoff) and 13.2V (warning threshold), the OLED displays a `LOW BAT` warning. Motor continues to run but the user knows to wrap up.

### 4. USB-Only Mode Detection

When testing without a battery (USB power to ESP32 only), VBAT readings are phantom values that would otherwise trigger false faults. The code detects this state and skips safety checks:

```cpp
bool usbMode = (vbatFiltered < VBAT_USB_MODE);  // VBAT_USB_MODE = 5.0V
if (!usbMode) {
  // safety checks
}
```

Display shows `VBAT: USB-ONLY` instead of voltage.

### 5. Visual State Feedback

The OLED clearly shows the current state at all times:

| State | Display |
|-------|---------|
| Booting (3 seconds) | "TURBINE BRICK v2 / Initialising ESC..." |
| Disarmed, pot above min | "MOVE POT TO MIN" + current % |
| Disarmed, pot at min (counting) | "MOVE POT TO MIN" + progress bar filling |
| Armed, normal operation | "VBAT: 15.42V 3.86/c" + "THR: 65% 1650us" |
| Armed, low battery warning | "LOW BAT 65%" + throttle bar still active |
| Fault (VBAT cutoff) | "!! CUTOFF 3.2V/c !!" + bar disabled |
| USB-only mode | "VBAT: USB-ONLY" + normal throttle behaviour |

### 6. ADC Hardening (from previous tests)

All earlier ADC fixes carry forward:

- 11dB attenuation on both ADC pins
- 32 samples averaged on pot, 64 on VBAT
- Two-point calibration on VBAT
- EMA smoothing filter
- Dead zones on pot (RAW_MIN=200, RAW_MAX=4000)

## Hardware

Same as test 04 — no new components needed:

- ESP32-C3
- SSD1306 128×32 OLED
- 10kΩ potentiometer
- Voltage divider (10kΩ + 2kΩ + 100nF cap)
- Skywalker 40A ESC
- 4S LiPo battery
- Brushless motor (prop OFF for testing)

## Procedure — Verify Each Safety Feature

### Test 1 — Arming Sequence

1. Pot at **MAX** position
2. Connect battery
3. ESC arms (audible tones)
4. ESP32 boots → splash screen for 3 seconds
5. **Expected:** OLED shows "MOVE POT TO MIN" + 100% indicator. Motor silent.
6. **Wrong if:** Motor spins. Means arming logic broken.
7. Turn pot down toward MIN
8. **Expected:** Display still shows "MOVE POT TO MIN" until pot reaches 0%
9. At 0%, progress bar starts filling
10. Hold for 500ms
11. **Expected:** Display switches to "THR: 0% 1000us", motor stays silent
12. Now turn pot up gradually → motor should respond normally

### Test 2 — Persistent Arming

1. From armed state, turn pot up to ~50%, motor running
2. Turn pot back to 0%
3. Turn pot up again
4. **Expected:** Motor responds immediately, no re-arming needed (arming persists until power cycle)

### Test 3 — Disarm on Power Cycle

1. With armed system running, disconnect battery
2. Wait 5 seconds
3. Reconnect battery
4. **Expected:** Back to "MOVE POT TO MIN" — must re-arm
5. This prevents accidental motor spin on battery reconnect

### Test 4 — VBAT Cutoff (simulated)

For this test you need either:
- A nearly-depleted 4S battery (risky, don't drain too far)
- A bench PSU that can sweep voltage down

1. Power on with battery/PSU at 15V → arm normally
2. Start motor at low throttle
3. Slowly drop PSU voltage toward 12.8V
4. **Expected:** Around 13.2V, display shows "LOW BAT" warning
5. **Expected:** Around 12.8V, display switches to "!! CUTOFF !!" and motor stops
6. Try turning pot — motor stays stopped (fault latched)
7. Raise PSU voltage back to 16V — motor remains stopped
8. **Expected:** Fault persists until power cycle. This is correct safe behaviour.
9. Disconnect and reconnect power → must re-arm with pot at MIN

### Test 5 — USB-Only Mode

1. Disconnect battery completely
2. Power ESP32 from USB only
3. **Expected:** Display shows "VBAT: USB-ONLY"
4. Pot still requires arming (pot to MIN held for 500ms)
5. Once armed, throttle control works
6. No fault triggers from phantom VBAT readings

## Safety Logic Flow

```
Power on
  ↓
ESP32 boots → ESC sends MIN pulse for 3 seconds
  ↓
Loop starts
  ↓
Read pot, read VBAT
  ↓
Is VBAT below cutoff (and not USB mode)?
  ├── YES → faultLatched = true
  └── NO  → continue
  ↓
Is system armed?
  ├── NO  → Force ESC = MIN, show "MOVE POT TO MIN"
  │         Check if pot at 0 for 500ms → set armed = true
  │
  └── YES → continue
  ↓
Is faultLatched?
  ├── YES → ESC = MIN, show "CUTOFF" (stays here until power cycle)
  └── NO  → continue
  ↓
Map pot % to ESC pulse → write to ESC → update display
  ↓
Loop again
```

## Configuration Constants

Update these to match your hardware/preferences:

```cpp
// Arming
#define ARM_HOLD_TIME    500   // ms pot at MIN before arming

// Pot dead zones
#define RAW_MIN          200   // tune to your pot's noise floor
#define RAW_MAX          4000  // tune to your pot's max stability

// VBAT thresholds (4S LiPo)
#define VBAT_LOW         13.20f
#define VBAT_CUTOFF      12.80f
#define VBAT_USB_MODE    5.00f

// VBAT calibration (run procedure in 04-voltage-divider.md)
#define VDIV_RATIO       5.25f
#define CAL_V_LOW        12.00f
#define CAL_V_HIGH       16.80f
#define CAL_R_LOW        12.59f
#define CAL_R_HIGH       17.16f
```

## What's NOT Included (Future Improvements)

For full production firmware, consider also adding:

- **Throttle ramp limiting** — prevent instant 0→100% transitions (smooth acceleration)
- **Watchdog timer** — force ESC to MIN if main loop hangs
- **ESC arm verification** — detect if ESC failed to arm (no response to throttle)
- **Over-temperature monitoring** — if you add a temperature sensor near ESC
- **State persistence** — remember last throttle setting across power cycles (not safe — better to start at 0)
- **Audio buzzer alerts** — beep on fault or low battery

These are nice-to-haves but not required for a desktop fan controller. Current safety level is appropriate for the use case.

## Required Libraries

- Adafruit SSD1306
- Adafruit GFX Library
- ESP32Servo

## YouTube Demonstration Tip

This sketch makes an excellent demonstration of engineering safety:

> "Watch what happens if I plug in the battery with the throttle at 50%..."
> *plug in battery* → motor stays silent, OLED says "MOVE POT TO MIN"
> "The controller refuses to arm until I've explicitly set throttle to zero. Same principle as a drone radio — you can't accidentally launch a spinning motor by reconnecting power."
> *move pot down* → progress bar fills → arming completes → "THR: 0%"
> *now turning pot up* → motor responds smoothly

Clearly visible safety behaviour, addresses any "is this safe?" comments preemptively.

## Next Step

This is the final standalone test. The next stage is integrating these safety features with:

- LED ring control (WS2812B on GPIO10) — status indication via colour
- Smooth animations during arming/running states
- Visual feedback on faults

See the main project firmware (in the parent repo) for the integrated version with LEDs.
