"""Self-check: python test_dashboard.py  (uses a temporary database, no network, no Arduino)."""
import json
import os
import tempfile
from datetime import datetime, timedelta

tmp = tempfile.mkdtemp()
os.environ["DB_PATH"] = os.path.join(tmp, "test.db")
os.environ["GATEWAY_FILE"] = os.path.join(tmp, "gateway.json")  # never touch the real saved phone settings
import app  # noqa: E402  (DB_PATH must be set first)

app.init_db()

# Rain rules
assert app.predict_rain()["level"] == "unlikely"
assert app.predict_rain(h=90, h_change_1h=6, online_rain_prob=70)["level"] == "likely"  # 2 + 1 + 2
assert app.predict_rain(h=78, online_rain_prob=40)["level"] == "possible"               # 1 + 1
assert app.predict_rain(sensor_rain="heavy rain")["level"] == "likely"

# Serial lines: readings, SMS events, and junk
app.store_line("# weather station starting")
app.store_line("not json {")
app.store_line('{"sms":"in","num":"+254700000001","text":"WEATHER"}')

# Forecast accuracy: one past hour H with two readings (avg t=21, h=61, dry)
H = (datetime.now() - timedelta(hours=2)).replace(minute=0, second=0, microsecond=0)
iso = lambda dt: dt.isoformat(timespec="seconds")
app.store_line('{"t":20,"h":60,"light":50,"rain":"dry","rain_raw":1000,"csq":18}', ts=iso(H + timedelta(minutes=10)))
app.store_line('{"t":22,"h":62,"light":50,"rain":"dry","rain_raw":1000,"csq":18}', ts=iso(H + timedelta(minutes=20)))
target = H.strftime("%Y-%m-%dT%H:00")
with app.db() as c:
    c.executemany("INSERT INTO forecasts VALUES (?,?,?,?,?,?)", [
        (iso(H - timedelta(hours=3)), target, 30, 90, 90, 5),      # older eligible forecast: superseded
        (iso(H - timedelta(minutes=90)), target, 24, 55, 10, 0),   # latest fetched >= 1 h before: the one used
        (iso(H - timedelta(minutes=30)), target, 99, 99, 99, 9),   # too close to the target hour: ignored
    ])
acc = app.accuracy(1)
assert acc == {"hours": 1, "temp_mae": 3.0, "hum_mae": 6.0, "rain_match_pct": 100}, acc

# Forecast line for the SMS report: max rain chance over the next 3 hours, only from a recent fetch
this_hour = datetime.now().replace(minute=0, second=0, microsecond=0)
app.online.update(fetched_at=iso(datetime.now()), hourly=[
    {"target_time": (this_hour + timedelta(hours=i)).strftime("%Y-%m-%dT%H:00"), "rain_prob": p}
    for i, p in enumerate([10, 40, 20, 90])])  # the 90 % is 3 h out: not included
line = app.forecast_line()
assert line == "FC:Fcst: rain 40% next 3h.\n" and len(line) - 4 <= 24, line  # firmware keeps 24 chars
app.online["fetched_at"] = iso(datetime.now() - timedelta(hours=3))
assert app.forecast_line() is None  # stale forecast is not sent
app.online.update(fetched_at=None, hourly=[])

# ---- Backup SMS gateway, against a fake phone (same endpoints as SMS Gateway for Android, Local Server mode) ----
import threading  # noqa: E402

from flask import Flask, jsonify, request  # noqa: E402
from werkzeug.serving import make_server  # noqa: E402

import sms_gateway  # noqa: E402

phone = Flask("fake_phone")
phone_inbox, phone_sent = [], []


@phone.post("/message")
def _phone_send():
    assert request.authorization.username == "sms" and request.authorization.password == "secret"
    body = request.get_json()
    phone_sent.append((body["phoneNumbers"][0], body["textMessage"]["text"]))
    return jsonify(id="x", state="Pending"), 202


@phone.get("/inbox")
def _phone_inbox():
    assert request.args["type"] == "SMS" and request.args["from"].endswith("Z")
    return jsonify(phone_inbox)


server = make_server("127.0.0.1", 0, phone)
threading.Thread(target=server.serve_forever, daemon=True).start()
app.config.GATEWAY_URL = f"http://127.0.0.1:{server.server_port}"
app.config.GATEWAY_USER, app.config.GATEWAY_PASS = "sms", "secret"
app.config.AUTHORIZED = ["0700 000 001"]

assert sms_gateway.normalize("0712 345 678") == sms_gateway.normalize("254712345678") == "+254712345678"

phone_inbox[:] = [
    {"id": "a", "sender": "+254700000001", "contentPreview": " weather "},  # authorized, any case/spacing
    {"id": "b", "sender": "+254711111111", "contentPreview": "WEATHER"},    # not authorized: ignored
    {"id": "c", "sender": "254700000001", "contentPreview": "HELP"},
]
seen = set()
app.process_inbox(sms_gateway.fetch_inbox(datetime.now().astimezone()), seen)
app.process_inbox(sms_gateway.fetch_inbox(datetime.now().astimezone()), seen)  # same inbox again: no repeats
assert [n for n, _ in phone_sent] == ["+254700000001", "+254700000001"], phone_sent
assert phone_sent[0][1].startswith("Weather ") and "station offline" in phone_sent[0][1], phone_sent[0]
assert phone_sent[1][1] == app.HELP_TEXT and all(len(t) <= 160 for _, t in phone_sent)

# Alerts: dry -> wet sends one "Rain started"; staying wet doesn't repeat; cooldown blocks a quick repeat.
phone_sent.clear()
now = datetime.now()
app.store_line('{"t":22,"h":70,"light":50,"rain":"dry","rain_raw":1000,"csq":18}', ts=iso(now))
app.check_alerts()  # first reading: remembered, no alert
app.store_line('{"t":22,"h":80,"light":50,"rain":"heavy rain","rain_raw":300,"csq":18}', ts=iso(now + timedelta(seconds=5)))
app.check_alerts()
app.store_line('{"t":22,"h":80,"light":50,"rain":"heavy rain","rain_raw":300,"csq":18}', ts=iso(now + timedelta(seconds=10)))
app.check_alerts()
assert len(phone_sent) == 1 and "Rain started" in phone_sent[0][1], phone_sent
server.shutdown()

# Routes
client = app.app.test_client()
data = client.get("/api/data").get_json()
assert data["sensor"]["rain"] == "heavy rain" and any(m["text"] == "WEATHER" for m in data["sms"]), data
assert data["gateway"]["enabled"] is False  # GATEWAY_URL was empty at startup
assert len(client.get("/api/history?hours=24").get_json()) >= 3
assert client.get("/api/accuracy").get_json()["24h"]["hours"] == 1
assert client.get("/").status_code == 200

# Phone settings from the dashboard: saved, password never sent back, blank password keeps the old one
form = {"url": "192.168.1.50:8080/", "user": "sms", "password": "pw1", "authorized": "0700 000001"}
assert client.post("/api/gateway", json=form).status_code == 400  # space inside a number splits it
form["authorized"] = "0700000001, 254700000002"
assert client.post("/api/gateway", json=form, headers={"Host": "evil.example"}).status_code == 403
assert client.post("/api/gateway", data=form).status_code == 403  # not JSON: a plain cross-site form post
g = client.post("/api/gateway", json=form).get_json()
assert g == {"url": "http://192.168.1.50:8080", "user": "sms", "has_password": True,
             "authorized": ["+254700000001", "+254700000002"]}, g
client.post("/api/gateway", json={**form, "password": ""})
assert app.config.GATEWAY_PASS == "pw1" and app.gateway_status["enabled"] and not app.auto_exit()
assert json.load(open(os.environ["GATEWAY_FILE"]))["password"] == "pw1"
client.post("/api/gateway", json={"url": "", "authorized": ""})  # backup off
assert not app.gateway_status["enabled"] and app.auto_exit()

print("All dashboard checks passed.")
