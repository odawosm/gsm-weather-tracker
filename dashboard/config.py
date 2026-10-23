"""Dashboard settings. Each can be overridden with an environment variable of the same name."""
import json
import os

SERIAL_PORT = os.environ.get("SERIAL_PORT")          # None = auto-detect (/dev/ttyACM*, /dev/ttyUSB*, COM*)
BAUD = int(os.environ.get("BAUD", 9600))
FAKE = os.environ.get("FAKE") == "1"                  # 1 = use fake_serial.py instead of the Arduino

LAT = float(os.environ.get("LAT", -1.29))             # default Nairobi
LON = float(os.environ.get("LON", 36.82))
FETCH_INTERVAL_S = int(os.environ.get("FETCH_INTERVAL_S", 15 * 60))

DB_PATH = os.environ.get("DB_PATH", os.path.join(os.path.dirname(__file__), "data.db"))
PORT = int(os.environ.get("PORT", 5000))

# Forecast accuracy uses the latest forecast fetched at least this many hours before the target hour.
FORECAST_LEAD_HOURS = 1

# ---- Backup SMS through an Android phone ("SMS Gateway for Android", Local Server mode) ----
# Leave GATEWAY_URL empty to switch the backup off. The app shows the address, username and password.
GATEWAY_URL = os.environ.get("GATEWAY_URL", "")           # e.g. "http://192.168.1.50:8080"
GATEWAY_USER = os.environ.get("GATEWAY_USER", "sms")
GATEWAY_PASS = os.environ.get("GATEWAY_PASS", "")
GATEWAY_POLL_S = 15                                       # how often to ask the phone for new texts
GATEWAY_ALERTS = os.environ.get("GATEWAY_ALERTS", "1") == "1"  # 0 if the SIM800L is already sending alerts
# Numbers allowed to use WEATHER / STATUS / HELP and receive alerts (any Kenyan format), comma-separated in env.
AUTHORIZED = [n for n in os.environ.get("AUTHORIZED", "").split(",") if n.strip()]

# Phone details saved from the dashboard's "SMS phone settings" card. They replace the four values above.
GATEWAY_FILE = os.environ.get("GATEWAY_FILE", os.path.join(os.path.dirname(__file__), "gateway.json"))
if os.path.exists(GATEWAY_FILE):
    with open(GATEWAY_FILE) as f:
        _saved = json.load(f)
    GATEWAY_URL, GATEWAY_USER = _saved["url"], _saved["user"]
    GATEWAY_PASS, AUTHORIZED = _saved["password"], _saved["authorized"]

# Stop the dashboard (and everything run.sh / run.bat started) when its browser tab is closed.
# Unset = on, except while the phone backup is on, so texts keep being answered with no page open.
AUTO_EXIT = {"1": True, "0": False}.get(os.environ.get("AUTO_EXIT"))

# Same limits as firmware/weather_station/config.h
TEMP_HIGH_C = 30
TEMP_LOW_C = 10
LIGHT_DROP_PCT = 40          # % points within 20 min
ALERT_COOLDOWN_MIN = 30
TREND_TEMP_C = 1.0
TREND_HUM = 5
TREND_LIGHT = 15
