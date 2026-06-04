# Wiring Diagram

Complete schematic for the Turbine Brick controller.

## Block Diagram

```mermaid
flowchart LR
    BAT["4S LiPo<br/>14.8V nominal"] -->|"+ / −"| ESC
    BAT -->|"+ via 10kΩ"| DIV[Voltage Divider]
    DIV -->|"midpoint"| ESP_VBAT["ESP32-C3<br/>GPIO4 ADC"]
    DIV -->|"− via 2kΩ"| GND((GND))

    ESC -->|"3-phase"| MOTOR["Brushless Motor<br/>P1604 3800KV"]
    ESC -->|"BEC 5V"| ESP_VCC["ESP32-C3<br/>5V"]
    ESC -->|"BEC GND"| GND
    ESP_PWM["ESP32-C3<br/>GPIO2"] -->|"PWM signal"| ESC

    POT["10kΩ Pot"] -->|"3V3"| POT
    POT -->|"wiper"| ESP_POT["ESP32-C3<br/>GPIO3 ADC"]
    POT -->|"GND"| GND

    ESP_I2C["ESP32-C3<br/>GPIO8 SDA<br/>GPIO9 SCL"] -->|"I²C"| OLED["SSD1306 OLED<br/>0x3C"]

    ESP_LED["ESP32-C3<br/>GPIO10"] -->|"DATA via 330Ω"| LED["WS2812B Ring<br/>18 LEDs"]
    ESP_VCC -->|"5V"| LED
    LED -->|"GND"| GND
```

## Voltage Divider Detail

```mermaid
flowchart TD
    BAT["BAT+<br/>(up to 16.8V)"] --> R1["R1 = 10kΩ"]
    R1 --> MID(("Mid Node"))
    MID --> R2["R2 = 2kΩ"]
    R2 --> GND1((GND))
    MID --> CAP["100nF cap<br/>filter"]
    CAP --> GND2((GND))
    MID -->|"~2.5V max"| ADC["GPIO4 ADC"]
```

**Calculation:** V_adc = V_bat × R2 / (R1 + R2) = V_bat × 0.1667
For full 4S at 16.8V → 2.80V at ADC ✓ (under 3.3V max)

## ESC Connections

```mermaid
flowchart LR
    BAT_PLUS["BAT+ (XT60/XT30)"] -->|"thick red"| ESC_BATP["ESC BAT+"]
    BAT_MINUS["BAT− (XT60/XT30)"] -->|"thick black"| ESC_BATM["ESC BAT−"]

    ESC_M1["ESC Motor A"] -->|"yellow"| MOT_A["Motor A"]
    ESC_M2["ESC Motor B"] -->|"yellow"| MOT_B["Motor B"]
    ESC_M3["ESC Motor C"] -->|"yellow"| MOT_C["Motor C"]

    ESC_BECP["ESC BEC+"] -->|"thin red"| ESP_5V["ESP32 5V"]
    ESC_BECM["ESC BEC−"] -->|"thin black"| ESP_GND["ESP32 GND"]
    ESC_SIG["ESC Signal"] -->|"white/yellow"| ESP_PWM["ESP32 GPIO2"]
```

If motor spins the wrong direction, swap any two of Motor A/B/C.

## Pin Reference Table

| ESP32-C3 GPIO | Function | Connects To | Notes |
|---|---|---|---|
| GPIO2 | ESC PWM signal | ESC signal wire | 1000-2000µs pulse |
| GPIO3 | POT wiper (ADC) | 10kΩ pot middle pin | 11dB attenuation required |
| GPIO4 | VBAT (ADC) | Divider midpoint | 11dB attenuation required, 100nF cap to GND |
| GPIO8 | I²C SDA | OLED SDA | Pull-ups on OLED module |
| GPIO9 | I²C SCL | OLED SCL | Pull-ups on OLED module |
| GPIO10 | LED data | WS2812B DIN (via 330Ω) | 5V tolerant signal |
| 5V | Power in | ESC BEC red wire | From ESC's 5V regulator |
| GND | Ground | Common ground bus | ALL grounds must connect |

## Critical Notes

**Common ground is mandatory.** ESC GND, ESP32 GND, OLED GND, LED strip GND, and voltage divider GND must all share the same ground point. The BEC provides this automatically when wired correctly.

**ADC attenuation must be set in software** before any analogRead. Without `analogSetPinAttenuation(PIN, ADC_11db)` the ESP32-C3 ADC saturates above ~750mV and readings are meaningless.

**Do not connect USB and battery simultaneously** unless your ESP32-C3 dev board has reverse-current protection on the USB Vbus rail. When in doubt, disconnect one before connecting the other.

**100nF filter capacitor on GPIO4** is recommended close to the ESP32 pin. It significantly reduces voltage reading noise.

**330Ω series resistor on LED data line** is recommended. Protects the first LED from voltage spikes and reduces signal reflections.

## Power Architecture

```mermaid
flowchart TD
    BAT["4S LiPo"] --> ESC
    ESC --> MOTOR["Motor 3-phase"]
    ESC --> BEC["BEC 5V/3A regulator"]
    BEC --> RAIL5V["5V Rail"]
    RAIL5V --> ESP32["ESP32-C3"]
    RAIL5V --> LED_RING["LED Ring"]
    ESP32 --> REG3V3["3V3 regulator (internal)"]
    REG3V3 --> OLED_PWR["OLED VCC"]
    REG3V3 --> POT_PWR["Pot top pin"]
```

The ESC's BEC handles voltage regulation from 14.8V battery down to 5V for the ESP32. The ESP32's internal regulator further drops to 3.3V for the OLED and pot reference. No separate regulators needed.
