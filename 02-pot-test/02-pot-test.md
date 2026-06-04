# 02 · Potentiometer Test

Reads the 10kΩ potentiometer with the ESP32-C3 ADC, displays the value live on the OLED, and tests dead-zone behaviour at both extremes.

## Files

- `pot_test.ino` — OLED + pot with configurable dead zones

## Prerequisites

- `01-oled-hello-test.md` working (OLED confirmed at 0x3C)

## Hardware

- Everything from test 01
- 10kΩ linear potentiometer (3 pins: left, wiper, right)
- 3× jumper wires

## Wiring

| Pot Pin     | ESP32-C3 Pin | Notes |
|-------------|--------------|-------|
| Left (or right) | 3V3      | One outer pin |
| Wiper (middle)  | GPIO3    | ADC input |
| Right (or left) | GND      | Other outer pin |

It doesn't matter which outer pin goes to 3V3 vs GND — it just reverses the direction of the knob. Swap them if min/max feel backwards.

## Critical ADC Configuration

ESP32-C3 ADC requires explicit attenuation setting for the 0-3.3V input range. **Without this, readings will be saturated/non-linear:**

```cpp
analogReadResolution(12);
analogSetPinAttenuation(PIN_POT, ADC_11db);
```

Add these lines in `setup()` after `Wire.begin()`. The sketch already includes them.

## Procedure

1. Wire the pot as above
2. Upload `pot_test.ino`
3. Open Serial Monitor at 115200 baud
4. Turn the pot through its full range — observe raw values, percentages, and the bar on the OLED

## Expected Results

| Pot Position | OLED Shows |
|--------------|------------|
| Full one way | `RAW:0` to `RAW:200`, `0%`, `[MIN DEAD ZONE]`, empty bar |
| Slightly turned | `RAW:300+`, `1-99%`, `ACTIVE`, bar partially filled |
| Full other way | `RAW:4000+`, `100%`, `[MAX DEAD ZONE]`, full bar |

With proper ADC attenuation, raw values should range from near 0 to near 4095 across the full pot rotation.

## Tuning Dead Zones

The default values in the sketch (`RAW_MIN=200`, `RAW_MAX=4000`) are starting points. Tune them to your specific pot:

1. Turn pot to minimum extreme — watch the `raw` value in Serial Monitor
2. Note the maximum raw value seen (e.g., 170-180 jitter peak)
3. Set `RAW_MIN` to 20-30 above that peak (e.g., 200 if jitter peaks at 180)

Same approach for max:

1. Turn pot to maximum — note the minimum raw value seen
2. Set `RAW_MAX` to 20-30 below that (e.g., 4000 if max stabilises at 4030)

Recompile and reflash with the tuned values.

## Why Dead Zones Matter

ADC readings naturally jitter at the extremes due to electrical noise. A pot at "min" might bounce between raw=170 and raw=180. Without dead zones, the percentage flickers between 0% and 1%. Dead zones map all readings below `RAW_MIN` to a solid 0%, eliminating flicker.

This is **critical for the motor control sketch** — the ESC needs a clean "min throttle" signal to arm. Without dead zones, the ESC may interpret throttle as "above zero" and refuse to arm.

## Troubleshooting

| Problem | Likely Cause |
|---------|--------------|
| Raw values stuck around 1000-2000, not full range | ADC attenuation not set — add `analogSetPinAttenuation(PIN_POT, ADC_11db)` |
| Raw values don't change when pot turned | Pot wiring issue — verify all 3 pins connected, check wiper continuity with multimeter |
| Pot reverses direction | Outer pins swapped — physically swap 3V3 and GND on pot outer pins |
| Min/max percentage flickers between 0/1 or 99/100 | Dead zone too narrow — bump `RAW_MIN` up or `RAW_MAX` down |

## Required Libraries

- Adafruit SSD1306
- Adafruit GFX Library

## Next Step

Once pot readings are stable with clean 0% and 100% at extremes, move to `03-motor-test.md`.
