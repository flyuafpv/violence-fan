# 03 · Motor Test

Adds ESC + motor control to the OLED + potentiometer setup. Pot controls motor speed from 0-100%.

⚠️ **SAFETY FIRST — READ BEFORE WIRING:**

- **Remove the propeller from the motor** before any testing. A spinning prop on an unsecured motor can cause injury.
- **Secure the motor** so it can't move around. Clamp it or hold the mount firmly — the motor will jump on startup.
- **Keep hands clear** of the motor during testing.
- **Pot to MIN before connecting battery** — ensures ESC doesn't enter calibration mode unexpectedly.
- **Verify ADC attenuation is set in code** — without it, pot readings are wrong and motor may spin at "min" position.

## Files

- `motor_test.ino` — OLED + pot + ESC control

## Prerequisites

- `01-oled-hello-test.md` working
- `02-pot-test.md` working with confirmed dead zone values

## Hardware

- Everything from test 02
- Skywalker 40A ESC (or any 4S-compatible ESC with BEC)
- Brushless motor (e.g. XING2/T-Motor P1604 3800KV)
- 4S LiPo battery + matching connector (XT60 or XT30)
- 3× motor wires (ESC to motor)

## Wiring

### ESC Connections

| ESC Wire | Goes To |
|----------|---------|
| BAT+ (red, thick) | Battery + (XT60/XT30 +) |
| BAT− (black, thick) | Battery − (XT60/XT30 −) |
| Motor A/B/C (3× yellow) | 3 motor wires (any order — swap any 2 to reverse direction later) |
| BEC + (red, thin) | ESP32-C3 5V pin |
| BEC − (black, thin) | ESP32-C3 GND |
| Signal (white/yellow) | ESP32-C3 GPIO2 |

### USB and Battery Power

**Do NOT have both USB and BEC power connected simultaneously** unless your ESP32-C3 board has reverse-current protection (a diode on the USB Vbus rail). Most modern boards have this but check yours.

Safe workflow during development:

1. Disconnect battery
2. Plug in USB
3. Flash code
4. Unplug USB
5. Plug in battery (BEC supplies power)

## Critical ADC Setup

The motor_test.ino sketch includes:

```cpp
analogReadResolution(12);
analogSetPinAttenuation(PIN_POT, ADC_11db);
```

Without these lines, pot readings are non-linear and "min" position may register as 25-50% throttle — **the motor will spin at "zero" position**. This is a safety issue, not just an annoyance.

## Procedure

1. Verify prop is OFF the motor and motor is secured
2. Verify ADC attenuation lines are present in `setup()`
3. Upload `motor_test.ino` via USB
4. **Unplug USB**
5. **Pot to MINIMUM** (full counter-clockwise)
6. **Connect battery to ESC** — ESC powers up
7. Listen for ESC startup tones — usually a series of beeps confirming cell count and arm sequence
8. ESC finds the MIN throttle signal → arms ready (final confirmation tone)
9. **Slowly turn pot up** — motor should spin from 0% (silent) up to 100% (full speed)

## Expected Behaviour

| Throttle % | Motor State | PWM Pulse |
|------------|-------------|-----------|
| 0% | Silent, still | 1000μs |
| 5-10% | Starts spinning slowly | ~1050-1100μs |
| 50% | Mid-speed | 1500μs |
| 100% | Full speed | 2000μs |

## ESC Calibration (do once on first power-on)

Most ESCs come pre-calibrated for 1000-2000μs range, but manual calibration ensures perfect throttle response:

1. **Prop OFF** the motor.
2. Battery **DISCONNECTED**. Pot to **MAXIMUM**.
3. **Connect battery** → ESC enters calibration mode (specific beep pattern — check Skywalker manual).
4. **Wait 2-3 seconds**, then turn pot to **MINIMUM**.
5. ESC confirms with a "calibration complete" tone.
6. **Disconnect battery**. Calibration is now stored permanently.
7. Future power-ons skip calibration and go straight to normal operation.

## Troubleshooting

| Problem | Likely Cause |
|---------|--------------|
| Motor spins immediately at "min" pot | ADC attenuation not set — pot reading wrong, sending ~50% throttle to ESC |
| Motor doesn't spin at all | ESC arming incomplete — turn pot to MIN, then up slowly again |
| Motor twitches but won't spin smoothly | Phase wires loose, or throttle jumping past min |
| Motor spins wrong direction | Swap any 2 of the 3 motor phase wires (no code change needed) |
| ESC enters calibration mode unexpectedly | Pot was at max when battery connected — disconnect, pot to min, reconnect |
| Motor runs at 100% from start | Pot wiring backwards — swap the two outer pot pins |

## Panic Stop

If anything goes wrong:

- Turn pot to MIN immediately → motor stops
- Or unplug battery → kills everything

## Required Libraries

- Adafruit SSD1306
- Adafruit GFX Library
- **ESP32Servo** (provides Servo class for ESP32)

## Next Step

Once motor responds smoothly to pot, move to `04-voltage-divider.md`.
