// Test 05: LDR divider on A0 (LDR to 5V, 10k to GND, so brighter = higher value).
// PASS: value changes with light. Cover the LDR with your hand and the value should fall.

const uint8_t LDR_PIN = A0;

void setup() {
  Serial.begin(9600);
  Serial.println(F("=== Test 05: LDR ==="));
  Serial.println(F("Hint: cover the LDR -> value falls. Shine a phone torch on it -> value rises."));
}

void loop() {
  int raw = analogRead(LDR_PIN);
  // ponytail: linear 0-1023 -> 0-100 %; the LDR is logarithmic, calibrate min/max later if needed
  int pct = map(raw, 0, 1023, 0, 100);
  Serial.print(F("Raw: "));
  Serial.print(raw);
  Serial.print(F("   Light: "));
  Serial.print(pct);
  Serial.print(F(" %"));
  if (raw < 5) Serial.print(F("   (near 0: check LDR->5V leg and A0 wiring)"));
  if (raw > 1018) Serial.print(F("   (near 1023: check 10k resistor to GND)"));
  Serial.println();
  delay(500);
}
