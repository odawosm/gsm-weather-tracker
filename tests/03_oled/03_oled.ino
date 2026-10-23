// Test 03: SSD1306 OLED 128x64 over I2C.
// PASS: "OLED TEST" title, a counter and a bar filling left to right on the display.
// Libraries: Adafruit SSD1306, Adafruit GFX.

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const uint8_t OLED_ADDR = 0x3C;  // use the address test 02 found (some boards are 0x3D)

Adafruit_SSD1306 display(128, 64, &Wire, -1);
unsigned int count = 0;

void setup() {
  Serial.begin(9600);
  Serial.println(F("=== Test 03: OLED ==="));
  // begin() allocates the 1 KB frame buffer; it fails if the address is wrong or RAM is short.
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("FAIL: display.begin() failed. Run test 02 for the address, check wiring."));
    for (;;) {}
  }
  Serial.println(F("PASS: display.begin() OK. Check the screen."));
}

void loop() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println(F("OLED TEST"));
  display.setTextSize(1);
  display.setCursor(0, 24);
  display.print(F("Count: "));
  display.println(count);

  byte w = count % 121;
  display.drawRect(0, 44, 124, 14, SSD1306_WHITE);
  display.fillRect(2, 46, w, 10, SSD1306_WHITE);
  display.display();

  if (count % 20 == 0) {
    Serial.print(F("Frame "));
    Serial.println(count);
  }
  count++;
  delay(50);
}
