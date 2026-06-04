# 01 · OLED Hello Test

First electronics test — verifies the OLED display is wired correctly and the ESP32-C3 can communicate with it over I²C.

## Files

- `i2c_scanner.ino` — Scans the I²C bus for connected devices
- `oled_hello.ino` — Displays "Hello" on the OLED

## Hardware

- ESP32-C3 (any variant — SuperMini, dev board, etc.)
- SSD1306 128×32 OLED I²C module
- 4× jumper wires

## Wiring

| OLED Pin | ESP32-C3 Pin | Notes |
|----------|--------------|-------|
| VCC      | 3V3 (or 5V) | Most OLEDs accept 3.3V–5V |
| GND      | GND          | Common ground |
| SDA      | GPIO8        | I²C data line |
| SCL      | GPIO9        | I²C clock line |

## Critical Board Settings (Arduino IDE)

Before flashing any ESP32-C3 code:

- Tools → Board → **ESP32C3 Dev Module**
- Tools → **USB CDC On Boot → Enabled** (REQUIRED for Serial output)
- Tools → **JTAG Adapter → Disabled** (sometimes interferes)
- Tools → Upload Speed → **921600**

If `USB CDC On Boot` is not enabled, Serial Monitor shows nothing even when code is running.

## Procedure

### Step 1 — Verify I²C device is detected

Upload `i2c_scanner.ino` first. Open Serial Monitor at **115200 baud**.

Expected output:

```
Scanning I2C...
Found device at 0x3C
Total devices: 1
```

The OLED should be detected at **0x3C** (or sometimes 0x3D — note your address).

**If nothing is found:**
- Check SDA/SCL aren't swapped
- Verify OLED has power (3.3V on VCC pin)
- Try different GPIO pins in case board labels differ from internal GPIO numbers

**If Serial Monitor shows nothing at all:**
- USB CDC On Boot is probably disabled — enable it and reflash
- Wrong baud rate — set Serial Monitor to 115200
- Wrong port selected in Tools menu

### Step 2 — Display "Hello"

Once the scanner finds the OLED, upload `oled_hello.ino`. If your OLED was at 0x3D, update the `OLED_ADDR` define in the sketch.

## Expected Result

"Hello" appears on the OLED in 2x size text, roughly centred.

## Display Orientation

The SSD1306 can be rotated in software. Add this line after `display.begin()` in setup:

```cpp
display.setRotation(2);   // 0=normal, 2=180° (upside down)
```

Useful when the OLED is mounted in an orientation that requires flipping. For 128×32 displays, only 0 (normal) and 2 (flipped) are practical.

## Required Libraries

Install via Arduino IDE Library Manager:

- **Adafruit SSD1306** (will auto-install Adafruit GFX as dependency)
- **Adafruit GFX Library**

## Troubleshooting

| Problem | Likely Cause |
|---------|--------------|
| `OLED init failed` in Serial Monitor | Wrong I²C address — try 0x3D, verify with scanner |
| Nothing on Serial Monitor | USB CDC not enabled, or wrong baud rate |
| Display stays black, no errors | `display.display()` not called, or contrast too low |
| Display flickers/garbled | Loose SDA/SCL connection, or wires too long |
| Text upside down | Mount orientation differs — add `display.setRotation(2)` |

## Next Step

Once "Hello" displays reliably, move to `02-pot-test.md`.
