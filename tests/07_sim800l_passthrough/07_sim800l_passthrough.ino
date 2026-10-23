// Test 07: SIM800L health check + Serial <-> SIM800L bridge.
// Wiring: SIM TXD -> shifter -> D7 (Arduino RX), D8 -> shifter -> SIM RXD (Arduino TX). Common GND with the LM2596.
// Serial Monitor: 9600 baud, line ending "Both NL & CR".
// Works WITHOUT a SIM: AT, AT+CSQ and AT+CBC still answer; CPIN/CREG/COPS will report "no SIM", which is expected.

#include <SoftwareSerial.h>

SoftwareSerial sim(7, 8);  // RX = D7, TX = D8

char reply[128];

// Collect the module's reply into `reply` and echo it. Returns true if `token` (PROGMEM) was seen,
// false on ERROR or timeout. Keeps reading until the line goes quiet so trailing text (e.g. "+CME ERROR: 10") is kept.
bool waitFor(const char *token, unsigned long timeoutMs) {
  size_t n = 0;
  bool done = false, ok = false;
  unsigned long start = millis(), last = millis();
  reply[0] = '\0';
  while (millis() - start < timeoutMs && !(done && millis() - last > 50)) {
    while (sim.available()) {
      char c = sim.read();
      last = millis();
      if (n < sizeof(reply) - 1) { reply[n++] = c; reply[n] = '\0'; }
    }
    if (!done && strstr_P(reply, token)) done = ok = true;
    if (!done && strstr_P(reply, PSTR("ERROR"))) done = true;
  }
  Serial.print(F("<< "));
  for (size_t i = 0; i < n; i++) {
    if (reply[i] == '\r') continue;
    Serial.print(reply[i]);
    if (reply[i] == '\n' && i + 1 < n) Serial.print(F("   "));
  }
  if (n == 0) Serial.print(F("(no reply)"));
  Serial.println();
  return ok;
}

bool cmd(const __FlashStringHelper *c, unsigned long timeoutMs = 1000) {
  while (sim.available()) sim.read();
  Serial.print(F(">> "));
  Serial.println(c);
  sim.println(c);
  return waitFor(PSTR("OK"), timeoutMs);
}

// Integer after `prefix` (PROGMEM), skipping `commas` commas first, or -1.
int field(const char *prefix, byte commas) {
  char *p = strstr_P(reply, prefix);
  if (!p) return -1;
  p += strlen_P(prefix);
  while (commas--) {
    p = strchr(p, ',');
    if (!p) return -1;
    p++;
  }
  return atoi(p);
}

void hintsNoAt() {
  Serial.println(F("FAIL: no reply to AT. Check, in order:"));
  Serial.println(F("  1. LM2596 output is 4.0V (multimeter) and reaches SIM VCC/GND; 1000uF cap fitted."));
  Serial.println(F("  2. Common ground: LM2596 OUT- joined to Arduino GND."));
  Serial.println(F("  3. TX/RX swapped? SIM TXD -> D7, SIM RXD <- D8 (through the shifter, HV=5V, LV=3.3V)."));
  Serial.println(F("  4. Baud: module may be at another rate. Change sim.begin() to 115200, then send AT+IPR=9600 and AT&W."));
  Serial.println(F("  5. NET LED: no blinking at all = no power / not booted."));
}

void healthCheck() {
  // A fresh module auto-detects the baud rate from the first few "AT"s, so try a few times.
  bool alive = false;
  for (byte i = 0; i < 5 && !alive; i++) alive = cmd(F("AT"));
  if (!alive) { hintsNoAt(); return; }
  Serial.println(F("-> Module responding."));
  delay(1000);

  cmd(F("AT+CPIN?"));
  bool simReady = strstr_P(reply, PSTR("READY"));
  if (simReady) Serial.println(F("-> SIM ready."));
  else if (strstr_P(reply, PSTR("SIM PIN"))) Serial.println(F("-> SIM is PIN-locked. Disable the PIN in a phone first."));
  else Serial.println(F("-> No SIM detected (expected if none is inserted). Power OFF before inserting one."));
  delay(1000);

  cmd(F("AT+CSQ"));
  int csq = field(PSTR("+CSQ: "), 0);
  Serial.print(F("-> "));
  if (csq < 0) Serial.println(F("Could not read signal."));
  else if (csq == 99) Serial.println(F("Signal unknown (99): no network measured. Check antenna; can be normal without a SIM."));
  else {
    Serial.print(F("Signal "));
    Serial.print(csq);
    Serial.print(F("/31 ("));
    Serial.print(-113 + 2 * csq);
    Serial.print(F(" dBm): "));
    if (csq < 10) Serial.println(F("weak - move the antenna / near a window."));
    else if (csq < 15) Serial.println(F("OK."));
    else if (csq < 20) Serial.println(F("good."));
    else Serial.println(F("excellent."));
  }
  delay(1000);

  cmd(F("AT+CREG?"));
  int reg = field(PSTR("+CREG: "), 1);
  Serial.print(F("-> "));
  switch (reg) {
    case 1: Serial.println(F("Registered on home network.")); break;
    case 5: Serial.println(F("Registered, roaming.")); break;
    case 2: Serial.println(F("Not registered, searching (NET LED fast blink). Wait 30-60 s.")); break;
    case 3: Serial.println(F("Registration denied: SIM inactive or barred. Test the SIM in a phone.")); break;
    case 0: Serial.println(simReady ? F("Not registered, not searching.") : F("Not registered (expected: no SIM).")); break;
    default: Serial.println(F("Registration unknown."));
  }
  delay(1000);

  cmd(F("AT+COPS?"));
  char *q = strchr(reply, '"');
  Serial.print(F("-> "));
  if (q) {
    char *e = strchr(q + 1, '"');
    if (e) *e = '\0';
    Serial.print(F("Operator: "));
    Serial.println(q + 1);
  } else {
    Serial.println(simReady ? F("No operator selected yet.") : F("No operator (expected: no SIM)."));
  }
  delay(1000);

  cmd(F("AT+CBC"));
  int mv = field(PSTR("+CBC: "), 2);
  Serial.print(F("-> "));
  if (mv < 0) Serial.println(F("Could not read supply voltage."));
  else {
    Serial.print(F("Supply "));
    Serial.print(mv);
    Serial.print(F(" mV: "));
    if (mv < 3500) Serial.println(F("TOO LOW - module will reset when transmitting. Raise LM2596 toward 4.0V."));
    else if (mv < 3800) Serial.println(F("low - OK at idle, may brown out on SMS. Aim for ~4.0V."));
    else if (mv <= 4300) Serial.println(F("good."));
    else Serial.println(F("TOO HIGH - turn the LM2596 down to 4.0V now."));
  }
}

void setup() {
  Serial.begin(9600);
  sim.begin(9600);
  Serial.println(F("=== Test 07: SIM800L check + passthrough ==="));
  Serial.println(F("Waiting 3 s for the module to boot..."));
  delay(3000);
  healthCheck();
  Serial.println();
  Serial.println(F("Passthrough active. Type AT commands (line ending: Both NL & CR)."));
}

void loop() {
  if (Serial.available()) sim.write(Serial.read());
  if (sim.available()) Serial.write(sim.read());
}
