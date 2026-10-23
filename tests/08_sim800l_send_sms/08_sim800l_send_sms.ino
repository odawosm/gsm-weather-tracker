// Test 08: send one SMS. NEEDS an active SIM with SMS credit (stops with a clear message if there is none).
// Set PHONE below, upload, open the Serial Monitor at 9600. Sends once per reset.

#include <SoftwareSerial.h>

const char *PHONE = "+2547XXXXXXXX";  // international format
const char MESSAGE[] PROGMEM = "Weather station test OK";

SoftwareSerial sim(7, 8);  // RX = D7, TX = D8
char reply[128];

// See test 07: collects the reply, true if `token` (PROGMEM) seen, false on ERROR/timeout.
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
  for (size_t i = 0; i < n; i++) if (reply[i] != '\r') Serial.print(reply[i]);
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

void fail(const __FlashStringHelper *why) {
  Serial.print(F("FAIL: "));
  Serial.println(why);
  for (;;) {}
}

void setup() {
  Serial.begin(9600);
  sim.begin(9600);
  Serial.println(F("=== Test 08: send SMS ==="));
  if (strchr(PHONE, 'X')) fail(F("set PHONE at the top of the sketch first."));
  delay(3000);

  bool alive = false;
  for (byte i = 0; i < 5 && !alive; i++) alive = cmd(F("AT"));
  if (!alive) fail(F("no reply to AT. Run test 07 first."));

  cmd(F("AT+CPIN?"));
  if (!strstr_P(reply, PSTR("READY"))) fail(F("SIM not ready (none inserted, or PIN-locked). Power off, insert SIM, retry."));

  Serial.println(F("Waiting for network registration (up to 60 s)..."));
  bool registered = false;
  for (byte i = 0; i < 30 && !registered; i++) {
    cmd(F("AT+CREG?"));
    registered = strstr_P(reply, PSTR(",1")) || strstr_P(reply, PSTR(",5"));
    if (!registered) delay(2000);
  }
  if (!registered) fail(F("not registered. Check antenna, signal (AT+CSQ) and that the SIM works in a phone."));

  if (!cmd(F("AT+CMGF=1"))) fail(F("could not set text mode."));

  Serial.print(F(">> AT+CMGS=\""));
  Serial.print(PHONE);
  Serial.println('"');
  while (sim.available()) sim.read();
  sim.print(F("AT+CMGS=\""));
  sim.print(PHONE);
  sim.println('"');
  if (!waitFor(PSTR(">"), 5000)) fail(F("no '>' prompt from AT+CMGS."));

  Serial.print(F(">> "));
  Serial.println((const __FlashStringHelper *)MESSAGE);
  sim.print((const __FlashStringHelper *)MESSAGE);
  sim.write(26);  // Ctrl+Z sends the message
  // Network can take a while; 60 s covers slow 2G cells.
  if (waitFor(PSTR("+CMGS:"), 60000)) Serial.println(F("PASS: SMS sent. Check the phone."));
  else fail(F("send failed. Check SMS credit, number format, and the 4.0V supply (brownouts reset the module while transmitting)."));
}

void loop() {}
