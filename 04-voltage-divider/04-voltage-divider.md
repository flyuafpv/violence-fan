# 04 · Voltage Divider Test

Adds battery voltage monitoring to the motor + pot test. Reads 4S LiPo voltage through a resistor divider and displays it on the OLED alongside throttle.

## Files

- `04-voltage-divider.ino` — full integration: OLED + pot + ESC + battery monitor with two-point calibration and EMA filtering

## Prerequisites

- `03-motor-test` working — motor spins from 0-100% via pot

## Hardware

- Everything from test 03
- 10kΩ resistor (R1 — high side)
- 2kΩ resistor (R2 — low side)
- 100nF ceramic capacitor (marking "104") — for noise filtering
- Small protoboard or solder + heat-shrink

## Theory

A 4S LiPo can reach 16.8V at full charge. The ESP32-C3 ADC max input is 3.3V. The divider scales battery voltage down by a factor of ~6.

With R1 = 10kΩ and R2 = 2kΩ:

```
Divider ratio (Vadc/Vbat) = R2 / (R1 + R2) = 0.1667
Inverse (for code): V_bat = V_adc × 6.0  (theoretical)
```

**In practice the effective ratio is around 5.0-5.3** due to ESP32-C3 ADC non-linearity. Two-point calibration in code corrects this.

## Critical ADC Setup

```cpp
analogReadResolution(12);
analogSetPinAttenuation(PIN_VBAT, ADC_11db);
analogSetPinAttenuation(PIN_POT, ADC_11db);
```

Add in `setup()` after `Wire.begin()`. The sketch includes these. Without 11dB attenuation, readings are non-linear.

## 4S LiPo Voltage Thresholds

| State | Per Cell | Total (4S) | Action |
|-------|----------|------------|--------|
| Full charge | 4.20V | 16.80V | Normal operation |
| Nominal | 3.70V | 14.80V | Normal operation |
| Low warning | 3.30V | 13.20V | Reduce throttle, display warning |
| Cutoff | 3.20V | 12.80V | Stop motor, latch fault |
| Damage zone | 3.00V | 12.00V | Below this = battery damaged |

3.2V/cell is the proper LiPo safety threshold. Below 3.0V/cell, cells suffer permanent damage.

## Wiring

```
BAT+ ──┬──[10kΩ R1]──┬──[2kΩ R2]── GND
                     │
                     ├── GPIO4 (PIN_VBAT)
                     │
                     └──[100nF cap]── GND
```

| Component | Notes |
|-----------|-------|
| R1 (10kΩ) | One end to BAT+, other to junction |
| R2 (2kΩ) | One end to junction, other to GND |
| 100nF cap | Junction to GND — noise filter, place close to ESP32 |
| Junction | Connects to GPIO4 |

**R2 must be the 2kΩ on the GND side.** Reversed resistors give the wrong ratio and could push >3.3V into the ADC, damaging the chip.

## Pre-Power-On Verification

Before connecting LiPo for the first time:

1. Set multimeter to DC voltage
2. Connect a 12V test source (bench PSU, 9V battery, etc.) to the divider
3. Measure between divider junction and GND
4. Reading should be approximately 2.0V (12V × 2k/12k)
5. If much higher (e.g., 6V or 12V), wiring is wrong — do not connect to ESP32

The ADC pin has an absolute max of ~3.6V. Exceeding this damages the ESP32-C3.

## Two-Point Calibration Procedure

The ESP32-C3 ADC is non-linear and has chip-to-chip variation. Two-point calibration gives ±0.1V accuracy at calibration points.

### Step 1 — Set calibration to identity

In the sketch:

```cpp
#define CAL_V_LOW     0.0f
#define CAL_V_HIGH    1.0f
#define CAL_R_LOW     0.0f
#define CAL_R_HIGH    1.0f
```

These produce scale=1, offset=0 — no correction applied. The displayed value equals raw reading.

### Step 2 — Take raw measurements at two known voltages

Use a bench PSU or other stable DC source:

1. Set source to 12.00V (verify with multimeter)
2. Flash with identity calibration
3. Wait 15 seconds for EMA filter to settle
4. Record the OLED reading → this is your **R_LOW**

5. Set source to 16.80V
6. Wait 15 seconds for EMA to settle
7. Record the OLED reading → this is your **R_HIGH**

Typical raw readings:
- At 12.00V actual → ~12.5-12.7V displayed
- At 16.80V actual → ~17.1-17.2V displayed

### Step 3 — Update calibration constants

```cpp
#define CAL_V_LOW     12.00f
#define CAL_V_HIGH    16.80f
#define CAL_R_LOW     12.59f   // your measured R_LOW
#define CAL_R_HIGH    17.16f   // your measured R_HIGH
```

### Step 4 — Verify

Reflash and check:
- PSU at 12.00V → OLED reads 12.0V ±0.1V
- PSU at 16.80V → OLED reads 16.8V ±0.1V
- Mid-range (14.80V) shows ±0.4V error — ADC non-linearity, acceptable

Endpoint accuracy will be excellent. Mid-range non-linearity is inherent to the ESP32-C3 ADC.

## EMA Filter

ESP32-C3 ADC readings jitter naturally. Exponential moving average smooths the display:

```cpp
vbatFiltered = vbatFiltered * 0.8f + vbat_corrected * 0.2f;
```

- 80% old reading + 20% new
- Response time: ~2 seconds (fine for battery monitoring)
- Reduces display jitter from ±0.5V to ±0.05V

## USB-Only Test Mode

When testing without a battery (USB power only), the VBAT pin reads floating phantom values. To prevent false fault triggers:

```cpp
#define VBAT_USB_MODE 5.00f

bool usbMode = (vbatFiltered < VBAT_USB_MODE);
if (!usbMode) {
  // normal safety checks
}
```

Display shows `VBAT: USB-ONLY` instead of voltage.

**Do not connect USB and battery simultaneously** unless your dev board has reverse-current protection.

## Testing Without a Battery

For calibration and verification, any stable DC source between 5V and 17V works:

- Bench PSU (best — adjustable, stable)
- 9V battery (single test point)
- Fixed adapter (12V, etc.)

Connect source between BAT+ and GND of the divider. ESP32 powered by USB. Verify OLED reading matches multimeter.

## Required Libraries

- Adafruit SSD1306
- Adafruit GFX Library
- ESP32Servo

## Common Issues

| Problem | Cause | Fix |
|---------|-------|-----|
| Reading wildly off (60V+ shown for 15V actual) | Sample count and divisor mismatch in `readVbat()` | Make sure loop count and divisor match |
| Reading 17-18V for 15V actual | ADC attenuation not set | Add `analogSetPinAttenuation(PIN_VBAT, ADC_11db)` in setup |
| Reading jitters 0.5V+ even with calibration | Long wires, no filter cap | Add 100nF cap near ESP32 GPIO4 pin |
| Reading drifts between sessions | ADC temperature sensitivity | Recalibrate after warm-up; ±0.3V variation is normal |
| Mid-range error after calibration | ADC inherent non-linearity | Acceptable for safety logic; for lab-grade precision use external ADC (ADS1115) |

## Hardware Limit

ESP32-C3 internal ADC has ±2-3% inherent calibration error and non-linearity. For precision applications, use an external 16-bit ADC like the ADS1115. For a fan controller, internal ADC with two-point calibration is sufficient.

## Next Step

Once VBAT readings are accurate, proceed to `05-safety-features` for arming sequence and fault latching.
