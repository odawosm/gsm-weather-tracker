// Test 06: rain sensor. VCC from D4 (powered only during a read), A0 -> A1, D0 -> D3.
// PASS: ~1000 and "dry" when the plate is dry; value falls and the class changes when you touch it with a wet finger.
// D0 is the comparator output: LOW = wet (threshold set by the blue trimpot on the board).

const uint8_t RAIN_PWR_PIN = 4;
const uint8_t RAIN_DIG_PIN = 3;
const uint8_t RAIN_ANA_PIN = A1;

// Calibrate against your plate: raw at or above a threshold falls in that class.
const int RAIN_DRY   = 900;  // >= 900 dry
const int RAIN_DAMP  = 700;  // 700-899 damp
const int RAIN_LIGHT = 400;  // 400-699 light rain, below 400 heavy rain

void setup() {
  Serial.begin(9600);
  pinMode(RAIN_PWR_PIN, OUTPUT);
  digitalWrite(RAIN_PWR_PIN, LOW);
  pinMode(RAIN_DIG_PIN, INPUT);
  Serial.println(F("=== Test 06: rain sensor ==="));
  Serial.println(F("Hint: touch the plate with a wet finger -> raw value falls."));
}

void loop() {
  digitalWrite(RAIN_PWR_PIN, HIGH);
  delay(100);  // let the board settle
  int raw = analogRead(RAIN_ANA_PIN);
  bool wetFlag = digitalRead(RAIN_DIG_PIN) == LOW;
  digitalWrite(RAIN_PWR_PIN, LOW);  // off between reads to slow plate corrosion

  Serial.print(F("Raw: "));
  Serial.print(raw);
  Serial.print(F("   Class: "));
  if (raw >= RAIN_DRY) Serial.print(F("dry"));
  else if (raw >= RAIN_DAMP) Serial.print(F("damp"));
  else if (raw >= RAIN_LIGHT) Serial.print(F("light rain"));
  else Serial.print(F("heavy rain"));
  Serial.print(F("   D0: "));
  if (wetFlag) Serial.print(F("WET"));
  else Serial.print(F("dry"));
  if (raw < 20) Serial.print(F("   (near 0 while dry: check VCC->D4 and GND)"));
  Serial.println();
  delay(900);
}
