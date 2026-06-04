#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
#define OLED_ADDR     0x3C   // change to 0x3D if scanner found that

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);  // SDA=GPIO8, SCL=GPIO9 — adjust if you used different pins
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed");
    while(1);
  }
  
  display.setRotation(2); 
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);          // 2x size
  display.setCursor(20, 8);        // x=20, y=8 — roughly centred on 128x32
  display.println("Hello");
  display.display();               // critical! pushes buffer to screen
}

void loop() {
}