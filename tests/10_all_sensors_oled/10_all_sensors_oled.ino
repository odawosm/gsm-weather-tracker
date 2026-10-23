// Test 10: DHT11 + LDR + rain + OLED together (no GSM).
// PASS: all readings on the OLED, one JSON line every 5 s on Serial, free RAM reported.
// Libraries: DHT sensor library, Adafruit Unified Sensor, Adafruit GFX, Adafruit SSD1306.

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

const uint8_t DHT_PIN      = 2;
const uint8_t RAIN_DIG_PIN = 3;
const uint8_t RAIN_PWR_PIN = 4;
const uint8_t LDR_PIN      = A0;
const uint8_t RAIN_ANA_PIN = A1;
const uint8_t OLED_ADDR    = 0x3C;

const int RAIN_DRY   = 900;  // same thresholds as test 06
const int RAIN_DAMP  = 700;
const int RAIN_LIGHT = 400;

const unsigned long INTERVAL_MS = 5000;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
DHT dht(DHT_PIN, DHT11);

int freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

const __FlashStringHelper *rainClass(int raw) {
  if (raw >= RAIN_DRY) return F("dry");
  if (raw >= RAIN_DAMP) return F("damp");
  if (raw >= RAIN_LIGHT) return F("light rain");
  return F("heavy rain");
}

void setup() {
  Serial.begin(9600);
  pinMode(RAIN_PWR_PIN, OUTPUT);
  digitalWrite(RAIN_PWR_PIN, LOW);
  pinMode(RAIN_DIG_PIN, INPUT);
  dht.begin();
  Serial.print(F("Free RAM before OLED: "));
  Serial.println(freeRam());
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("FAIL: OLED begin() failed (wiring/address, or not enough RAM for the 1 KB buffer)."));
    for (;;) {}
  }
  Serial.print(F("Free RAM after OLED: "));
  Serial.println(freeRam());
  display.setTextColor(SSD1306_WHITE);
}

void loop() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  bool dhtOk = !isnan(t) && !isnan(h);

  int light = map(analogRead(LDR_PIN), 0, 1023, 0, 100);

  digitalWrite(RAIN_PWR_PIN, HIGH);
  delay(100);
  int rainRaw = analogRead(RAIN_ANA_PIN);
  bool rainFlag = digitalRead(RAIN_DIG_PIN) == LOW;
  digitalWrite(RAIN_PWR_PIN, LOW);

  // JSON line for the dashboard; null when the DHT read fails.
  Serial.print(F("{\"t\":"));
  if (dhtOk) Serial.print(t, 1); else Serial.print(F("null"));
  Serial.print(F(",\"h\":"));
  if (dhtOk) Serial.print(h, 0); else Serial.print(F("null"));
  Serial.print(F(",\"light\":"));
  Serial.print(light);
  Serial.print(F(",\"rain\":\""));
  Serial.print(rainClass(rainRaw));
  Serial.print(F("\",\"rain_raw\":"));
  Serial.print(rainRaw);
  Serial.print(F(",\"rain_flag\":"));
  Serial.print(rainFlag ? 1 : 0);
  Serial.print(F(",\"free_ram\":"));
  Serial.print(freeRam());
  Serial.println('}');

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Weather station test"));
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
  display.setCursor(0, 16);
  display.print(F("Temp:  "));
  if (dhtOk) { display.print(t, 1); display.println(F(" C")); } else display.println(F("err"));
  display.print(F("Hum:   "));
  if (dhtOk) { display.print(h, 0); display.println(F(" %")); } else display.println(F("err"));
  display.print(F("Light: "));
  display.print(light);
  display.println(F(" %"));
  display.print(F("Rain:  "));
  display.println(rainClass(rainRaw));
  display.print(F("RAM free: "));
  display.print(freeRam());
  display.display();

  delay(INTERVAL_MS - 100);
}
