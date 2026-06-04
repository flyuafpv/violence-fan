# 03 · Motor Test

Adds ESC and brushless motor control to the OLED + potentiometer setup. Potentiometer controls motor speed from 0-100%.

## Safety First

Read before wiring:

- **Remove the propeller from the motor** before any testing
- **Secure the motor** so it cannot move around — it jumps on startup
- **Keep hands clear** of the motor during testing
- **Pot to MIN before connecting battery** — prevents unexpected ESC calibration mode
- **Verify ADC attenuation is set in code** — without it, pot reads ~50% at "min" position and the motor will spin

## Files

- `03-motor-test.ino` — OLED + pot + ESC control

## Prerequisites

- `01-oled-hello-test` and `02-pot-test` working with confirmed dead zone values

## Hardware

- Everything from test 02
- Brushless ESC with BEC (e.g. Skywalker 40A)
- Brushless motor (e.g. P1604 3800KV)
- 4S LiPo battery + matching connector (XT60 or XT30)
- 3× motor wires (ESC to motor)

## Wiring

### ESC Connections

| ESC Wire | Goes To |
|----------|---------|
| BAT+ (red, thick) | Battery + |
| BAT− (black, thick) | Battery − |
| Motor A/B/C (3× yellow) | Motor 3 wires (any order — swap any 2 to reverse direction) |
| BEC + (red, thin) | ESP32-C3 5V pin |
| BEC − (black, thin) | ESP32-C3 GND |
| Signal (white/yellow) | ESP32-C3 GPIO2 |

### USB and Battery Power

**Do not have both USB and BEC power connected simultaneously** unless your ESP32-C3 dev board has reverse-current protection on the USB Vbus rail. Most modern boards have this — check yours.

Safe workflow during development:

1. Disconnect battery
2. Plug in USB
3. Flash code
4. Unplug USB
5. Plug in battery (BEC supplies power)

## Critical ADC Setup

The sketch includes these lines in `setup()`:

```cpp
analogReadResolution(12);
analogSetPinAttenuation(PIN_POT, ADC_11db);
```

Without them, pot readings are non-linear and "min" position may register as 25-50% throttle — the motor will spin at "zero" position. This is a safety issue, not just an annoyance.

## Procedure

1. Verify prop is OFF the motor and motor is secured
2. Verify ADC attenuation lines are present in `setup()`
3. Upload `03-motor-test.ino` via USB
4. Unplug USB
5. Pot to MINIMUM (full counter-clockwise)
6. Connect battery to ESC
7. Listen for ESC startup tones — series of beeps confirming cell count and arm sequence
8. ESC finds the MIN throttle signal → arms ready (final confirmation tone)
9. Slowly turn pot up — motor should spin from 0% (silent) up to 100% (full speed)

## Expected Behaviour

| Throttle % | Motor State | PWM Pulse |
|------------|-------------|-----------|
| 0% | Silent, still | 1000μs |
| 5-10% | Starts spinning slowly | ~1050-1100μs |
| 50% | Mid-speed | 1500μs |
| 100% | Full speed | 2000μs |

## ESC Calibration (do once on first power-on)

Most ESCs come pre-calibrated for 1000-2000μs range. Manual calibration ensures perfect throttle response:

1. Prop OFF the motor
2. Battery DISCONNECTED. Pot to MAXIMUM
3. Connect battery → ESC enters calibration mode (specific beep pattern — check ESC manual)
4. Wait 2-3 seconds, then turn pot to MINIMUM
5. ESC confirms with a "calibration complete" tone
6. Disconnect battery. Calibration is stored permanently.

Future power-ons skip calibration and go straight to normal operation.

## Troubleshooting

| Problem | Likely Cause |
|---------|--------------|
| Motor spins immediately at "min" pot | ADC attenuation not set — pot reading wrong, sending ~50% throttle |
| Motor doesn't spin at all | ESC arming incomplete — pot to MIN, then up slowly again |
| Motor twitches but won't spin smoothly | Phase wires loose, or throttle jumping past min |
| Motor spins wrong direction | Swap any 2 of the 3 motor phase wires |
| ESC enters calibration mode unexpectedly | Pot was at max when battery connected — disconnect, pot to min, reconnect |
| Motor runs at 100% from start | Pot wiring backwards — swap the two outer pot pins |

## Panic Stop

If anything goes wrong:

- Turn pot to MIN → motor stops
- Or unplug battery → kills everything

## Required Libraries

- Adafruit SSD1306
- Adafruit GFX Library
- **ESP32Servo**

## Next Step

Once motor responds smoothly to pot, proceed to `04-voltage-divider`.
