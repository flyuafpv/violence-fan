#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
#define OLED_ADDR     0x3C

#define PIN_POT       3

// Dead zone configuration
#define RAW_MIN       200    // anything below this = 0%
#define RAW_MAX       4000   // anything above this = 100%

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int readPotRaw() {
  // Average 32 samples for stability
  long sum = 0;
  for (int i = 0; i < 32; i++) {
    sum += analogRead(PIN_POT);
    delayMicroseconds(100);
  }
  return sum / 32;
}

int rawToPct(int raw) {
  if (raw < RAW_MIN) return 0;
  if (raw > RAW_MAX) return 100;
  return map(raw, RAW_MIN, RAW_MAX, 0, 100);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    while(1);
  }
  
  analogReadResolution(12);
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("POT TEST v2");
  display.println("With dead zones");
  display.display();
  delay(1500);
}

void loop() {
  int raw = readPotRaw();
  int pct = rawToPct(raw);
  
  display.clearDisplay();
  
  // Line 1: raw value + percentage
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print("RAW:");
  display.print(raw);
  display.setCursor(80, 0);
  display.print(pct);
  display.print("%");
  
  // Line 2: status text
  display.setCursor(0, 10);
  if (pct == 0) {
    display.print("[MIN DEAD ZONE]");
  } else if (pct == 100) {
    display.print("[MAX DEAD ZONE]");
  } else {
    display.print("ACTIVE");
  }
  
  // Bottom: throttle bar
  display.drawRect(0, 22, 100, 8, SSD1306_WHITE);
  int barWidth = map(pct, 0, 100, 0, 100);
  display.fillRect(0, 22, barWidth, 8, SSD1306_WHITE);
  
  // Bar percentage on right
  display.setCursor(105, 23);
  display.print(pct);
  
  display.display();
  
  // Serial logging
  Serial.print("raw=");
  Serial.print(raw);
  Serial.print(" pct=");
  Serial.println(pct);
  
  delay(50);
}