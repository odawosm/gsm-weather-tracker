// Test 02: I2C bus scan (SDA = A4, SCL = A5).
// PASS: 0x3C found (OLED). 0x27 or 0x3F also appear if the PCF8574 LCD backpack is connected.

#include <Wire.h>

void setup() {
  Serial.begin(9600);
  Wire.begin();
  Serial.println(F("=== Test 02: I2C scanner ==="));
}

void loop() {
  byte found = 0;
  Serial.println(F("Scanning..."));
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      found++;
      Serial.print(F("  Found 0x"));
      if (addr < 16) Serial.print('0');
      Serial.print(addr, HEX);
      if (addr == 0x3C || addr == 0x3D) Serial.print(F("  <- SSD1306 OLED"));
      if (addr == 0x27 || addr == 0x3F) Serial.print(F("  <- PCF8574 LCD backpack"));
      Serial.println();
    }
  }
  if (found == 0) {
    Serial.println(F("FAIL: no devices. Check SDA->A4, SCL->A5, VCC->5V, GND, and the OLED pin order (GND,VCC,SCL,SDA)."));
  } else {
    Serial.print(F("PASS: "));
    Serial.print(found);
    Serial.println(F(" device(s) found."));
  }
  Serial.println();
  delay(5000);
}
