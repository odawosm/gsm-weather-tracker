"""Fake Arduino: realistic JSON lines in the same format as the firmware.

    python fake_serial.py                 print a line every 5 s (pipe it into a virtual serial port if you like)
    python fake_serial.py --backfill 24   write 24 h of past readings into data.db so the chart is full straight away
    FAKE=1 python app.py                  run the dashboard fed by this module instead of the serial port
"""
import json
import math
import random
import sys
import time
from datetime import datetime, timedelta

STEP_S = 5
_rain_until = None


def _rain_class(raw):  # same thresholds as config.h
    return "dry" if raw >= 900 else "damp" if raw >= 700 else "light rain" if raw >= 400 else "heavy rain"


def reading(at):
    """One reading for time `at`: daily temperature/humidity/light cycle, plus the odd 20-60 min shower."""
    global _rain_until
    if (_rain_until is None or at > _rain_until) and random.random() < 0.00025:  # ~ one shower per 6 h
        _rain_until = at + timedelta(minutes=random.randint(20, 60))
    raining = _rain_until is not None and at <= _rain_until

    hour = at.hour + at.minute / 60
    warm = math.sin((hour - 9) / 24 * 2 * math.pi)              # warmest mid-afternoon
    sun = max(0.0, math.sin((hour - 6.5) / 12 * math.pi))       # daylight ~06:30-18:30
    t = 19 + 6 * warm - (3 if raining else 0) + random.gauss(0, 0.2)
    h = 60 - 20 * warm + (25 if raining else 0) + random.gauss(0, 1)
    light = 85 * sun * (0.35 if raining else 1) + random.gauss(0, 1)
    raw = random.randint(300, 650) if raining else int(1010 + random.gauss(0, 4))
    return {
        "t": round(t, 1),
        "h": int(min(95, max(20, h))),
        "light": int(min(100, max(0, light))),
        "rain": _rain_class(raw),
        "rain_raw": min(1023, raw),
        "csq": random.randint(15, 20),
    }


def lines():
    """Endless JSON lines, one every STEP_S seconds, with an occasional SMS exchange."""
    while True:
        yield json.dumps(reading(datetime.now()))
        if random.random() < 1 / 720:  # ~ once an hour
            yield json.dumps({"sms": "in", "num": "+254700000001", "text": "WEATHER"})
            yield json.dumps({"sms": "out", "num": "+254700000001", "text": "Weather: (fake report)"})
        time.sleep(STEP_S)


def backfill(hours):
    from app import init_db, store_line
    init_db()
    start = datetime.now() - timedelta(hours=hours)
    n = int(hours * 60)  # one reading a minute is plenty for the chart
    for i in range(n):
        at = start + timedelta(minutes=i)
        store_line(json.dumps(reading(at)), ts=at.isoformat(timespec="seconds"))
    print(f"Inserted {n} readings covering the last {hours:g} h.")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--backfill":
        backfill(float(sys.argv[2]))
    else:
        for line in lines():
            print(line, flush=True)
