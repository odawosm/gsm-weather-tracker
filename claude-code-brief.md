# Brief for Claude Code: SMS weather station

Read `sms-weather-station-notes.md` in this folder first for the full background. This brief is the work order. Build everything described here, in the order given, and keep each part runnable on its own.

## Hardware summary (final)

- Arduino Uno R3
- SIM800L GSM module (red core board, pins: NET, VCC, RST, RXD, TXD, GND), 2G, Safaricom micro SIM
- 4-channel bidirectional logic level shifter between Arduino and SIM800L (HV = 5V, LV = 3.3V)
- DHT11 temperature/humidity
- LDR + 10kΩ divider
- Rain sensor module (plate + comparator board, analog and digital outputs)
- 0.96" OLED SSD1306, I2C, pin order on the board: GND, VCC, SCL, SDA (address usually 0x3C)
- LM2596 buck converter set to 4.0V + 1000µF capacitor powering the SIM800L; bench PSU at 12V
- Spare: PCF8574 I2C backpack (HW-61) for a 16x2 LCD, not used unless a 16x2 LCD is added later (address 0x27 or 0x3F)

## Pin map

| Arduino pin | Connected to |
|---|---|
| D2 | DHT11 DATA |
| D3 | Rain sensor D0 (digital flag) |
| D4 | Rain sensor VCC (switch HIGH only while reading, then LOW) |
| D7 | SIM800L TXD via level shifter (Arduino RX, SoftwareSerial) |
| D8 | SIM800L RXD via level shifter (Arduino TX, SoftwareSerial) |
| A0 | LDR divider midpoint (higher value = brighter) |
| A1 | Rain sensor A0 (~1023 dry, falls toward 0 when wet) |
| A4 | OLED SDA |
| A5 | OLED SCL |
| 5V | DHT11 VCC, OLED VCC, LDR top, level shifter HV |
| 3.3V | Level shifter LV |
| GND | All grounds, including LM2596 OUT− (common ground) |
| Serial (USB) | 9600 baud, JSON lines to the laptop dashboard |

SIM800L: 9600 baud, SoftwareSerial on D7/D8, AT commands, text mode SMS.

## Part 1: Module test sketches

Create a folder `tests/` with one Arduino sketch per module. Each must compile for the Uno, print clear PASS/FAIL style output on the hardware Serial monitor at 9600 baud, and need no other module connected. Use the pin map above. Keep strings in `F()`.

1. `tests/01_blink_serial/` — blink the onboard LED and print a counter. Confirms board, USB and serial work.
2. `tests/02_i2c_scanner/` — scan the I2C bus and print every address found. Expect 0x3C for the OLED (and 0x27/0x3F if the LCD backpack is connected).
3. `tests/03_oled/` — Adafruit_SSD1306 + Adafruit_GFX. Show a title, a counter and a filled bar that animates. Report over Serial if `display.begin()` fails.
4. `tests/04_dht11/` — read temperature and humidity every 2 s, print values, print an error if the read returns NaN. Note DHT11 range (0–50 °C, 20–90 %RH).
5. `tests/05_ldr/` — print raw A0 value and a 0–100 % light estimate every 500 ms. Include a hint: cover the LDR and the value should fall.
6. `tests/06_rain/` — set D4 HIGH, wait 100 ms, read A1 and D3, set D4 LOW, print raw value, a classification (dry / damp / light rain / heavy rain using thresholds as constants) and the digital flag. Repeat every second. Hint: touch the plate with a wet finger.
7. `tests/07_sim800l_passthrough/` — SoftwareSerial bridge: anything typed in the Serial monitor goes to the SIM800L and replies come back. On startup, automatically send `AT`, `AT+CSQ` (signal), `AT+CREG?` (registration), `AT+COPS?` (operator) and `AT+CBC` (supply voltage) with 1 s gaps, and print a one-line interpretation of each reply (e.g. "Registered on home network", "Signal 18/31: good"). If no reply to `AT`, print troubleshooting hints: check 4.0V supply, common ground, TX/RX swapped, baud rate (try `AT+IPR=9600`).
8. `tests/08_sim800l_send_sms/` — send one SMS "Weather station test OK" to a number stored in a `const char*` at the top of the file (placeholder `+2547XXXXXXXX`). Print each AT step and the final `+CMGS` / ERROR result.
9. `tests/09_sim800l_receive_sms/` — set text mode and `AT+CNMI=2,2,0,0,0`, print any incoming SMS with sender number and text, and reply "Got it: <text>" to the sender if the sender is in a small authorized list.
10. `tests/10_all_sensors_oled/` — DHT11 + LDR + rain + OLED together, no GSM. Show all readings on the OLED and print a JSON line over Serial every 5 s. Also print free RAM so we know the margin before adding the GSM code.

Add `tests/README.md` with the order to run them, what a passing result looks like for each, and the libraries to install (DHT sensor library by Adafruit, Adafruit Unified Sensor, Adafruit GFX, Adafruit SSD1306).

## Part 2: Main firmware `firmware/weather_station/weather_station.ino`

- Read DHT11, LDR and rain sensor every 5 s (rain sensor powered via D4 only during the read).
- Show readings on the OLED, rotating between a summary page and a status page (signal strength, registration, last SMS time).
- Print one JSON line per reading over hardware Serial (9600 baud):
  `{"t":24.5,"h":61,"light":72,"rain":"dry","rain_raw":987,"csq":18}`
- Keep a ring buffer of the last 12 readings (one per 5 min) in RAM for trends.
- SMS commands from authorized numbers (list at the top of the file): `WEATHER` (full report), `STATUS` (signal, uptime, free RAM), `HELP`. Ignore unknown senders. Reply must fit in one SMS (≤160 chars).
- Report format example: `Weather 14/10 12:30: Temp 24.5C, Hum 61%, Light 72%, Rain: dry. Trend: humidity rising.`
- Trend line: compare the current reading with the oldest in the buffer; mention humidity, temperature and light changes beyond thresholds.
- Automatic alerts to all authorized numbers: rain starts (dry → wet), temperature crosses configurable high/low thresholds, light drops by more than 40 % within 20 min. Rate-limit each alert type to once per 30 min.
- Optional hourly report to subscribed numbers (compile-time flag).
- GSM init: wait for `AT` OK, set `AT+CMGF=1`, `AT+CNMI=2,2,0,0,0`, check `AT+CREG?` until registered (with timeout and retry). Handle module resets gracefully (re-init if `AT` stops answering).
- Memory: Uno has 2 KB RAM; SSD1306 buffer uses 1 KB. Use `F()` for all literals, avoid `String`, use fixed char buffers. Print free RAM in the STATUS reply. If RAM is too tight, switch the OLED to U8g2 page-buffer mode.
- Make all thresholds, pins and timings `#define`s or `const` at the top. Add a `config.h`.

## Part 3: Dashboard `dashboard/`

Python 3 local web app that combines live sensor readings with online weather.

- `app.py`: Flask. Background thread reads JSON lines from the Arduino serial port (port and baud in `config.py` or env vars; auto-detect common ports; keep running if the Arduino is unplugged and reconnect).
- Fetch Open-Meteo (no API key) current weather + hourly forecast for configurable lat/lon (default Nairobi -1.29, 36.82) every 15 min; cache the result.
- SQLite `data.db` with tables `readings` (timestamp, t, h, light, rain, rain_raw, csq), `forecasts` (fetched_at, target_time, temp, humidity, rain_prob, precipitation), `sms_log` (timestamp, direction, number, text) if the Arduino reports SMS events over serial.
- Routes: `/` (page), `/api/data` (latest reading + latest online conditions + comparison + rain prediction), `/api/history?hours=24`, `/api/accuracy`.
- `templates/index.html` + `static/`: single page, vanilla JS polling every 5 s, Chart.js for a 24 h history chart. Sections: "Now at this location" (sensors), "Online conditions and forecast", "Comparison" (sensor vs online temperature and humidity, with difference), "Rain prediction", "Forecast accuracy", "SMS log".
- Rain prediction: rule-based score from humidity level and trend, light drop rate, rain sensor state and Open-Meteo rain probability → "unlikely / possible / likely". Structure it so a pressure trend input (BMP280) can be added later.
- Forecast accuracy: for each past hour with both a stored forecast and sensor readings, compute absolute error for temperature and humidity; show mean absolute error over the last 24 h / 7 days and how often forecast rain matched the rain sensor.
- `fake_serial.py`: generates realistic JSON lines on a virtual port or feeds the app directly, so the dashboard can be developed and demonstrated without hardware.
- `requirements.txt`, `README.md` with run instructions (`python app.py`, open http://localhost:5000).

## Deliverables checklist

- [ ] `tests/` with sketches 01–10 and README
- [ ] `firmware/weather_station/` with `weather_station.ino` and `config.h`
- [ ] `dashboard/` with app, templates, static, fake_serial, requirements, README
- [ ] Top-level `README.md`: wiring summary (link to notes), build order, how to run tests, firmware and dashboard

Work through Part 1 first and stop after it so the tests can be run on the hardware before Parts 2 and 3.
