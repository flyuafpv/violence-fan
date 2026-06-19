# Turbine Brick

A brushless desktop fan controller built around an ESP32-C3, controlling an FPV-style brushless motor through an ESC.

The repository contains the full electronics development sequence (six progressively integrated test sketches), the final controller firmware, the 3D-printable enclosure (STL files), and reference documentation.

## What This Project Does

Turns FPV drone hardware into a quiet, controllable, vibration-isolated desktop fan with:

- Brushless motor + 90mm ducted propeller for high airflow
- Potentiometer throttle control with dead zones
- 128×32 OLED status display
- 18-LED ring inside the duct for indication and ambience
- Battery voltage monitoring with two-point calibration
- Safety arming sequence (motor disabled until throttle at zero)
- Low-voltage warning and hard cutoff to protect LiPo cells

## Hardware

| Component | Notes |
|---|---|
| MCU | ESP32-C3 (SuperMini or any variant with USB CDC) |
| Display | SSD1306 128×32 OLED, I²C |
| Throttle | 10kΩ linear potentiometer |
| LED ring | WS2812B 60 LED/m strip, 18 LEDs |
| ESC | 40A brushless ESC with BEC (Skywalker 40A or similar) |
| Motor | Brushless out-runner (P1604 3800KV or similar small FPV motor) |
| Propeller | 90mm 3-blade ducted (Gemfan D90S or similar) |
| Battery | 4S LiPo, XT60 or XT30 connector |
| Resistors | 10kΩ + 2kΩ for voltage divider, 330Ω for LED data line |
| Capacitor | 100nF ceramic for ADC filtering |
| Inserts | M3×5 brass heat-set inserts (×4) |
| Fasteners | M3×10 socket cap (×4), M2.6×8 self-tap (×8), M2×6 (×4) |

Full wiring diagram: see `WIRING.md`.

## Repository Layout

```
.
├── README.md                          # this file
├── WIRING.md                          # wiring diagrams and pin reference
├── turbine_brick_v2.html              # full mechanical design reference
├── fan_controller_reference.html      # electronics architecture reference
├── turbine_brick_controller/          # FINAL integrated firmware
│   └── turbine_brick_controller.ino
├── 01-oled-hello-test/                # progressive test sketches
│   ├── 01-oled-hello-test.ino
│   └── 01-oled-hello-test.md
├── 02-pot-test/
│   ├── 02-pot-test.ino
│   └── 02-pot-test.md
├── 03-motor-test/
│   ├── 03-motor-test.ino
│   └── 03-motor-test.md
├── 04-voltage-divider/
│   ├── 04-voltage-divider.ino
│   └── 04-voltage-divider.md
├── 05-safety-features/
│   ├── 05-safety-features.ino
│   └── 05-safety-features.md
├── 06-led-test/
│   ├── 06-led-test.ino
│   └── 06-led-test.md
└── STL/                               # 3D-printable parts
```

## Build Approach

The electronics are developed in six incremental test sketches, each adding one subsystem:

1. **`01-oled-hello-test`** — verifies OLED display and I²C wiring
2. **`02-pot-test`** — adds potentiometer reading with dead-zone calibration
3. **`03-motor-test`** — adds ESC and brushless motor control
4. **`04-voltage-divider`** — adds battery voltage monitoring with two-point calibration
5. **`05-safety-features`** — adds arming sequence and fault latching
6. **`06-led-test`** — adds the WS2812B LED ring

Each subfolder contains a self-contained sketch (`*.ino`) and a readme (`*.md`) documenting wiring, procedure, expected behaviour, and troubleshooting. Work through them in order — each test depends on the previous ones being verified.

The final firmware (`turbine_brick_controller.ino`) integrates everything into a single sketch with full safety logic.

## Required Arduino Libraries

Install through Arduino IDE → Tools → Manage Libraries:

- **Adafruit SSD1306** (will auto-install Adafruit GFX)
- **Adafruit GFX Library**
- **Adafruit NeoPixel**
- **ESP32Servo**

Plus the ESP32 board package (Tools → Board → Boards Manager → search "ESP32" by Espressif Systems).

## Arduino IDE Settings (Critical)

These settings must be applied when flashing any sketch in this project:

- **Tools → Board:** ESP32C3 Dev Module
- **Tools → USB CDC On Boot:** Enabled *(required for Serial Monitor)*
- **Tools → JTAG Adapter:** Disabled
- **Tools → Upload Speed:** 921600
- **Serial Monitor baud rate:** 115200

Without `USB CDC On Boot` set to Enabled, Serial Monitor shows nothing even when code is running. This setting can reset between IDE updates — verify before flashing.

## Flashing

1. Connect ESP32-C3 to your computer via USB-C
2. Open the relevant `.ino` file in Arduino IDE
3. Apply the IDE settings above
4. Select the correct port under Tools → Port
5. Click Upload (or `Ctrl+U`)
6. Open Serial Monitor at 115200 baud to see debug output

If upload fails:
- Try pressing and holding the BOOT button while clicking Upload
- Lower the upload speed to 460800 if the cable is poor

## First-Time Usage

After flashing the final controller firmware:

1. Verify the propeller is OFF the motor during initial testing
2. Secure the motor so it cannot move
3. Connect the battery — ESC plays arming tones (3 seconds)
4. OLED displays "TURBINE BRICK / Initialising ESC..."
5. After 3 seconds, OLED displays "MOVE POT TO MIN"
6. Move the potentiometer to its minimum position
7. Hold at minimum for 500ms — progress bar fills
8. Display switches to throttle mode: "THR: 0%"
9. Slowly increase throttle — motor responds

If the OLED shows a fault message ("CUTOFF 3.2V/c"), disconnect the battery and check voltage. The controller will not run until power is cycled.

## Voltage Calibration

The voltage divider readings depend on individual ESP32-C3 ADC characteristics. Each device should be calibrated using the two-point procedure documented in `04-voltage-divider/04-voltage-divider.md`.

Default calibration values in the source code are placeholders — update `CAL_R_LOW` and `CAL_R_HIGH` in `turbine_brick_controller.ino` after running the calibration procedure on your specific board.

## Final Assembly & Calibration

The progressive test sketches work fine on a breadboard or prototyping board, but **ADC readings will be unstable** in that environment — loose contacts, long jumper wires, and shared ground returns create noise that corrupts analog signals. Don't waste time fine-tuning calibration on a prototyping board; tune everything **after soldering and final assembly inside the enclosure**.

### Wiring Best Practices for Final Assembly

**Voltage divider:**
- Solder R1, R2, and the 100nF cap directly to the back of the ESP32-C3 dev board
- Keep the wire from divider midpoint to GPIO4 as short as possible (under 10cm)
- Eliminates breadboard contact resistance and reduces ADC noise

**Twisted pairs reduce EMI:**
- POT wiper wire twisted with its GND return
- LED 5V and LED GND twisted together (cancels WS2812B current-pulse noise)
- ESC signal wire can run separately (digital signal, more robust)

**Single ground point:**
- All ground returns meet at ONE physical spot — typically the ESC BEC GND pad
- Avoid daisy-chaining grounds across multiple components
- Keeps the ADC ground reference clean

**Separate analog and digital wires:**
- Keep POT wiper and VBAT divider wires on one side of the enclosure
- Keep LED data and ESC signal wires on the other
- Don't run analog and digital wires parallel to each other for long distances

**Decoupling capacitor on LED 5V:**
- Solder a 470µF electrolytic cap between the LED strip's 5V and GND wires inside the base, as close to the strip as practical
- Absorbs current pulses to reduce noise propagating back to the ESP32 ADC
- Closer to the LEDs = more effective, but anywhere on the LED 5V/GND wire pair helps

**Wire lengths:**
- POT wires: under 15cm
- VBAT divider wire to GPIO4: under 10cm
- LED data wire can be longer (digital signal)

### Recalibration After Assembly

Once everything is soldered and mounted in the enclosure, run through these tests in order:

1. **Pot test (`02-pot-test`)** — verify raw values reach near 0 and near 4095 at the extremes with minimal jitter. With proper wiring, dead zones of `RAW_MIN=100, RAW_MAX=4050` are typically sufficient.

2. **Voltage divider test (`04-voltage-divider`)** — re-run the two-point calibration procedure. Calibration values will shift slightly from breadboard readings due to cleaner connections.

3. **Safety features test (`05-safety-features`)** — verify the arming sequence, low-battery warning, and fault latching all behave correctly.

4. **Final controller** — flash the integrated firmware with updated calibration constants. Verify all LED ring states (disarmed, armed, low battery, fault).

### Documentation

Take photos of the wired-up internals before closing the enclosure. Useful for future maintenance and troubleshooting without needing to disassemble.

## Safety Notes

- The motor uses an FPV-grade brushless motor capable of significant RPM. Always remove the propeller for bench testing.
- The 4S LiPo battery can deliver large currents during a short circuit. Verify all wiring before connecting power.
- Do not exceed 16.8V on the voltage divider input — would damage the ESP32-C3 ADC.
- Do not connect USB and battery simultaneously unless the dev board has reverse-current protection.
- The arming sequence is intentional safety logic — do not bypass it.

## License

See LICENSE file.
