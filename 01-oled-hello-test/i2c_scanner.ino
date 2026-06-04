#include <Wire.h>
void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);  // SDA=8, SCL=9 for ESP32-C3
  delay(1000);
  Serial.println("Scanning I2C...");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("Found device at 0x");
      Serial.println(addr, HEX);
    }
  }
  Serial.println("Done");
}
void loop() {}