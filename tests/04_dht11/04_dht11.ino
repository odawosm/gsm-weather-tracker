// Test 04: DHT11 on D2.
// PASS: temperature and humidity printed every 2 s, sensible for the room.
// DHT11 range: 0-50 C (+/-2 C), 20-90 %RH (+/-5 %). Breathe on it and humidity should rise.
// Libraries: DHT sensor library (Adafruit), Adafruit Unified Sensor.

#include <DHT.h>

const uint8_t DHT_PIN = 2;
DHT dht(DHT_PIN, DHT11);

void setup() {
  Serial.begin(9600);
  Serial.println(F("=== Test 04: DHT11 ==="));
  dht.begin();
}

void loop() {
  delay(2000);  // DHT11 needs ~1 s between reads; 2 s is safe
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) {
    Serial.println(F("FAIL: read returned NaN. Check DATA->D2, VCC->5V, GND. Bare 3/4-pin sensors need a 10k pull-up DATA->VCC."));
    return;
  }
  Serial.print(F("PASS  Temp: "));
  Serial.print(t, 1);
  Serial.print(F(" C   Hum: "));
  Serial.print(h, 0);
  Serial.print(F(" %"));
  if (t < 0 || t > 50 || h < 20 || h > 90) Serial.print(F("   (outside DHT11 rated range)"));
  Serial.println();
}
