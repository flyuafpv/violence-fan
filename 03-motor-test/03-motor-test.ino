#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
#define OLED_ADDR     0x3C

#define PIN_POT       3
#define PIN_ESC       2

#define RAW_MIN       200
#define RAW_MAX       4000

#define ESC_MIN_US    1000
#define ESC_MAX_US    2000

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Servo esc;

int readPotRaw() {
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
  
  // Splash screen
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("MOTOR TEST");
  display.println("Pot to MIN to arm");
  display.display();
  
  // ESC init — send MIN pulse and wait for ESC to recognise it
  esc.attach(PIN_ESC, ESC_MIN_US, ESC_MAX_US);
  esc.writeMicroseconds(ESC_MIN_US);
  delay(3000);  // ESC arming tones play during this time
}

void loop() {
  int raw = readPotRaw();
  int pct = rawToPct(raw);
  
  // Map percentage to ESC pulse width
  int pulseUs = map(pct, 0, 100, ESC_MIN_US, ESC_MAX_US);
  esc.writeMicroseconds(pulseUs);
  
  // Display
  display.clearDisplay();
  
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print("THROTTLE: ");
  display.print(pct);
  display.println("%");
  
  display.setCursor(0, 10);
  display.print("PWM: ");
  display.print(pulseUs);
  display.println("us");
  
  // Throttle bar
  display.drawRect(0, 22, 100, 8, SSD1306_WHITE);
  int barWidth = map(pct, 0, 100, 0, 100);
  display.fillRect(0, 22, barWidth, 8, SSD1306_WHITE);
  display.setCursor(105, 23);
  display.print(pct);
  
  display.display();
  
  // Serial log
  Serial.print("raw=");
  Serial.print(raw);
  Serial.print(" pct=");
  Serial.print(pct);
  Serial.print(" us=");
  Serial.println(pulseUs);
  
  delay(20);  // 50Hz loop — appropriate for ESC PWM
}