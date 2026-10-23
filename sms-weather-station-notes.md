# SMS-based weather report information system

Course project: EEIC/7301 Project Design & Evaluation.

An Arduino Uno reads environmental sensors, shows readings on an OLED, and uses a SIM800L GSM module to send a weather report by SMS when an authorized phone number texts a command (e.g. "WEATHER"). Network: Safaricom (Kenya), 2G.

## Hardware (final choices)

- Arduino Uno R3
- SIM800L GSM module (quad-band, 2G) with antenna and micro SIM (Safaricom, SMS credit)
- DHT11 temperature/humidity sensor
- LDR with 10kΩ resistor (voltage divider)
- Rain sensor module (sensing plate + comparator board with VCC, GND, A0, D0)
- 0.96" OLED, I2C, SSD1306
- LM2596 buck converter, output set to 4.0V
- 1000µF electrolytic capacitor across SIM800L VCC/GND
- 1kΩ + 2kΩ resistors (voltage divider on Arduino TX → SIM800L RX)
- Bench power supply (instead of a 12V adapter): 12V, current limit ≥ 2A
- Breadboard/veroboard, jumper wires, USB cable

## Pin map (Arduino Uno)

| Pin | Connection |
|---|---|
| D2 | DHT11 DATA |
| D3 | Rain sensor D0 (digital rain flag, optional) |
| D4 | Rain sensor VCC (switched on only when reading, to limit plate corrosion) |
| D7 | SIM800L TXD (Arduino RX, SoftwareSerial) |
| D8 | SIM800L RXD (Arduino TX) via 1kΩ series + 2kΩ to GND |
| A0 | LDR divider midpoint |
| A1 | Rain sensor A0 (analog: ~1023 dry → 0 soaked) |
| A4 | OLED SDA |
| A5 | OLED SCL |
| 5V | DHT11 VCC, OLED VCC, LDR top leg |
| GND | All module grounds + LM2596 OUT− (common ground) |

## Wiring details

- DHT11: VCC → 5V, DATA → D2, GND → GND.
- LDR: leg 1 → 5V, leg 2 → A0 and one end of 10kΩ; other end of 10kΩ → GND.
- OLED: VCC → 5V, GND → GND, SDA → A4, SCL → A5.
- Rain sensor board: VCC → D4, GND → GND, A0 → A1, D0 → D3. The two wires from the sensing plate go to the board's plate pins (no polarity). Mount the plate tilted so water runs off.
- SIM800L: TXD → level shifter LV2 → HV2 → Arduino D7. RXD ← level shifter LV1 ← HV1 ← Arduino D8. Shifter HV → 5V, LV → Arduino 3.3V, both shifter GND → GND. RST not connected. VCC/GND from the LM2596 output.
- Fallback without the shifter: D8 → 1kΩ → SIM RXD, 2.2kΩ from SIM RXD to GND; SIM TXD → D7 direct.
- OLED board pin order is GND, VCC, SCL, SDA.
- Capacitor: 1000µF across SIM800L VCC (+, long leg) and GND (−, stripe side), leads kept short, placed as close to the module as possible.

## Power

- Bench PSU set to 12V, current limit ≥ 2A → LM2596 IN+/IN−.
- LM2596 output set to 4.0V (check with a multimeter BEFORE connecting the SIM800L; 5V will damage it) → SIM800L VCC/GND.
- LM2596 OUT− must be joined to Arduino GND (common ground) or serial will not work.
- Arduino: USB during programming/testing. Final build: the same 12V into the Arduino barrel jack.
- Alternative: PSU set to 4.0V, ≥ 2A, straight to the capacitor/SIM800L, skipping the LM2596 (only if the PSU handles 2A bursts cleanly).
- Never power the SIM800L from the Arduino's 5V or 3.3V pins (resets due to 2A bursts).

## SIM800L notes

- Network LED: fast blink (~1/s) = searching; slow blink (~every 3s) = registered.
- Check registration with `AT+CREG?` → `0,1` means registered.
- Use 9600 baud over SoftwareSerial (`AT+IPR=9600` if needed).
- Test the SIM in a phone first (active, has SMS credit).
- Weak signal is the most common failure; use the external antenna indoors.

## Assembly order

1. Wire PSU → LM2596, power on with nothing else attached, adjust to 4.0V output.
2. Power off; connect capacitor and SIM800L to LM2596 output (watch capacitor polarity).
3. Join LM2596 OUT− to Arduino GND.
4. Add the 1kΩ/2kΩ divider and the D7/D8 serial wires.
5. Wire DHT11, LDR, rain sensor and OLED to the Arduino.
6. Insert SIM (power off), attach antenna, power up, wait for slow LED blink.

## Firmware plan (to be written)

- Libraries: DHT (Adafruit), Adafruit_SSD1306 + Adafruit_GFX (or U8g2 page mode if RAM is tight), SoftwareSerial.
- Read DHT11 (temperature, humidity), LDR (light %), rain sensor (dry / light rain / heavy rain from A1; power D4 HIGH only during the reading).
- Show readings on the OLED, refreshed every few seconds.
- Listen for incoming SMS (`AT+CMGF=1`, `AT+CNMI=2,2,0,0,0`). If the sender is in an authorized-numbers list and the text is "WEATHER", reply with a one-SMS weather report (`AT+CMGS`).
- Optionally send a scheduled report (e.g. every hour) to the authorized numbers.
- Keep strings in `F()` macros; the Uno has 2KB RAM and the SSD1306 buffer uses ~1KB.
- Budget noted in the notebook: Arduino 3,800; sensors 400; GSM 1,200; OLED 1,200; total ~6,600 (KES).
- Also print one JSON line per reading over hardware Serial (USB, 9600 baud) for the dashboard, e.g.
  `{"t":24.5,"h":61,"light":72,"rain":"dry","rain_raw":987}`.

## Local dashboard (to be written)

Goal: a locally running web app that combines live sensor readings with online weather data.

- Stack: Python 3, Flask, pyserial, requests. Single `index.html` with vanilla JS polling `/api/data` every few seconds (Chart.js for a simple history chart).
- Serial: read JSON lines from the Arduino's USB port (e.g. `/dev/ttyUSB0`, `/dev/ttyACM0` or `COM3`), 9600 baud. Do not open the Arduino serial monitor at the same time.
- Online data: Open-Meteo (free, no API key). Fetch current weather + hourly forecast for Nairobi (approx. lat -1.29, lon 36.82; make it configurable) every 10–15 minutes and cache it.
- Page layout: "Now at this location" (sensor readings) | "Online conditions and forecast" (Open-Meteo) | comparison (sensor vs online temperature/humidity) | recent readings chart.
- Optional: log readings to CSV or SQLite for history; show last SMS sent.
- Run with `python app.py`, open http://localhost:5000.

## Agreed extra features

Firmware (SMS):
- Trend in the report: compare current readings with those from ~1 hour ago and add a short line, e.g. "Humidity rising", "Light falling".
- Automatic alerts: text the authorized numbers when rain starts (rain sensor goes wet), when temperature crosses a configurable threshold, or when light drops sharply (storm building). Rate-limit alerts (no repeats within e.g. 30 min).
- Keep the Arduino's own short history (last ~12 readings at 5-min intervals) in RAM for trend/alert logic; the full history lives in the dashboard log.

Dashboard:
- Data logging: store every sensor reading and every Open-Meteo fetch in SQLite (`readings` and `forecasts` tables) with timestamps.
- Forecast vs sensor comparison: show online current temperature/humidity next to the sensor values, with the difference.
- Forecast accuracy check: for each past hour, compare what Open-Meteo forecast (fetched earlier) with what the sensors measured; show mean absolute error for temperature and humidity, and whether forecast rain matched the rain sensor. This is an evaluation metric for the project report.
- Rain prediction (rule-based data fusion): combine humidity level and trend, light drop rate, rain sensor state and Open-Meteo rain probability into a "rain likely / possible / unlikely" indicator. If a BMP280 is added later (I2C, shares A4/A5), include pressure trend (drop of ~2–3 hPa over 3 h → rain likely) as a fourth input.
- SMS log: who sent what command and when, and which alerts were sent.
