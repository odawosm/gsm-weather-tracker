// Test 09: receive SMS and reply to authorized senders. NEEDS an active SIM.
// PASS: text the module from an authorized phone; the sender and text print here and the phone gets "Got it: <text>".
// Unauthorized senders are printed but not answered.

#include <SoftwareSerial.h>

// Numbers as the network reports them (international format, e.g. +2547...).
const char *const AUTHORIZED[] = { "+2547XXXXXXXX" };
const byte N_AUTHORIZED = sizeof(AUTHORIZED) / sizeof(AUTHORIZED[0]);

SoftwareSerial sim(7, 8);  // RX = D7, TX = D8
char reply[128];
char line[170];       // one line from the module (SMS body up to 160 chars)
char sender[20];
bool expectBody = false;

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

bool authorized(const char *num) {
  for (byte i = 0; i < N_AUTHORIZED; i++)
    if (strcmp(num, AUTHORIZED[i]) == 0) return true;
  return false;
}

void sendSms(const char *to, const char *text) {
  while (sim.available()) sim.read();
  sim.print(F("AT+CMGS=\""));
  sim.print(to);
  sim.println('"');
  if (!waitFor(PSTR(">"), 5000)) { Serial.println(F("FAIL: no '>' prompt.")); return; }
  sim.print(F("Got it: "));
  // Reply must fit one SMS: 160 - strlen("Got it: ") = 152 chars of echoed text.
  for (byte i = 0; i < 152 && text[i]; i++) sim.print(text[i]);
  sim.write(26);
  if (waitFor(PSTR("+CMGS:"), 60000)) Serial.println(F("PASS: reply sent."));
  else Serial.println(F("FAIL: reply not sent (credit? supply voltage?)."));
}

void handleLine() {
  if (expectBody) {
    expectBody = false;
    Serial.print(F("SMS from "));
    Serial.print(sender);
    Serial.print(F(": "));
    Serial.println(line);
    if (authorized(sender)) {
      Serial.println(F("Sender authorized, replying..."));
      sendSms(sender, line);
    } else {
      Serial.println(F("Sender NOT authorized, ignored."));
    }
    return;
  }
  // Header: +CMT: "+2547XXXXXXXX","","26/10/14,12:30:00+12"   (body follows on the next line)
  if (strncmp_P(line, PSTR("+CMT:"), 5) == 0) {
    char *a = strchr(line, '"');
    char *b = a ? strchr(a + 1, '"') : NULL;
    if (!b) return;
    *b = '\0';
    strncpy(sender, a + 1, sizeof(sender) - 1);
    sender[sizeof(sender) - 1] = '\0';
    expectBody = true;
  } else if (line[0]) {
    Serial.print(F("[module] "));
    Serial.println(line);
  }
}

void setup() {
  Serial.begin(9600);
  sim.begin(9600);
  Serial.println(F("=== Test 09: receive SMS ==="));
  if (strchr(AUTHORIZED[0], 'X')) Serial.println(F("NOTE: AUTHORIZED still has the placeholder; replies will not be sent."));
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
  if (!registered) fail(F("not registered. Check antenna and that the SIM works in a phone."));

  if (!cmd(F("AT+CMGF=1"))) fail(F("could not set text mode."));
  if (!cmd(F("AT+CNMI=2,2,0,0,0"))) fail(F("could not enable SMS forwarding (CNMI)."));
  Serial.println(F("Ready. Send an SMS to the module's number."));
}

void loop() {
  static byte n = 0;
  while (sim.available()) {
    char c = sim.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[n] = '\0';
      n = 0;
      handleLine();
    } else if (n < sizeof(line) - 1) {
      line[n++] = c;
    }
  }
}
