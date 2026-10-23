"""Backup SMS through an Android phone running "SMS Gateway for Android" (https://sms-gate.app), Local Server mode.

Used when the SIM800L can't be: the laptop asks the phone for new texts (GET /inbox) and replies through it
(POST /message). Phone and laptop must be on the same Wi-Fi; plain HTTP on the local network is enough because
the laptop always starts the connection (no webhooks, so no HTTPS certificate and nothing exposed on the laptop).
"""
from datetime import timezone

import requests

import config


def _url(path):
    return config.GATEWAY_URL.rstrip("/") + path


def _auth():
    return (config.GATEWAY_USER, config.GATEWAY_PASS)


def send_sms(number, text):
    r = requests.post(_url("/message"), auth=_auth(), timeout=15,
                      json={"textMessage": {"text": text[:160]}, "phoneNumbers": [number]})
    r.raise_for_status()


def fetch_inbox(since):
    """Texts received since `since` (timezone-aware datetime): list of {id, sender, contentPreview, createdAt, ...}."""
    r = requests.get(_url("/inbox"), auth=_auth(), timeout=15, params={
        "type": "SMS", "limit": 100, "from": since.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")})
    r.raise_for_status()
    return r.json()


def normalize(number):
    """Kenyan numbers to one format: '0712 345 678', '254712345678', '+254712345678' -> '+254712345678'."""
    n = "".join(ch for ch in (number or "") if ch.isdigit() or ch == "+")
    if n.startswith("0") and len(n) == 10:
        return "+254" + n[1:]
    if n.startswith("254"):
        return "+" + n
    return n
