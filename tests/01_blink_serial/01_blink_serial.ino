// Test 01: board, USB and serial.
// PASS: onboard LED (pin 13, "L") blinks once per second and the counter climbs in the Serial Monitor (9600 baud).

unsigned long count = 0;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(9600);
  Serial.println(F("=== Test 01: blink + serial ==="));
  Serial.println(F("PASS if the L LED blinks and the counter below increases."));
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
  Serial.print(F("Tick "));
  Serial.println(++count);
}
