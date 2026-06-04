# 04 · Voltage Divider Test

Adds battery voltage monitoring to the motor + pot test. Reads 4S LiPo voltage through a resistor divider and displays it on the OLED alongside throttle.

This is the final electronics integration test — confirms all 4 input/output paths work together:

- OLED (display)
- Potentiometer (analog input)
- ESC (PWM output)
- Voltage divider (analog input for safety monitoring)

## Files

- `motor_vbat_test.ino` — Full integration: OLED + pot + ESC + battery monitor with two-point calibration, EMA filtering, and fault latching

## Prerequisites

- `03-motor-test.md` working — motor spins from 0-100% via pot

## Hardware

- Everything from test 03
- **10kΩ resistor** (R1 — high side)
- **2kΩ resistor** (R2 — low side)
- **100nF ceramic capacitor** (marking "104") — for noise filtering
- Small protoboard or just solder + heat-shrink

## Theory

A 4S LiPo can reach 16.8V at full charge. The ESP32-C3 ADC max input is 3.3V. So we need to scale the battery voltage down by a factor of about 5-6.

With **R1 = 10kΩ** and **R2 = 2kΩ**:

```
Divider ratio = R2 / (R1 + R2) = 2 / 12 = 0.1667
Inverse ratio (for code) = 1 / 0.1667 = 6.0

V_adc = V_bat × 0.1667
V_bat = V_adc × 6.0  (in theory)
```

**In practice, the real ratio is around 5.0-5.3** due to ESP32-C3 ADC non-linearity and reference voltage variation. This is normal — calibration in code handles it.

## Critical ADC Setup

The ESP32-C3 ADC requires explicit attenuation configuration. **Without this, voltage readings will be wildly wrong:**

```cpp
analogReadResolution(12);
analogSetPinAttenuation(PIN_VBAT, ADC_11db);  // 0-3.3V range
analogSetPinAttenuation(PIN_POT, ADC_11db);   // same for pot
```

Add these lines in `setup()` after `Wire.begin()`. The 11dB attenuation gives the full 0-3.3V input range needed for the divider output.

## 4S LiPo Voltage Thresholds

| State | Per Cell | Total (4S) | Action |
|-------|----------|------------|--------|
| Full charge | 4.20V | 16.80V | Normal operation |
| Nominal | 3.70V | 14.80V | Normal operation |
| **Low warning** | **3.30V** | **13.20V** | **Reduce throttle, warn user** |
| **Cutoff** | **3.20V** | **12.80V** | **Stop motor, latch FAULT** |
| Dead (damage risk) | 3.00V | 12.00V | Below this = battery damaged |

**3.2V/cell cutoff** is the proper LiPo safety threshold. Below 3.0V/cell, cells suffer permanent damage. The 3.2V cutoff leaves headroom for cell imbalance and load recovery.

## Wiring

### Voltage Divider Circuit

```
BAT+ ──┬──[10kΩ R1]──┬──[2kΩ R2]── GND
                     │
                     ├── GPIO4 (PIN_VBAT)
                     │
                     └──[100nF cap]── GND
```

| Connection | Notes |
|------------|-------|
| BAT+ | Battery positive — where ESC BAT+ connects |
| R1 (10kΩ) | One end to BAT+, other end to junction |
| R2 (2kΩ) | One end to junction, other end to GND |
| 100nF cap | Junction to GND — noise filter, place close to ESP32 |
| Junction | Connects to GPIO4 |

**Critical:** R2 must be the **2kΩ on the GND side**. Reversed resistors give the wrong ratio and could push >3.3V into the ADC, damaging the chip.

### Pin Summary (full project)

| ESP32-C3 Pin | Function |
|--------------|----------|
| GPIO2 | ESC PWM signal |
| GPIO3 | Potentiometer wiper (ADC, 11dB attenuation) |
| GPIO4 | Voltage divider midpoint (ADC, 11dB attenuation) |
| GPIO8 | OLED SDA |
| GPIO9 | OLED SCL |
| GPIO10 | (Reserved for WS2812B LED data) |
| 5V | From ESC BEC + |
| GND | Common ground |

## Wiring Verification Before First Power-On

**Critical safety check before connecting the LiPo for the first time:**

1. **Multimeter set to DC voltage**
2. Probe between divider junction (R1/R2 meeting point) and GND with the divider connected to a test source (e.g., 12V from a bench PSU)
3. Reading should be **~2.0V** (12V × 2k/12k = 2.0V theoretical)
4. If reading is much higher (e.g., 6V or 12V), resistors are wrong or wired incorrectly → **DO NOT connect to the ESP32 yet**

The ADC pin has an absolute max of ~3.6V. Exceeding this damages the ESP32-C3.

## Two-Point Calibration Procedure

The ESP32-C3 ADC is non-linear and has chip-to-chip variation. Two-point calibration gives ±0.1V accuracy at calibration points and ±0.4V across the range.

### Step 1 — Initial test with placeholder calibration

Set the calibration constants in code to identity values:

```cpp
#define CAL_V_LOW     0.0f
#define CAL_V_HIGH    1.0f
#define CAL_R_LOW     0.0f
#define CAL_R_HIGH    1.0f
```

These produce scale=1, offset=0 → no correction applied. The displayed value equals `vadc × VDIV_RATIO`.

### Step 2 — Take raw measurements at two known voltages

Use a bench PSU or other stable voltage source:

1. Set source to **12.00V** (verify with multimeter)
2. Flash code with no-correction values
3. Wait 15 seconds for EMA filter to settle
4. Record the OLED reading → this is your **R_LOW**

5. Set source to **16.80V** (verify with multimeter)
6. Wait 15 seconds for EMA filter to settle
7. Record the OLED reading → this is your **R_HIGH**

Typical raw readings for ESP32-C3:
- At 12.00V actual → ~12.5-12.7V displayed
- At 16.80V actual → ~17.1-17.2V displayed

### Step 3 — Update calibration constants

Replace the placeholders with your measured values:

```cpp
#define CAL_V_LOW     12.00f   // actual voltage measured with multimeter
#define CAL_V_HIGH    16.80f   // actual voltage measured with multimeter
#define CAL_R_LOW     12.59f   // YOUR raw OLED reading at 12.00V
#define CAL_R_HIGH    17.16f   // YOUR raw OLED reading at 16.80V
```

### Step 4 — Verify

1. Reflash with the updated calibration
2. Wait 15 seconds for EMA to settle at each test point
3. Verify:
   - PSU at 12.00V → OLED reads 12.0V ±0.1V ✓
   - PSU at 14.80V → OLED reads 14.5-15.2V (mid-range non-linearity)
   - PSU at 16.80V → OLED reads 16.8V ±0.1V ✓

Endpoint accuracy will be excellent. Mid-range accuracy is limited by ADC non-linearity (~±0.4V) but is acceptable for safety logic.

## EMA Filter for Stable Display

ESP32-C3 ADC readings naturally jitter. An exponential moving average smooths the display:

```cpp
vbatFiltered = vbatFiltered * 0.8f + vbat_corrected * 0.2f;
```

- 80% old reading + 20% new reading
- Response time: ~2 seconds (fine for battery monitoring)
- Reduces display jitter from ±0.5V to ±0.05V

## USB-Only Test Mode

When testing without a battery (USB power to ESP32 only), the VBAT pin reads floating phantom values. To prevent false fault triggers, the code detects this and disables safety checks:

```cpp
#define VBAT_USB_MODE 5.00f   // below this = USB-only mode

bool usbMode = (vbatFiltered < VBAT_USB_MODE);
if (!usbMode) {
  // normal safety checks
}
```

Display shows `VBAT: USB-ONLY` instead of voltage.

**Don't connect USB and battery simultaneously** unless your ESP32-C3 board has reverse-current protection. Workflow:

1. Disconnect battery → flash code via USB
2. Disconnect USB → connect battery (BEC powers ESP32)

## Fault Latching

Once VBAT drops below cutoff, the motor stays stopped until power is cycled:

```cpp
if (vbatFiltered < VBAT_CUTOFF) {
  faultLatched = true;  // stays true until power cycle
}
```

This prevents the motor from oscillating on and off near the cutoff threshold. Requires user action (unplug/replug battery) to recover.

## Testing Without a Battery

For calibration and basic verification, use any stable DC source between 5V and 17V:

- Bench PSU (best — adjustable, stable)
- 9V battery (one-time test point)
- Fixed voltage adapter (5V, 12V, etc.)
- Several batteries in series (for higher voltage tests)

Connect the source between BAT+ and GND of the divider. ESP32 powered by USB. Verify OLED reading matches multimeter.

## Required Libraries

- Adafruit SSD1306
- Adafruit GFX Library
- ESP32Servo

## Common Issues

| Problem | Cause | Fix |
|---------|-------|-----|
| Reading is wildly off (60V+ shown for 15V actual) | Sample count and divisor mismatch in `readVbat()` | Make sure loop count and divisor are the same number |
| Reading 17-18V for 15V actual | ADC attenuation not set | Add `analogSetPinAttenuation(PIN_VBAT, ADC_11db)` in setup |
| Reading jitters 0.5V+ even with calibration | Long wires, no filter cap | Add 100nF cap near ESP32 GPIO4 pin |
| Reading drifts between sessions | ADC temperature sensitivity | Recalibrate after warm-up; accept ±0.3V variation as normal |
| Mid-range reading off by 0.4V after calibration | ADC inherent non-linearity | Acceptable for safety logic; for better accuracy use external ADC chip (ADS1115) |

## Hardware Limit

ESP32-C3 internal ADC has ±2-3% inherent calibration error and non-linearity. For lab-grade precision (±0.01V) use an external 16-bit ADC like the ADS1115 over I²C. For a fan controller, the internal ADC with two-point calibration is sufficient.

## Next Step

This is the last standalone test. After this:

- All inputs (pot, vbat) and outputs (ESC, OLED) are validated
- Combine into the full controller firmware with state machine (DISARMED / ARMED / FAULT)
- Add LED ring control (WS2812B on GPIO10)
- Implement full safety logic and ESC arming sequence

See the main project firmware for the integrated version.
