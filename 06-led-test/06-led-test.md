# 06 · LED Ring Test

Adds the WS2812B LED ring to the integrated controller. Tests addressable LED control, animations, and integration with the existing controller state machine.

## Files

- `06-led-test.ino` — full integration: OLED + pot + ESC + VBAT + arming + LED ring animations

## Prerequisites

- `05-safety-features` working — arming sequence and fault latching verified

## Hardware

- Everything from test 05
- WS2812B LED strip cut to 18 LEDs (60 LEDs/m strip = 30cm length)
- 330Ω resistor (recommended, optional)
- Wires to connect LED strip to ESP32

## LED Strip Wiring

| LED Strip Pad | ESP32-C3 Pin | Notes |
|---------------|--------------|-------|
| 5V (red)      | 5V (BEC)     | Direct from BEC, NOT through ESP32 regulator |
| DIN (centre)  | GPIO10       | Via 330Ω resistor (recommended) |
| GND (black)   | GND          | Common ground |

```
ESP32 GPIO10 ──[330Ω]──► WS2812B DIN
ESP32 5V (BEC) ────────► WS2812B 5V
ESP32 GND ─────────────► WS2812B GND
```

## Why the 330Ω Resistor

The series resistor on the data line:

- Protects the first LED's data input from voltage spikes
- Reduces signal reflections on longer wire runs
- Prevents the first LED from burning out during power-on transients

Common value: 220Ω - 470Ω, exact value not critical. The strip works without it for short wire runs (<10cm) but adding it is best practice.

## Power Budget

WS2812B LEDs draw up to 60mA at full white per LED. For 18 LEDs:

- Worst case (all white, full brightness): 18 × 60mA = 1.08A
- Typical use (mixed colours, moderate brightness): 200-400mA

The BEC on the Skywalker 40A provides 3A — comfortable margin. Don't power the LED ring from the ESP32's 3V3 regulator — it can't supply that current.

## Strip Cutting and Soldering

WS2812B strips have copper pads between each LED that mark cut points. Cut with scissors at the centre of the copper pad.

For 18 LEDs:
1. Count 18 LEDs from one end
2. Cut at the pad **after** the 18th LED
3. Solder 3 wires to the **input end** (look for arrow markings on the strip — wire to the side the arrow points away from)
4. Test the strip before installing in the cage

## Strip Installation in Cage

The cage has an internal LED channel (11mm wide × 3mm deep) around the duct circumference, designed to hold the strip.

1. Solder wires to strip BEFORE installation (can't reach solder points once installed)
2. Clean the channel with isopropyl alcohol
3. Peel adhesive backing from strip (most WS2812B strips have 3M adhesive)
4. Press strip into channel, starting from the wire-exit position
5. Route wires through the central Ø20mm hole down to the base interior

If your strip has no adhesive, use thin double-sided VHB tape applied to the back of the strip.

## LED Ring Animation States

The sketch maps controller states to LED animations:

| State | Animation | Brightness |
|-------|-----------|------------|
| FAULT (VBAT cutoff) | Pulsing red | High (200/255) |
| DISARMED (boot or before arming) | Rotating amber | Low (30/255) |
| LOW BAT (13.2V < VBAT < 12.8V) | Pulsing orange | Normal (80/255) |
| ARMED, 0-50% throttle | Blue → Green gradient | Normal (80/255) |
| ARMED, 50-100% throttle | Green → Red gradient | Normal (80/255) |
| USB-only mode | Same as armed, ignores VBAT | Normal (80/255) |

The colour gradient on throttle gives intuitive feedback — green = nominal, hot colours = high power.

## Procedure

### Step 1 — Standalone LED Test

Before integrating with the controller, verify the strip works on its own. The sketch includes a simple test routine triggered at startup:

1. Wire only the LED strip + ESP32 (no ESC, no motor)
2. Upload `06-led-test.ino`
3. Power ESP32 via USB
4. **Expected:** Startup animation runs (all LEDs cycle red, green, blue, then settle into disarmed rotating amber)

If startup animation fails:
- Check wire orientation (DIN vs DOUT — arrow on strip)
- Verify 5V power present at strip
- Try first LED only (cut strip down to 1 LED for diagnosis)

### Step 2 — Full Integration Test

1. Reconnect ESC, motor (prop OFF), battery
2. Power on with pot at any position
3. **Expected:** LEDs show rotating amber (disarmed state)
4. Move pot to MIN, hold 500ms → arm
5. **Expected:** LEDs switch to blue (0% throttle armed)
6. Slowly increase throttle
7. **Expected:** LEDs transition through blue → cyan → green → yellow → orange → red as throttle rises

### Step 3 — Fault State Test

1. With armed system, simulate low voltage (or wait for actual battery drain)
2. **Expected:** When VBAT < 13.2V, LEDs switch to orange pulse
3. **Expected:** When VBAT < 12.8V, LEDs switch to pulsing red (fault latched)
4. Disconnect battery to clear fault

## Tuning

Brightness can be adjusted with these defines:

```cpp
#define LED_BRIGHTNESS_NORMAL   80   // 0-255, comfortable for desktop use
#define LED_BRIGHTNESS_LOW      30   // dim — for disarmed state
#define LED_BRIGHTNESS_FAULT    200  // attention-grabbing for faults
```

Higher brightness draws more current and can be too bright in a dark room. 80/255 is a comfortable starting point.

## Required Libraries

- Adafruit SSD1306
- Adafruit GFX Library
- **Adafruit NeoPixel** (new for this sketch)
- ESP32Servo

## Troubleshooting

| Problem | Likely Cause |
|---------|--------------|
| Only first LED lights, rest stay off | DIN/DOUT swapped, or short between data line and 5V |
| All LEDs stay white at full brightness | Data line not connected, strip in undefined state |
| Random flickering | Loose data line, missing GND, or no 330Ω resistor on long run |
| LEDs work but colours wrong | Strip is GRB vs RGB — check `NEO_GRB` vs `NEO_RGB` in code |
| Strip works briefly then fails | Power supply can't deliver enough current — check BEC current rating |
| ESP32 reboots when LEDs at full white | Power supply brownout — reduce brightness or use larger BEC |

## Common Issues — Colour Order

Different LED strips use different colour orders:

- Most WS2812B → GRB (use `NEO_GRB`)
- Some older strips → RGB (use `NEO_RGB`)
- Some RGBW strips → GRBW (use `NEO_GRBW`)

If red and green appear swapped, change `NEO_GRB` to `NEO_RGB` in the `Adafruit_NeoPixel` constructor.

## Next Step

This is the final test sketch. The main project firmware (`turbine_brick_controller.ino` in the parent folder) integrates all features from tests 01-06 into a single production-ready sketch.
