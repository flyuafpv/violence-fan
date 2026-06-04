# 05 · Safety Features

Adds production-grade safety logic to the integrated motor + pot + VBAT sketch. Final stage before LED ring integration.

## Files

- `05-safety-features.ino` — full integration with arming sequence and fault latching

## Prerequisites

- `04-voltage-divider` working — VBAT readings accurate with two-point calibration

## Why This Sketch Exists

The earlier motor sketches spin the motor immediately when the battery is connected, regardless of pot position. If you plug in the battery with the pot at 50%, the motor jumps to 50% throttle instantly.

This is not safe for a finished product. Real flight controllers require an arming sequence: the user must explicitly move throttle to minimum before the motor will respond. Same principle applied here.

## Safety Features Added

### 1. ESC Arming Sequence

On power-on, the motor stays at minimum throttle regardless of pot position. The user must:

1. Move pot to MINIMUM (0%)
2. Hold at MIN for 500ms
3. After this, throttle control unlocks

Once armed, the system stays armed until power is cycled.

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

If battery voltage drops below 12.8V (3.2V/cell on 4S), the `faultLatched` flag is set permanently. Motor stops and stays stopped until power is cycled. Protects LiPo cells from over-discharge damage.

```cpp
if (!usbMode && vbatFiltered < VBAT_CUTOFF) {
  faultLatched = true;
}
```

Once latched, no amount of pot movement or voltage recovery resumes operation. The user must investigate, swap the battery, and power-cycle. This is intentional safe behaviour.

### 3. Low Battery Warning

Between 12.8V (cutoff) and 13.2V (warning), the OLED displays `LOW BAT`. Motor continues to run.

### 4. USB-Only Mode Detection

When testing without a battery, VBAT readings are phantom values that would trigger false faults. The code detects this state:

```cpp
bool usbMode = (vbatFiltered < VBAT_USB_MODE);  // 5.0V
if (!usbMode) {
  // safety checks
}
```

Display shows `VBAT: USB-ONLY`.

### 5. Visual State Feedback

The OLED clearly shows the current state at all times:

| State | Display |
|-------|---------|
| Booting (3 seconds) | "TURBINE BRICK / Initialising ESC..." |
| Disarmed, pot above min | "MOVE POT TO MIN" + current % |
| Disarmed, pot at min (counting) | "MOVE POT TO MIN" + progress bar filling |
| Armed, normal | "VBAT: 15.42V 3.86/c" + "THR: 65% 1650us" |
| Armed, low battery | "LOW BAT 65%" + throttle bar active |
| Fault (VBAT cutoff) | "!! CUTOFF 3.2V/c !!" + bar disabled |
| USB-only mode | "VBAT: USB-ONLY" + normal throttle behaviour |

## Hardware

Same as test 04 — no new components.

## Procedure — Verify Each Safety Feature

### Test 1 — Arming Sequence

1. Pot at MAX position
2. Connect battery
3. ESC arms (audible tones)
4. ESP32 boots → splash screen for 3 seconds
5. **Expected:** OLED shows "MOVE POT TO MIN", motor silent
6. Turn pot down to MIN
7. **Expected:** Progress bar fills during 500ms hold
8. **Expected:** Display switches to "THR: 0% 1000us"
9. Now turn pot up gradually → motor responds normally

### Test 2 — Persistent Arming

1. From armed state, turn pot up to ~50%, motor running
2. Turn pot back to 0%
3. Turn pot up again
4. **Expected:** Motor responds immediately (arming persists until power cycle)

### Test 3 — Disarm on Power Cycle

1. With armed system running, disconnect battery
2. Wait 5 seconds, reconnect battery
3. **Expected:** Back to "MOVE POT TO MIN" — must re-arm

### Test 4 — VBAT Cutoff

Requires bench PSU that can sweep voltage:

1. Power on with PSU at 15V → arm normally
2. Start motor at low throttle
3. Slowly drop PSU voltage toward 12.8V
4. **Expected:** Around 13.2V, display shows "LOW BAT" warning
5. **Expected:** Around 12.8V, display switches to "!! CUTOFF !!" and motor stops
6. Try turning pot — motor stays stopped (fault latched)
7. Raise PSU voltage back to 16V — motor remains stopped
8. Disconnect and reconnect → must re-arm

### Test 5 — USB-Only Mode

1. Disconnect battery completely
2. Power ESP32 from USB only
3. **Expected:** Display shows "VBAT: USB-ONLY"
4. Pot still requires arming
5. Once armed, throttle control works without VBAT fault triggers

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
  └── YES → continue
  ↓
Is faultLatched?
  ├── YES → ESC = MIN, show "CUTOFF" (until power cycle)
  └── NO  → continue
  ↓
Map pot % to ESC pulse → write to ESC → update display
  ↓
Loop again
```

## Configuration Constants

```cpp
#define ARM_HOLD_TIME    500   // ms pot at MIN before arming

#define RAW_MIN          200   // pot dead zone low
#define RAW_MAX          4000  // pot dead zone high

#define VBAT_LOW         13.20f  // 3.3V/cell
#define VBAT_CUTOFF      12.80f  // 3.2V/cell
#define VBAT_USB_MODE    5.00f

// From your calibration in 04-voltage-divider
#define VDIV_RATIO       5.25f
#define CAL_V_LOW        12.00f
#define CAL_V_HIGH       16.80f
#define CAL_R_LOW        12.59f
#define CAL_R_HIGH       17.16f
```

## What's NOT Included (Future Improvements)

For full production firmware:

- **Throttle ramp limiting** — smooth acceleration instead of instant changes
- **Watchdog timer** — force ESC to MIN if main loop hangs
- **ESC arm verification** — detect if ESC failed to arm
- **Over-temperature monitoring** — requires additional temperature sensor

These are nice-to-haves, not required for a desktop fan.

## Required Libraries

- Adafruit SSD1306
- Adafruit GFX Library
- ESP32Servo

## Next Step

Proceed to `06-led-test` to add WS2812B LED ring visual feedback.
