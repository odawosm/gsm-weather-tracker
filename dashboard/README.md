# Dashboard

Local web page that combines the station's live sensor readings with Open-Meteo weather, logs everything to SQLite (`data.db`) and works out a rain prediction and forecast accuracy.

## Run

Easiest: from the project folder run `./run.sh` (Linux/macOS) or double-click `run.bat` (Windows). Either one sets everything up and opens the page. On Windows, `run.bat` also installs Python if it's missing; see "Set up on Windows" in the [main README](../README.md#set-up-on-windows).

Internet: Open-Meteo is fetched over the **computer's** internet connection (Wi-Fi/Ethernet). Without it, the sensor sections keep working and the online sections show an error until the connection returns. The SIM800L is used only for SMS.

Manual setup:

```bash
cd dashboard
python3 -m venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\activate
pip install -r requirements.txt

python app.py                      # with the Arduino plugged in
```

Open <http://localhost:5000>.

**Close the Arduino IDE Serial Monitor first.** Only one program can use the serial port at a time.

**Stopping:** close the browser tab and the dashboard stops about 10 s later (a reload doesn't count), or press Ctrl+C. While the phone SMS backup is on, closing the tab doesn't stop it, so it can keep answering texts. This follows the SMS phone card straight away: switch the backup on or off there and the closing behaviour changes with it. Override with `AUTO_EXIT=1` (always stop) or `AUTO_EXIT=0` (never stop).

## Without hardware

```bash
python fake_serial.py --backfill 24   # optional: 24 h of fake history so the chart is full immediately
FAKE=1 python app.py                  # live fake readings every 5 s
```

Windows: `set FAKE=1` then `python app.py`.

## Settings

Edit `config.py`, or set environment variables:

| Variable | Default | Meaning |
|---|---|---|
| `SERIAL_PORT` | auto-detect | e.g. `/dev/ttyACM0` or `COM3` |
| `BAUD` | `9600` | must match the firmware |
| `LAT`, `LON` | `-1.29`, `36.82` (Nairobi) | location for Open-Meteo |
| `FETCH_INTERVAL_S` | `900` | Open-Meteo refresh |
| `FAKE` | off | `1` = use `fake_serial.py` |

The backup phone's address, login and approved numbers are set in the dashboard's SMS phone card (see below), not here.

The Arduino can be unplugged and replugged while the app runs; it reconnects by itself.

## What's on the page

On screens at least 1000 px wide the cards are laid out in rows: Now / Online / Comparison, then Rain prediction / SMS phone, then the chart across the full width, then Forecast accuracy / SMS log. Narrower screens fit as many columns as they can, down to one.

- **Now at this location**: latest sensor reading, temperature / humidity / light / rain in a 2×2 grid.
- **Online conditions and forecast**: Open-Meteo current weather and the next 12 hours.
- **Comparison**: sensor vs online temperature and humidity, with the difference.
- **Rain prediction**: unlikely / possible / likely, from humidity level and 1 h trend, light drop over 30 min, rain sensor state and the Open-Meteo rain chance for the next 3 h. The reasons are listed. A BMP280 pressure input is already supported in `predict_rain()` (`pressure_drop_3h`).
- **SMS phone**: whether the backup phone is connected (green), not reachable (red, hover for the reason), connecting… or off, plus the form for its details (see below).
- **Last 24 hours**: temperature, humidity and light chart. The temperature axis always covers at least `TEMP_MIN_SPAN` (6 °C, set in `static/app.js`), so the DHT11's ±0.2 °C flicker doesn't fill the whole height. It widens by itself when the temperature changes more than that.
- **Forecast accuracy**: for each finished hour, the sensor's hourly average vs the Open-Meteo forecast fetched at least 1 h earlier. Mean absolute error (MAE) for temperature and humidity, and how often forecast rain (≥ 0.1 mm or ≥ 50 % chance) matched the rain sensor. It fills in as forecasts accumulate, so allow a few hours.
- **SMS log**: commands received and replies/alerts sent, as reported by the firmware.

## Backup SMS through an Android phone

If the SIM800L isn't available, an Android phone can handle the SMS side instead. The laptop checks the phone for new texts every 15 s and replies through it. Commands (`WEATHER`, `STATUS`, `HELP`), wording and alerts are the same as the station's, built from the readings the Arduino sends over USB.

**Setup:**

1. Use a spare Android phone with a Safaricom SIM that has SMS credit. The gateway texts from the **phone's** number.
2. Install **SMS Gateway for Android** (<https://sms-gate.app>). In the app, switch on **Local Server** and start it. Note the local address (e.g. `192.168.1.50:8080`), username and password it shows.
3. Connect the phone and the laptop to the **same Wi-Fi**.
4. Start the dashboard (`./run.sh` or `run.bat`). In the **SMS phone** card (next to Rain prediction), enter:
   - the phone's address (e.g. `192.168.1.50:8080`)
   - the username and password
   - the approved numbers, separated by commas (e.g. `0712345678, 0723456789`)

   Then press **Save**. Within a few seconds the header shows **SMS phone: ok**.

   To change the details later, edit them and press Save again; leave the password blank to keep the current one. To switch the backup off, clear the address and save.

   The details are saved in `dashboard/gateway.json` (readable only by your user account), so they're kept after a restart. Don't share that file, because it contains the password. The environment variables `GATEWAY_URL`, `GATEWAY_USER`, `GATEWAY_PASS` and `AUTHORIZED` still work, but only until something is saved from the dashboard.
5. The top of the same card shows the **connection**: connected, not reachable (hover for the reason), connecting… or off.
6. From an approved phone, text `WEATHER` to the gateway phone. The reply arrives within about 20 s.

**Notes:**
- Keep the phone on charge, and exclude the app from battery saving so Android doesn't stop it.
- The Arduino must stay connected to the laptop, because the replies use its readings. If it's unplugged, replies say "station offline since …".
- Texts that arrive while the dashboard is off are not answered.
- If the SIM800L is also working, set `GATEWAY_ALERTS = "0"` (env `GATEWAY_ALERTS=0`) so alerts aren't sent twice.
- Replies and alerts appear in the SMS log marked "(phone)".

## Check

```bash
python test_dashboard.py
```
