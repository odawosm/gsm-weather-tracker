// SMS weather station: DHT11 + LDR + rain sensor + SSD1306 OLED + SIM800L.
// Libraries: DHT sensor library (Adafruit), Adafruit Unified Sensor, U8g2 (for U8x8). Settings in config.h.
// Serial (USB, 9600): one JSON line per reading, plus {"sms":...} lines for SMS events. Other lines start with '#'.
// OLED uses U8x8 (text only, no frame buffer): the Adafruit 1 KB buffer left too little RAM for the GSM code.
// Runs without a working SIM800L: sensors, OLED and JSON keep going and the module is retried every GSM_CHECK_MS.

#include <SoftwareSerial.h>
#include <U8x8lib.h>
#include <DHT.h>
#include "config.h"

#define FS(p) ((const __FlashStringHelper *)(p))
#define COUNT(a) (sizeof(a) / sizeof(a[0]))

SoftwareSerial sim(PIN_SIM_RX, PIN_SIM_TX);
U8X8_SSD1306_128X64_NONAME_HW_I2C oled(U8X8_PIN_NONE);
DHT dht(PIN_DHT, DHT11);

// ---------- Readings ----------
struct Reading {
  int t10;       // temperature in tenths of a degree; keeps the last valid value if the DHT fails
  int h;
  int light;     // %
  int rainRaw;
  byte rain;     // index into RAIN_NAMES
  bool dhtOk;
};
Reading cur;

struct Sample { int t10; byte h; byte light; };
Sample ring[RING_SIZE];  // one sample per TREND_SAMPLE_MS
byte ringHead = 0, ringCount = 0;

const char RAIN_0[] PROGMEM = "dry";
const char RAIN_1[] PROGMEM = "damp";
const char RAIN_2[] PROGMEM = "light rain";
const char RAIN_3[] PROGMEM = "heavy rain";
const char *const RAIN_NAMES[] PROGMEM = { RAIN_0, RAIN_1, RAIN_2, RAIN_3 };
const char *rainName(byte i) { return (const char *)pgm_read_word(&RAIN_NAMES[i]); }

// ---------- GSM state ----------
bool gsmAlive = false;   // module answers AT
bool smsReady = false;   // text mode + SMS forwarding configured
int csq = -1;            // 0-31, -1 unknown
int creg = -1;           // +CREG stat: 1 home, 5 roaming, 2 searching, 0 not searching, 3 denied
char line[64];           // one line from the module (SMS bodies are truncated; commands are short)
byte lineLen = 0;
char msg[161];           // outgoing SMS, one SMS max
char smsFrom[16], smsBody[16];
bool expectBody = false, smsPending = false;
char lastSms[14] = "--";

// Online forecast from the laptop dashboard ("FC:<text>" lines over USB), e.g. "Fcst: rain 40% next 3h."
char forecast[25];
unsigned long forecastAt;
char usbLine[32];
byte usbLen = 0;

const char T_OK[] PROGMEM = "OK";

// ---------- Alerts ----------
enum { AL_RAIN, AL_TEMP, AL_LIGHT, AL_COUNT };
unsigned long lastAlert[AL_COUNT];
bool alertSent[AL_COUNT];
bool prevWet = false, havePrev = false;
int8_t prevTempZone = 0;  // -1 below TEMP_LOW_C, 0 normal, 1 above TEMP_HIGH_C

unsigned long lastRead, lastSample, lastGsmCheck, lastHourly;

int freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

// ================= GSM =================

// Read available bytes; true when a full line is in `line`. The CMGS '>' prompt has no newline, so it's returned alone.
bool readLine() {
  while (sim.available()) {
    char c = sim.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[lineLen] = '\0';
      lineLen = 0;
      return true;
    }
    if (c == '>' && lineLen == 0) {
      line[0] = '>';
      line[1] = '\0';
      return true;
    }
    if (lineLen < sizeof(line) - 1) line[lineLen++] = c;
  }
  return false;
}

// Unsolicited lines: incoming SMS and module reboots. True if the line was consumed.
bool handleUrc() {
  if (expectBody) {  // line after +CMT: is the message text
    expectBody = false;
    strncpy(smsBody, line, sizeof(smsBody) - 1);
    smsBody[sizeof(smsBody) - 1] = '\0';
    smsPending = true;  // ponytail: one pending slot, a second SMS before it's handled replaces it
    return true;
  }
  // +CMT: "+2547XXXXXXXX","","26/10/14,12:30:00+12"
  if (strncmp_P(line, PSTR("+CMT:"), 5) == 0) {
    char *a = strchr(line, '"');
    char *b = a ? strchr(a + 1, '"') : NULL;
    if (b) {
      *b = '\0';
      strncpy(smsFrom, a + 1, sizeof(smsFrom) - 1);
      smsFrom[sizeof(smsFrom) - 1] = '\0';
      expectBody = true;
    }
    return true;
  }
  if (strcmp_P(line, PSTR("RDY")) == 0) {  // module restarted (brownout?): re-init on next check
    gsmAlive = smsReady = false;
    return true;
  }
  return false;
}

void gsmPoll() {
  while (readLine())
    if (line[0]) handleUrc();
}

// Wait for a line containing `token` (PROGMEM). False on ERROR or timeout. URCs are handled while waiting.
bool waitFor(const char *token, unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    if (!readLine() || !line[0] || handleUrc()) continue;
    if (strstr_P(line, token)) return true;
    if (strstr_P(line, PSTR("ERROR"))) return false;
  }
  return false;
}

bool at(const __FlashStringHelper *cmd, const char *token = T_OK, unsigned long ms = 1000) {
  sim.println(cmd);
  return waitFor(token, ms);
}

bool setupSms() {
  return at(F("AT+CMGF=1")) && at(F("AT+CNMI=2,2,0,0,0"));
}

void gsmInit() {
  gsmAlive = smsReady = false;
  for (byte i = 0; i < 3 && !gsmAlive; i++) gsmAlive = at(F("AT"), T_OK, 500);
  if (!gsmAlive) return;
  at(F("ATE0"));
  // Network time for report timestamps; takes effect after the module's next power cycle.
  at(F("AT+CLTS=1"));
  at(F("AT&W"));
  smsReady = setupSms();  // may fail without a SIM; retried once registered
}

void gsmCheck() {
  if (!gsmAlive) {
    gsmInit();
    if (!gsmAlive) {
      csq = creg = -1;
      return;
    }
  }
  if (!at(F("AT+CSQ"), PSTR("+CSQ:"))) {  // stopped answering: re-init next time
    gsmAlive = smsReady = false;
    csq = creg = -1;
    return;
  }
  csq = atoi(line + 6);
  if (csq == 99) csq = -1;
  if (at(F("AT+CREG?"), PSTR("+CREG:"))) {
    char *p = strchr(line, ',');
    creg = p ? atoi(p + 1) : -1;
  }
  if ((creg == 1 || creg == 5) && !smsReady) smsReady = setupSms();
}

void fmtUptime(char *out) {
  unsigned long m = millis() / 60000UL;
  snprintf_P(out, 14, PSTR("up %luh%02lum"), m / 60, m % 60);
}

// "14/10 12:30" from the module clock, or uptime if the clock isn't synced / no module.
void fmtWhen(char *out) {
  if (gsmAlive && at(F("AT+CCLK?"), PSTR("+CCLK:"))) {
    char *q = strchr(line, '"');  // "yy/MM/dd,hh:mm:ss+zz"
    if (q && atoi(q + 1) >= 24) {  // unsynced clock reports year 04
      snprintf_P(out, 14, PSTR("%.2s/%.2s %.5s"), q + 7, q + 4, q + 10);
      return;
    }
  }
  fmtUptime(out);
}

void printJsonStr(const char *s) {
  for (; *s; s++) {
    if ((byte)*s < 32) continue;
    if (*s == '"' || *s == '\\') Serial.print('\\');
    Serial.print(*s);
  }
}

// {"sms":"in|out|ignored|fail","num":"...","text":"..."} for the dashboard's SMS log.
void logSms(const char *dir, const char *num, const char *text) {
  Serial.print(F("{\"sms\":\""));
  Serial.print(FS(dir));
  Serial.print(F("\",\"num\":\""));
  printJsonStr(num);
  Serial.print(F("\",\"text\":\""));
  printJsonStr(text);
  Serial.println(F("\"}"));
}

bool sendSms(const char *to, const char *text) {
  if (!gsmAlive || !smsReady || strchr(to, 'X')) return false;
  sim.print(F("AT+CMGS=\""));
  sim.print(to);
  sim.println('"');
  bool ok = waitFor(PSTR(">"), 5000);
  if (ok) {
    sim.print(text);
    sim.write(26);  // Ctrl+Z sends
    ok = waitFor(PSTR("+CMGS:"), 60000);
  } else {
    sim.write(27);  // ESC aborts a half-open prompt
  }
  if (ok) fmtWhen(lastSms);
  logSms(ok ? PSTR("out") : PSTR("fail"), to, text);
  return ok;
}

// ================= Sensors =================

// Wait without starving the SIM800L input (its 64-byte buffer fills in ~70 ms).
void idle(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) gsmPoll();
}

void readSensors() {
  // ponytail: the DHT read blocks interrupts ~4 ms, so a few SIM800L bytes can be lost if an SMS lands exactly then
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  cur.dhtOk = !isnan(t) && !isnan(h);
  if (cur.dhtOk) {
    cur.t10 = (int)(t * 10 + 0.5) + TEMP_OFFSET_C10;
    cur.h = constrain((int)(h + 0.5) + HUM_OFFSET, 0, 100);
  }
  cur.light = constrain(map(analogRead(PIN_LDR), LIGHT_RAW_DARK, LIGHT_RAW_BRIGHT, 0, 100), 0, 100);

  digitalWrite(PIN_RAIN_PWR, HIGH);
  idle(100);
  cur.rainRaw = analogRead(PIN_RAIN_A);
  digitalWrite(PIN_RAIN_PWR, LOW);  // off between reads to slow plate corrosion
  cur.rain = cur.rainRaw >= RAIN_DRY ? 0 : cur.rainRaw >= RAIN_DAMP ? 1 : cur.rainRaw >= RAIN_LIGHT ? 2 : 3;
}

void pushSample() {
  ring[ringHead] = { cur.t10, (byte)cur.h, (byte)cur.light };
  ringHead = (ringHead + 1) % RING_SIZE;
  if (ringCount < RING_SIZE) ringCount++;
}

const Sample &oldest() { return ring[ringCount < RING_SIZE ? 0 : ringHead]; }
const Sample &recent(byte k) { return ring[(ringHead + RING_SIZE - k) % RING_SIZE]; }  // k = 1 is newest

void printJson() {
  Serial.print(F("{\"t\":"));
  if (cur.dhtOk) Serial.print(cur.t10 / 10.0, 1); else Serial.print(F("null"));
  Serial.print(F(",\"h\":"));
  if (cur.dhtOk) Serial.print(cur.h); else Serial.print(F("null"));
  Serial.print(F(",\"light\":"));
  Serial.print(cur.light);
  Serial.print(F(",\"rain\":\""));
  Serial.print(FS(rainName(cur.rain)));
  Serial.print(F("\",\"rain_raw\":"));
  Serial.print(cur.rainRaw);
  Serial.print(F(",\"csq\":"));
  if (csq >= 0) Serial.print(csq); else Serial.print(F("null"));
  Serial.println('}');
}

// ================= Messages =================

// Appends "Temp 24.5C, Hum 61%, Light 72%, Rain: dry." to msg.
void addReadings() {
  size_t n = strlen(msg);
  if (cur.dhtOk)
    snprintf_P(msg + n, sizeof(msg) - n, PSTR("Temp %d.%dC, Hum %d%%, "), cur.t10 / 10, cur.t10 % 10, cur.h);
  else
    strlcat_P(msg, PSTR("Temp/Hum n/a, "), sizeof(msg));
  n = strlen(msg);
  snprintf_P(msg + n, sizeof(msg) - n, PSTR("Light %d%%, Rain: %S."), cur.light, rainName(cur.rain));
}

bool addChange(int d, int threshold, const char *name, bool any) {
  if (abs(d) < threshold) return false;
  if (any) strlcat_P(msg, PSTR(", "), sizeof(msg));
  strlcat_P(msg, name, sizeof(msg));
  strlcat_P(msg, d > 0 ? PSTR(" rising") : PSTR(" falling"), sizeof(msg));
  return true;
}

// Compares now with the oldest sample in the buffer (up to 1 h ago).
void addTrend() {
  strlcat_P(msg, PSTR(" Trend: "), sizeof(msg));
  if (ringCount < 2) {
    strlcat_P(msg, PSTR("n/a yet."), sizeof(msg));
    return;
  }
  const Sample &o = oldest();
  bool any = false;
  any |= addChange(cur.t10 - o.t10, TREND_TEMP_C10, PSTR("temp"), any);
  any |= addChange(cur.h - o.h, TREND_HUM, PSTR("humidity"), any);
  any |= addChange(cur.light - o.light, TREND_LIGHT, PSTR("light"), any);
  strlcat_P(msg, any ? PSTR(".") : PSTR("steady."), sizeof(msg));
}

void buildReport() {
  char when[14];
  fmtWhen(when);
  snprintf_P(msg, sizeof(msg), PSTR("Weather %s: "), when);
  addReadings();
  addTrend();
  if (forecast[0] && millis() - forecastAt < FORECAST_MAX_AGE_MS) {  // worst case still fits one SMS
    strlcat_P(msg, PSTR(" "), sizeof(msg));
    strlcat(msg, forecast, sizeof(msg));
  }
}

void usbPoll() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (usbLen < sizeof(usbLine) - 1) usbLine[usbLen++] = c;
      continue;
    }
    usbLine[usbLen] = '\0';
    usbLen = 0;
    if (strncmp_P(usbLine, PSTR("FC:"), 3) == 0) {
      strncpy(forecast, usbLine + 3, sizeof(forecast) - 1);  // last byte stays '\0'
      forecastAt = millis();
      Serial.print(F("# forecast: "));
      Serial.println(forecast);
    }
  }
}

const char *netName() {
  switch (creg) {
    case 1: return PSTR("home");
    case 5: return PSTR("roaming");
    case 2: return PSTR("searching");
    case 3: return PSTR("denied");
    case 0: return PSTR("none");
    default: return PSTR("?");
  }
}

void buildStatus() {
  char up[14];
  fmtUptime(up);
  snprintf_P(msg, sizeof(msg), PSTR("Status: Net %S, Signal %d/31, %s, RAM %dB free, Last SMS %s."),
             netName(), csq, up, freeRam(), lastSms);
}

void buildHelp() {
  strcpy_P(msg, PSTR("Commands: WEATHER = current report, STATUS = signal, uptime, RAM, HELP = this list."));
}

bool authorized(const char *num) {
  for (byte i = 0; i < COUNT(AUTHORIZED); i++)
    if (strcmp(num, AUTHORIZED[i]) == 0) return true;
  return false;
}

void handleSms() {
  char from[sizeof(smsFrom)], body[sizeof(smsBody)];
  strcpy(from, smsFrom);  // copy first: a new SMS may arrive while we reply
  strcpy(body, smsBody);
  smsPending = false;

  if (!authorized(from)) {
    logSms(PSTR("ignored"), from, body);
    return;
  }
  logSms(PSTR("in"), from, body);
  fmtWhen(lastSms);

  char *c = body;  // trim + uppercase
  while (*c == ' ') c++;
  for (char *p = c; *p; p++) *p = toupper(*p);
  for (int n = strlen(c); n && c[n - 1] == ' '; n--) c[n - 1] = '\0';

  if (strcmp_P(c, PSTR("WEATHER")) == 0) buildReport();
  else if (strcmp_P(c, PSTR("STATUS")) == 0) buildStatus();
  else buildHelp();  // HELP or anything unrecognised
  sendSms(from, msg);
}

// ================= Alerts =================

void alert(byte type, const char *what) {
  if (alertSent[type] && millis() - lastAlert[type] < ALERT_COOLDOWN_MS) return;
  alertSent[type] = true;
  lastAlert[type] = millis();
  char when[14];
  fmtWhen(when);
  snprintf_P(msg, sizeof(msg), PSTR("ALERT %s: %S. "), when, what);
  addReadings();
  Serial.print(F("# alert: "));
  Serial.println(msg);
  for (byte i = 0; i < COUNT(AUTHORIZED); i++) sendSms(AUTHORIZED[i], msg);
}

void checkAlerts() {
  bool wet = cur.rainRaw < RAIN_DAMP;  // light rain or heavier
  int8_t zone = !cur.dhtOk ? prevTempZone
              : cur.t10 > TEMP_HIGH_C * 10 ? 1
              : cur.t10 < TEMP_LOW_C * 10 ? -1 : 0;
  if (havePrev) {
    if (wet && !prevWet) alert(AL_RAIN, PSTR("Rain started"));
    if (zone != prevTempZone && zone != 0)
      alert(AL_TEMP, zone > 0 ? PSTR("High temperature") : PSTR("Low temperature"));
  }
  // Light drop vs the brightest recent sample (~20 min). ponytail: sunset also triggers this; gate on time of day if it's noisy.
  int brightest = 0;
  for (byte k = 1; k <= LIGHT_DROP_SAMPLES && k <= ringCount; k++)
    if (recent(k).light > brightest) brightest = recent(k).light;
  if (brightest - cur.light > LIGHT_DROP_PCT) alert(AL_LIGHT, PSTR("Light dropped sharply (storm?)"));

  prevWet = wet;
  prevTempZone = zone;
  havePrev = true;
}

// ================= Display (16 x 8 characters) =================

void row(byte r, const __FlashStringHelper *label) {
  oled.clearLine(r);
  oled.setCursor(0, r);
  oled.print(label);
}

void draw() {
  // Weather only: no SIM800L details (GSM, network, signal, last SMS) on the screen. STATUS by SMS still has them.
  oled.setInverseFont(1);
  row(0, F(" WEATHER        "));
  oled.setInverseFont(0);
  row(2, F("Temp  "));
  if (cur.dhtOk) { oled.print(cur.t10 / 10.0, 1); oled.print(F(" C")); } else oled.print(F("err"));
  row(3, F("Hum   "));
  if (cur.dhtOk) { oled.print(cur.h); oled.print(F(" %")); } else oled.print(F("err"));
  row(4, F("Light "));
  oled.print(cur.light);
  oled.print(F(" %"));
  row(5, F("Rain  "));
  oled.print(FS(rainName(cur.rain)));
  row(6, F(""));
  char up[14];
  fmtUptime(up);
  row(7, F(""));
  oled.print(up);
}

// ================= Main =================

void setup() {
  Serial.begin(9600);
  sim.begin(9600);
  pinMode(PIN_RAIN_PWR, OUTPUT);
  digitalWrite(PIN_RAIN_PWR, LOW);
  pinMode(PIN_RAIN_DIG, INPUT);
  dht.begin();
  oled.setI2CAddress(OLED_ADDR * 2);
  oled.begin();
  oled.setFont(u8x8_font_chroma48medium8_r);
  oled.drawString(0, 0, "Starting...");
  Serial.println(F("# weather station starting"));

  gsmInit();
  Serial.println(gsmAlive ? F("# GSM ok") : F("# GSM no reply, will retry"));
  if (gsmAlive) gsmCheck();

  readSensors();
  pushSample();
  printJson();
  checkAlerts();
  draw();
  lastRead = lastSample = lastGsmCheck = lastHourly = millis();
  Serial.print(F("# free RAM "));
  Serial.println(freeRam());
}

void loop() {
  gsmPoll();
  usbPoll();
  if (smsPending) handleSms();

  unsigned long now = millis();
  if (now - lastRead >= READ_INTERVAL_MS) {
    lastRead = now;
    readSensors();
    if (now - lastSample >= TREND_SAMPLE_MS) {
      lastSample = now;
      pushSample();
    }
    printJson();
    checkAlerts();
    draw();
  }
  if (now - lastGsmCheck >= GSM_CHECK_MS) {
    lastGsmCheck = now;
    gsmCheck();
  }
#if HOURLY_REPORT
  if (now - lastHourly >= HOURLY_MS) {
    lastHourly = now;
    buildReport();
    for (byte i = 0; i < COUNT(SUBSCRIBERS); i++) sendSms(SUBSCRIBERS[i], msg);
  }
#endif
}
