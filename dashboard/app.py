"""SMS weather station dashboard: Arduino readings + Open-Meteo forecast, logged to SQLite.

Run:  python app.py          (FAKE=1 python app.py to run without the Arduino)
Open: http://localhost:5000
"""
import json
import os
import re
import sqlite3
import threading
import time
import webbrowser
from contextlib import contextmanager
from datetime import datetime, timedelta
from urllib.parse import urlparse

import requests
import serial
from flask import Flask, jsonify, render_template, request
from serial.tools import list_ports

import config
import sms_gateway

app = Flask(__name__)
online = {"current": None, "hourly": [], "fetched_at": None, "error": None}  # latest Open-Meteo result
serial_status = {"port": None, "connected": False}
FORECAST_SEND_S = 300  # how often the forecast line goes to the Arduino for its SMS report
gateway_status = {"enabled": bool(config.GATEWAY_URL), "ok": None, "error": None}
gateway_wake = threading.Event()  # set when the phone settings change, so the new phone is tried at once
alert_state = {"prev": None, "last": {}}  # backup alerts: previous reading and last send time per alert type

# ponytail: all times are the laptop's local time and Open-Meteo is asked for the location's local time
# (timezone=auto); they match as long as the laptop runs in the station's time zone.


def now_iso(dt=None):
    return (dt or datetime.now()).isoformat(timespec="seconds")


@contextmanager
def db():
    conn = sqlite3.connect(config.DB_PATH, timeout=10)
    conn.row_factory = sqlite3.Row
    try:
        yield conn
        conn.commit()
    finally:
        conn.close()


def init_db():
    with db() as c:
        c.executescript("""
            CREATE TABLE IF NOT EXISTS readings (ts TEXT, t REAL, h REAL, light INTEGER, rain TEXT, rain_raw INTEGER, csq INTEGER);
            CREATE INDEX IF NOT EXISTS readings_ts ON readings(ts);
            CREATE TABLE IF NOT EXISTS forecasts (fetched_at TEXT, target_time TEXT, temp REAL, humidity REAL,
                                                  rain_prob REAL, precipitation REAL);
            CREATE INDEX IF NOT EXISTS forecasts_target ON forecasts(target_time);
            CREATE TABLE IF NOT EXISTS sms_log (ts TEXT, direction TEXT, number TEXT, text TEXT);
        """)


def store_line(line, ts=None):
    """Store one line from the Arduino. Non-JSON lines ('# ...' comments, boot noise) are ignored."""
    line = line.strip()
    if line.startswith("#"):  # firmware status messages, e.g. "# GSM no reply, will retry"
        print(f"Arduino {line}")
    if not line.startswith("{"):
        return
    try:
        d = json.loads(line)
    except ValueError:
        return
    ts = ts or now_iso()
    with db() as c:
        if "sms" in d:
            c.execute("INSERT INTO sms_log VALUES (?,?,?,?)", (ts, d["sms"], d.get("num"), d.get("text")))
        elif "light" in d:
            c.execute("INSERT INTO readings VALUES (?,?,?,?,?,?,?)",
                      (ts, d.get("t"), d.get("h"), d.get("light"), d.get("rain"), d.get("rain_raw"), d.get("csq")))


# ---------------- Background threads ----------------

def find_port():
    if config.SERIAL_PORT:
        return config.SERIAL_PORT
    for p in list_ports.comports():
        if ("Arduino" in (p.description or "") or p.vid in (0x2341, 0x2A03, 0x1A86)  # Arduino, clones, CH340
                or p.device.startswith(("/dev/ttyACM", "/dev/ttyUSB"))):
            return p.device
    return None


def serial_loop():
    """Read the Arduino forever; keeps running while it's unplugged and reconnects when it's back."""
    while True:
        port = find_port()
        if port:
            try:
                with serial.Serial(port, config.BAUD, timeout=2) as ser:
                    serial_status.update(port=port, connected=True)
                    print(f"Reading Arduino on {port}")
                    next_send = time.time() + 10  # opening the port reboots the Uno: let it start first
                    while True:
                        if time.time() >= next_send:
                            fc = forecast_line()
                            if fc:
                                ser.write(fc.encode())
                            next_send = time.time() + FORECAST_SEND_S
                        raw = ser.readline()
                        if raw:
                            store_line(raw.decode(errors="replace"))
            except (serial.SerialException, OSError) as e:
                print(f"Serial {port}: {e} (retrying in 5 s)")
        serial_status["connected"] = False
        time.sleep(5)


def rain_prob_3h():
    """Highest Open-Meteo rain chance over the next 3 hours, or None if there's no recent forecast."""
    if not online["fetched_at"] or online["fetched_at"] < now_iso(datetime.now() - timedelta(hours=2)):
        return None
    this_hour = datetime.now().strftime("%Y-%m-%dT%H:00")
    probs = [f["rain_prob"] for f in online["hourly"] if f["target_time"] >= this_hour][:3]
    probs = [p for p in probs if p is not None]
    return max(probs) if probs else None


def forecast_line():
    """Forecast for the station's SMS report, sent over USB. The firmware keeps up to 24 characters after 'FC:'."""
    p = rain_prob_3h()
    return None if p is None else f"FC:Fcst: rain {p:.0f}% next 3h.\n"


def fake_loop():
    import fake_serial
    serial_status.update(port="fake_serial.py", connected=True)
    for line in fake_serial.lines():
        store_line(line)


def fetch_online():
    r = requests.get("https://api.open-meteo.com/v1/forecast", params={
        "latitude": config.LAT, "longitude": config.LON, "timezone": "auto", "forecast_days": 2,
        "current": "temperature_2m,relative_humidity_2m,precipitation",
        "hourly": "temperature_2m,relative_humidity_2m,precipitation_probability,precipitation",
    }, timeout=20)
    r.raise_for_status()
    j = r.json()
    hr = j["hourly"]
    this_hour = datetime.now().strftime("%Y-%m-%dT%H:00")
    rows = [dict(target_time=t, temp=a, humidity=b, rain_prob=p, precipitation=mm)
            for t, a, b, p, mm in zip(hr["time"], hr["temperature_2m"], hr["relative_humidity_2m"],
                                       hr["precipitation_probability"], hr["precipitation"])
            if t >= this_hour]
    fetched = now_iso()
    with db() as c:
        c.executemany("INSERT INTO forecasts VALUES (?,?,?,?,?,?)",
                      [(fetched, f["target_time"], f["temp"], f["humidity"], f["rain_prob"], f["precipitation"])
                       for f in rows])
    online.update(current=j["current"], hourly=rows[:12], fetched_at=fetched, error=None)


def online_loop():
    while True:
        try:
            fetch_online()
            time.sleep(config.FETCH_INTERVAL_S)
        except Exception as e:  # network down etc.: keep the last good data, retry in a minute
            online["error"] = str(e)
            print(f"Open-Meteo: {e}")
            time.sleep(60)


# ---------------- Backup SMS through an Android phone ----------------
# Same commands, wording and alerts as the firmware, built from the readings the Arduino logs over USB.

HELP_TEXT = "Commands: WEATHER = current report, STATUS = station status, HELP = this list."


def latest_reading():
    with db() as c:
        return c.execute("SELECT * FROM readings ORDER BY ts DESC LIMIT 1").fetchone()


def readings_text(r):
    """'Temp 24.5C, Hum 61%, Light 72%, Rain: dry.' (+ a note if the station has gone quiet)."""
    th = f"Temp {r['t']:.1f}C, Hum {r['h']:.0f}%, " if r["t"] is not None and r["h"] is not None else "Temp/Hum n/a, "
    text = f"{th}Light {r['light']}%, Rain: {r['rain']}."
    if r["ts"] < now_iso(datetime.now() - timedelta(minutes=10)):
        text += f" (station offline since {r['ts'][11:16]})"
    return text


def station_report():
    r = latest_reading()
    if not r:
        return "Weather: no station readings yet."
    with db() as c:  # trend vs about an hour earlier, like the firmware's ring buffer
        old = c.execute("SELECT * FROM readings WHERE ts BETWEEN ? AND ? ORDER BY ts DESC LIMIT 1",
                        (now_iso(datetime.fromisoformat(r["ts"]) - timedelta(minutes=75)),
                         now_iso(datetime.fromisoformat(r["ts"]) - timedelta(minutes=55)))).fetchone()
    msg = f"Weather {datetime.fromisoformat(r['ts']).strftime('%d/%m %H:%M')}: {readings_text(r)}"
    if old and None not in (r["t"], r["h"], old["t"], old["h"]):
        changes = [f"{name} {'rising' if d > 0 else 'falling'}" for name, d, limit in (
            ("temp", r["t"] - old["t"], config.TREND_TEMP_C),
            ("humidity", r["h"] - old["h"], config.TREND_HUM),
            ("light", r["light"] - old["light"], config.TREND_LIGHT)) if abs(d) >= limit]
        msg += f" Trend: {', '.join(changes) if changes else 'steady'}."
    fc = forecast_line()
    if fc:
        msg += " " + fc[3:].strip()
    return msg[:160]


def station_status():
    r = latest_reading()
    last = r["ts"].replace("T", " ")[5:16] if r else "never"
    station = "online" if r and r["ts"] >= now_iso(datetime.now() - timedelta(minutes=10)) else "offline"
    return f"Status (phone backup): station {station}, last reading {last}, Arduino USB " \
           f"{'connected' if serial_status['connected'] else 'not connected'}."


def log_sms(direction, number, text):
    with db() as c:
        c.execute("INSERT INTO sms_log VALUES (?,?,?,?)", (now_iso(), direction, number, text))


def send_logged(number, text):
    try:
        sms_gateway.send_sms(number, text)
        log_sms("out (phone)", number, text)
    except requests.RequestException as e:
        log_sms("fail (phone)", number, f"{text} [{e}]")


def process_inbox(messages, seen):
    """Answer new texts from authorized numbers; log everything. `seen` holds message ids already handled."""
    allowed = {sms_gateway.normalize(n) for n in config.AUTHORIZED}
    for m in messages:
        if m.get("id") in seen:
            continue
        seen.add(m.get("id"))
        sender = sms_gateway.normalize(m.get("sender"))
        text = (m.get("contentPreview") or "").strip()
        if sender not in allowed:
            log_sms("ignored (phone)", sender, text)
            continue
        log_sms("in (phone)", sender, text)
        cmd = text.upper()
        send_logged(sender, station_report() if cmd == "WEATHER" else station_status() if cmd == "STATUS" else HELP_TEXT)


def send_alert(kind, what, r):
    last = alert_state["last"].get(kind)
    if last and datetime.now() - last < timedelta(minutes=config.ALERT_COOLDOWN_MIN):
        return
    alert_state["last"][kind] = datetime.now()
    text = f"ALERT {datetime.fromisoformat(r['ts']).strftime('%d/%m %H:%M')}: {what}. {readings_text(r)}"[:160]
    for n in config.AUTHORIZED:
        send_logged(sms_gateway.normalize(n), text)


def check_alerts():
    """Rain starts, temperature crosses a limit, light drops sharply: same rules as the firmware."""
    r = latest_reading()
    prev = alert_state["prev"]
    if not r or (prev and prev["ts"] == r["ts"]):
        return
    alert_state["prev"] = dict(r)
    if not prev:
        return

    def wet(x):
        return x["rain"] in ("light rain", "heavy rain")

    def zone(x):
        return 0 if x["t"] is None else 1 if x["t"] > config.TEMP_HIGH_C else -1 if x["t"] < config.TEMP_LOW_C else 0

    if wet(r) and not wet(prev):
        send_alert("rain", "Rain started", r)
    if zone(r) and zone(r) != zone(prev):
        send_alert("temp", "High temperature" if zone(r) > 0 else "Low temperature", r)
    with db() as c:
        brightest = c.execute("SELECT max(light) FROM readings WHERE ts >= ?",
                              (now_iso(datetime.fromisoformat(r["ts"]) - timedelta(minutes=20)),)).fetchone()[0]
    if brightest is not None and brightest - r["light"] > config.LIGHT_DROP_PCT:
        send_alert("light", "Light dropped sharply (storm?)", r)


def gateway_loop():
    since = datetime.now().astimezone()  # ponytail: texts that arrived while the dashboard was off are not answered
    seen = set()
    url = None
    while True:
        if config.GATEWAY_URL != url:  # first run, or changed from the dashboard
            url = config.GATEWAY_URL
            print(f"SMS backup: using the phone gateway at {url}" if url else "SMS backup: off")
        if url:
            try:
                process_inbox(sms_gateway.fetch_inbox(since), seen)
                if config.GATEWAY_ALERTS:
                    check_alerts()
                gateway_status.update(ok=True, error=None)
            except Exception as e:  # a wrong address can answer anything; keep trying instead of stopping the loop
                if gateway_status["ok"] is not False:
                    print(f"SMS backup: phone not reachable ({e})")
                gateway_status.update(ok=False, error=str(e))
        gateway_wake.wait(config.GATEWAY_POLL_S)
        gateway_wake.clear()


# ---------------- Rain prediction ----------------

def predict_rain(h=None, h_change_1h=None, light_drop_30m=None, sensor_rain=None,
                 online_rain_prob=None, pressure_drop_3h=None):
    """Rule-based rain score from whatever inputs are available (None = missing, skipped).
    To add an input (e.g. BMP280 pressure), add a parameter and a rule below."""
    score, reasons = 0, []

    def rule(points, reason):
        nonlocal score
        score += points
        reasons.append(reason)

    if sensor_rain in ("light rain", "heavy rain"):
        rule(4, f"rain sensor reports {sensor_rain} now")
    elif sensor_rain == "damp":
        rule(1, "rain sensor is damp")
    if h is not None:
        if h >= 85:
            rule(2, f"humidity high ({h:.0f}%)")
        elif h >= 75:
            rule(1, f"humidity elevated ({h:.0f}%)")
    if h_change_1h is not None and h_change_1h >= 5:
        rule(1, f"humidity rising (+{h_change_1h:.0f}% in 1 h)")
    if light_drop_30m is not None and light_drop_30m >= 30:
        rule(1, f"light dropped {light_drop_30m:.0f} points in 30 min")
    if online_rain_prob is not None:
        if online_rain_prob >= 60:
            rule(2, f"forecast rain chance {online_rain_prob:.0f}% in the next 3 h")
        elif online_rain_prob >= 30:
            rule(1, f"forecast rain chance {online_rain_prob:.0f}% in the next 3 h")
    if pressure_drop_3h is not None and pressure_drop_3h >= 2:  # BMP280: ~2-3 hPa fall over 3 h
        rule(2, f"pressure fell {pressure_drop_3h:.1f} hPa in 3 h")

    level = "likely" if score >= 4 else "possible" if score >= 2 else "unlikely"
    return {"level": level, "score": score, "reasons": reasons}


# ---------------- Forecast accuracy ----------------

def accuracy(days):
    """Per past hour: sensor hourly average vs the latest forecast fetched >= FORECAST_LEAD_HOURS before it."""
    now = datetime.now()
    since = now_iso(now - timedelta(days=days))
    this_hour = now.strftime("%Y-%m-%dT%H:00")
    with db() as c:
        hours = c.execute("""
            SELECT substr(ts, 1, 13) || ':00' AS hr, avg(t) AS t, avg(h) AS h,
                   max(rain IN ('light rain', 'heavy rain')) AS wet
            FROM readings WHERE ts >= ? AND t IS NOT NULL GROUP BY hr""", (since,)).fetchall()
        forecasts = c.execute("SELECT * FROM forecasts WHERE target_time >= ? ORDER BY fetched_at",
                              (since[:13] + ":00",)).fetchall()

    best = {}
    for f in forecasts:  # ordered by fetched_at, so the latest eligible forecast wins
        cutoff = now_iso(datetime.fromisoformat(f["target_time"]) - timedelta(hours=config.FORECAST_LEAD_HOURS))
        if f["fetched_at"] <= cutoff:
            best[f["target_time"]] = f

    t_err, h_err, matches = [], [], []
    for row in hours:
        f = best.get(row["hr"])
        if not f or row["hr"] >= this_hour:  # skip hours without a forecast and the unfinished current hour
            continue
        if f["temp"] is not None:
            t_err.append(abs(row["t"] - f["temp"]))
        if f["humidity"] is not None and row["h"] is not None:
            h_err.append(abs(row["h"] - f["humidity"]))
        forecast_wet = (f["precipitation"] or 0) >= 0.1 or (f["rain_prob"] or 0) >= 50
        matches.append(forecast_wet == bool(row["wet"]))

    def mean(xs):
        return round(sum(xs) / len(xs), 2) if xs else None

    return {"hours": len(matches), "temp_mae": mean(t_err), "hum_mae": mean(h_err),
            "rain_match_pct": round(100 * sum(matches) / len(matches)) if matches else None}


# ---------------- Routes ----------------

@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/data")
def api_data():
    now = datetime.now()
    with db() as c:
        latest = c.execute("SELECT * FROM readings ORDER BY ts DESC LIMIT 1").fetchone()
        hour_ago = c.execute("SELECT h FROM readings WHERE ts BETWEEN ? AND ? AND h IS NOT NULL ORDER BY ts DESC LIMIT 1",
                             (now_iso(now - timedelta(minutes=75)), now_iso(now - timedelta(minutes=60)))).fetchone()
        max_light = c.execute("SELECT max(light) FROM readings WHERE ts >= ?",
                              (now_iso(now - timedelta(minutes=30)),)).fetchone()[0]
        sms = [dict(r) for r in c.execute("SELECT * FROM sms_log ORDER BY ts DESC LIMIT 20")]
    sensor = dict(latest) if latest else None

    cur = online["current"] or {}
    comparison = None
    if sensor and cur:
        def diff(a, b):
            return round(a - b, 1) if a is not None and b is not None else None
        comparison = {
            "t_sensor": sensor["t"], "t_online": cur.get("temperature_2m"),
            "t_diff": diff(sensor["t"], cur.get("temperature_2m")),
            "h_sensor": sensor["h"], "h_online": cur.get("relative_humidity_2m"),
            "h_diff": diff(sensor["h"], cur.get("relative_humidity_2m")),
        }

    rain = predict_rain(
        h=sensor["h"] if sensor else None,
        h_change_1h=(sensor["h"] - hour_ago["h"]) if sensor and sensor["h"] is not None and hour_ago else None,
        light_drop_30m=(max_light - sensor["light"]) if sensor and max_light is not None else None,
        sensor_rain=sensor["rain"] if sensor else None,
        online_rain_prob=rain_prob_3h(),
    )
    return jsonify(sensor=sensor, serial=serial_status, online=online, comparison=comparison, rain=rain, sms=sms,
                   gateway=gateway_status)


@app.get("/api/gateway")
def api_gateway():
    # the password itself never goes back to the page
    return jsonify(url=config.GATEWAY_URL, user=config.GATEWAY_USER, has_password=bool(config.GATEWAY_PASS),
                   authorized=config.AUTHORIZED)


@app.post("/api/gateway")
def api_gateway_save():
    # Only from this laptop's own page, or any website could point the backup (and its password) at its own server:
    # a cross-site JSON POST needs a CORS preflight Flask never approves, and the Host check stops DNS rebinding.
    if request.host.split(":")[0] not in ("localhost", "127.0.0.1") or not request.is_json:
        return jsonify(error="Not allowed"), 403
    d = request.get_json()
    url = str(d.get("url") or "").strip().rstrip("/")
    if url and "://" not in url:
        url = "http://" + url
    if url and not urlparse(url).hostname:
        return jsonify(error="The address should look like 192.168.1.50:8080"), 400
    numbers = [n for n in re.split(r"[\s,;]+", str(d.get("authorized") or "")) if n]
    bad = [n for n in numbers if not re.fullmatch(r"\+\d{10,15}", sms_gateway.normalize(n))]
    if bad:
        return jsonify(error=f"Not a phone number: {bad[0]} (separate numbers with commas, no spaces inside one)"), 400
    password = str(d.get("password") or "") or config.GATEWAY_PASS  # blank = keep the current one
    if url and not password:
        return jsonify(error="Enter the password shown in the SMS Gateway app"), 400
    saved = {"url": url, "user": str(d.get("user") or "").strip() or "sms", "password": password,
             "authorized": [sms_gateway.normalize(n) for n in numbers]}
    with open(config.GATEWAY_FILE, "w") as f:
        json.dump(saved, f, indent=2)
    os.chmod(config.GATEWAY_FILE, 0o600)  # holds the password: only this user may read it
    config.GATEWAY_URL, config.GATEWAY_USER = saved["url"], saved["user"]
    config.GATEWAY_PASS, config.AUTHORIZED = saved["password"], saved["authorized"]
    gateway_status.update(enabled=bool(url), ok=None, error=None)
    gateway_wake.set()
    return api_gateway()


@app.route("/api/history")
def api_history():
    hours = min(request.args.get("hours", 24, type=float), 24 * 7)
    bucket = 16 if hours <= 24 else 15  # per minute, or per 10 minutes for longer ranges
    with db() as c:
        rows = c.execute("""
            SELECT substr(ts, 1, ?) AS bucket, round(avg(t), 1) AS t, round(avg(h)) AS h, round(avg(light)) AS light
            FROM readings WHERE ts >= ? GROUP BY bucket ORDER BY bucket""",
            (bucket, now_iso(datetime.now() - timedelta(hours=hours)))).fetchall()
    return jsonify([dict(r) for r in rows])


@app.route("/api/accuracy")
def api_accuracy():
    return jsonify({"24h": accuracy(1), "7d": accuracy(7)})


# ---------------- Stop when the page is closed (config.AUTO_EXIT) ----------------
EXIT_AFTER_BYE_S = 10       # tab closed: stop shortly after, unless it was only a reload
EXIT_AFTER_SILENCE_S = 600  # no contact from any page at all (browser crashed etc.)
page_seen = {"last": None, "bye": None}


@app.before_request
def note_page_contact():
    if request.path != "/api/bye":  # any request from an open page cancels a pending stop
        page_seen.update(last=time.monotonic(), bye=None)


@app.post("/api/bye")
def api_bye():
    page_seen["bye"] = time.monotonic()
    return "", 204


def exit_watchdog():
    while True:
        time.sleep(2)
        now = time.monotonic()  # doesn't advance while the laptop sleeps, so waking up doesn't trigger a stop
        closed = page_seen["bye"] is not None and now - page_seen["bye"] > EXIT_AFTER_BYE_S
        silent = page_seen["last"] is not None and now - page_seen["last"] > EXIT_AFTER_SILENCE_S
        if (closed or silent) and auto_exit():
            print("Dashboard page closed: stopping.", flush=True)
            os._exit(0)  # every database write is already committed; the serial port closes with the process


def auto_exit():
    """config.AUTO_EXIT, or when unset: stop with the page only while the phone backup is off."""
    return config.AUTO_EXIT if config.AUTO_EXIT is not None else not config.GATEWAY_URL


def start_background():
    init_db()
    threading.Thread(target=fake_loop if config.FAKE else serial_loop, daemon=True).start()
    threading.Thread(target=online_loop, daemon=True).start()
    threading.Thread(target=gateway_loop, daemon=True).start()  # idles while no phone is set


if __name__ == "__main__":
    start_background()
    threading.Thread(target=exit_watchdog, daemon=True).start()  # checks auto_exit() itself: the phone can change
    url = f"http://localhost:{config.PORT}"
    if auto_exit():
        print(f"Dashboard: {url}  (close the page or press Ctrl+C to stop)")
    else:
        print(f"Dashboard: {url}  (keeps running when the page is closed; Ctrl+C to stop)")
    threading.Timer(1.5, webbrowser.open, [url]).start()  # after the server is up
    app.run(host="127.0.0.1", port=config.PORT, use_reloader=False)
