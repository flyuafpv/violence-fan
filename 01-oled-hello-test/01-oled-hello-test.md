# 01 · OLED Hello Test

Verifies the OLED display is wired correctly and the ESP32-C3 can communicate with it over I²C. First test in the Turbine Brick build sequence.

## Files

- `01-oled-hello-test.ino` — combined I²C scanner + Hello display sketch

## Hardware

- ESP32-C3
- SSD1306 128×32 OLED I²C module
- 4× jumper wires

## Wiring

| OLED Pin | ESP32-C3 Pin | Notes |
|----------|--------------|-------|
| VCC      | 3V3 or 5V    | Most OLEDs accept 3.3-5V |
| GND      | GND          | Common ground |
| SDA      | GPIO8        | I²C data line |
| SCL      | GPIO9        | I²C clock line |

## Arduino IDE Settings (Critical)

Apply before flashing any sketch in this project:

- Tools → Board → **ESP32C3 Dev Module**
- Tools → **USB CDC On Boot → Enabled** *(required for Serial Monitor)*
- Tools → JTAG Adapter → Disabled
- Tools → Upload Speed → **921600**
- Serial Monitor baud rate: **115200**

If USB CDC On Boot is disabled, Serial Monitor shows nothing even when code is running.

## Procedure

The sketch runs an I²C scanner first, then displays "Hello" if a device is found.

1. Wire the OLED as above
2. Open `01-oled-hello-test.ino` in Arduino IDE
3. Apply IDE settings, select the correct port, upload
4. Open Serial Monitor at 115200 baud
5. Press the reset button on the ESP32-C3 if needed

## Expected Output

**Serial Monitor:**
```
Scanning I2C...
Found device at 0x3C
Total devices: 1
OLED initialised
```

**OLED:** "Hello" in 2x size text, roughly centred.

The OLED should be detected at **0x3C** (occasionally 0x3D). Update `OLED_ADDR` in the sketch if needed.

## Display Orientation

The SSD1306 can be rotated in software:

```cpp
display.setRotation(2);   // 0=normal, 2=180° flipped
```

For 128×32 displays only 0 (normal) and 2 (flipped) are practical. Add this line after `display.begin()` in setup.

## Required Libraries

- Adafruit SSD1306
- Adafruit GFX Library

## Troubleshooting

| Problem | Likely Cause |
|---------|--------------|
| `OLED init failed` in Serial Monitor | Wrong I²C address — try 0x3D, verify with scanner output |
| Nothing on Serial Monitor | USB CDC On Boot not enabled, or wrong baud rate |
| Display stays black, no errors | `display.display()` not called, or contrast set too low |
| Display flickers/garbled | Loose SDA/SCL connection, or wires too long |
| Text upside down | Add `display.setRotation(2)` after `display.begin()` |
| Scanner finds nothing | SDA/SCL swapped, or no power to OLED |

## Next Step

Once "Hello" displays reliably, proceed to `02-pot-test`.
