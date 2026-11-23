from typing import Optional
from collections import deque
from contextlib import asynccontextmanager
from datetime import datetime
from html import escape
from zoneinfo import ZoneInfo
import hashlib
import hmac
import os
import secrets
import socket
import time

import httpx
from dotenv import load_dotenv
from fastapi import BackgroundTasks, FastAPI, Header, HTTPException
from fastapi.responses import HTMLResponse
from pydantic import BaseModel
from zeroconf import ServiceInfo
from zeroconf.asyncio import AsyncZeroconf

load_dotenv()                                   # reads the .env file next to this script
BOT_TOKEN = os.getenv("TELEGRAM_BOT_TOKEN", "")
CHAT_IDS = [c.strip() for c in os.getenv("TELEGRAM_CHAT_IDS", "").split(",") if c.strip()]

API_KEY = os.getenv("DEVICE_API_KEY", "")       # must match API_KEY in the firmware's secrets.h
if not API_KEY:
    print("WARNING: DEVICE_API_KEY is not set in .env, so every alert will be rejected")

SERVER_PORT = int(os.getenv("SERVER_PORT", "8000"))   # must match the --port given to uvicorn
ANNOUNCE_IP = os.getenv("ANNOUNCE_IP", "")            # optional: force the address to announce

DEVICE_NAMES = {"dev1": "Grandma"}   # device_id -> name used in the message

# type number -> (what the button says, icon). Must match the type numbers in the firmware's config.h.
ALERT_TYPES = {
    1: ("I need help",     "✋"),         # raised hand
    2: ("I need a break",  "⏸️"),   # pause
    3: ("I'm overwhelmed", "\U0001F635"),     # dizzy face
    4: ("I'm hungry",      "\U0001F34E"),     # apple
    5: ("I'm okay",        "\U0001F44D"),     # thumbs up
    6: ("I want to leave", "\U0001F6AA"),     # door
    7: ("Test button",     "\U0001F9EA"),     # test tube (BOOT button, testing only)
}
IST = ZoneInfo("Asia/Kolkata")

# ---------- Announcing this server on the local network (mDNS) ----------
def local_ip():
    """This computer's address on the local network (the one the ESP32 can reach)."""
    if ANNOUNCE_IP:
        return ANNOUNCE_IP
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))     # sends nothing; just picks the outgoing network
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()

@asynccontextmanager
async def lifespan(app):
    """Runs at start-up and shut-down: announce '_distress._tcp' so devices can find us."""
    ip = local_ip()
    host = socket.gethostname().split(".")[0]
    info = ServiceInfo(
        "_distress._tcp.local.",
        f"Distress server {host}._distress._tcp.local.",
        addresses=[socket.inet_aton(ip)],
        port=SERVER_PORT,
        properties={"path": "/api/alert"},
    )
    azc = AsyncZeroconf()
    announced = False
    try:
        await azc.async_register_service(info, allow_name_change=True)
        announced = True
        print(f"Announcing on the local network: _distress._tcp at {ip}:{SERVER_PORT}")
    except Exception as e:
        print(f"WARNING: could not announce on the local network ({type(e).__name__}). "
              f"Devices will need the address typed in.")
    yield
    if announced:
        await azc.async_unregister_service(info)
    await azc.async_close()

app = FastAPI(lifespan=lifespan)
seen = set()                         # (device_id, event_id) pairs already received
log = deque(maxlen=50)               # last 50 requests, newest first

class Alert(BaseModel):
    device_id: str
    event_id: str
    ts: int
    battery_mv: int
    rssi: Optional[int] = None
    type: int

def type_label(t):
    return ALERT_TYPES.get(t, (f"Button {t}", ""))[0]

def record(status, a=None):
    """Remember one request so the web page can show it. Returns the row."""
    row = {
        "at": datetime.now(IST).strftime("%H:%M:%S"),
        "status": status,
        "device": a.device_id if a else "-",
        "event": a.event_id if a else "-",
        "kind": type_label(a.type) if a else "-",
        "battery": a.battery_mv if a else "-",
        "rssi": a.rssi if a else "-",
        "telegram": "-",
    }
    log.appendleft(row)
    return row

# ---------- Telegram ----------
def battery_pct(mv):
    """Rough LiPo estimate: 3.3 V = 0 %, 4.2 V = 100 %."""
    return max(0, min(100, round((mv - 3300) * 100 / 900)))

def build_message(a, when):
    """The text caregivers receive, for example:
    ✋ Grandma: I need help
    Time: 23 Nov 2026, 08:14 PM
    Battery: 67%
    """
    name = DEVICE_NAMES.get(a.device_id, a.device_id)
    label, icon = ALERT_TYPES.get(a.type, (f"Button {a.type}", "\U0001F514"))   # bell if unknown
    when_txt = datetime.fromtimestamp(when, IST).strftime("%d %b %Y, %I:%M %p")
    return (f"{icon} {name}: {label}\n"
            f"Time: {when_txt}\n"
            f"Battery: {battery_pct(a.battery_mv)}%")

def send_telegram(chat_id, text):
    r = httpx.post(f"https://api.telegram.org/bot{BOT_TOKEN}/sendMessage",
                   data={"chat_id": chat_id, "text": text}, timeout=10)
    r.raise_for_status()

def notify(a, when, row):
    """Runs after the reply to the device has been sent."""
    if not BOT_TOKEN or not CHAT_IDS:
        row["telegram"] = "not configured"
        return
    text = build_message(a, when)
    sent = 0
    for chat_id in CHAT_IDS:
        try:
            send_telegram(chat_id, text)
            sent += 1
        except httpx.HTTPStatusError as e:
            # Telegram's reply explains why (never print the error itself: it contains the token)
            print(f"Telegram FAILED for {chat_id}: {e.response.status_code} {e.response.text}")
        except httpx.HTTPError as e:
            print(f"Telegram FAILED for {chat_id}: {type(e).__name__}")
    row["telegram"] = f"sent {sent}/{len(CHAT_IDS)}"

# ---------- Web page ----------
PAGE_HEAD = """<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="refresh" content="3"><title>Distress server</title>
<style>
body{font-family:system-ui,sans-serif;margin:16px}
table{border-collapse:collapse;width:100%}
td,th{border-bottom:1px solid #ccc;padding:6px 8px;text-align:left}
tr.ok{background:#e3f6e3} tr.duplicate{background:#eee} tr.bad{background:#fbe3e3}
</style></head><body>
<h2>Server is running</h2>
<p>Newest first. Refreshes every 3 seconds.</p>
<table><tr><th>Time (IST)</th><th>Result</th><th>Device</th><th>Event</th>
<th>Button</th><th>Battery mV</th><th>RSSI</th><th>Telegram</th></tr>"""

@app.get("/", response_class=HTMLResponse)
def page():
    css = {"OK": "ok", "DUPLICATE": "duplicate", "BAD KEY": "bad"}
    rows = "".join(
        f"<tr class='{css.get(r['status'], '')}'><td>{r['at']}</td><td>{r['status']}</td>"
        f"<td>{escape(str(r['device']))}</td><td>{escape(str(r['event']))}</td>"
        f"<td>{escape(str(r['kind']))}</td><td>{r['battery']}</td><td>{r['rssi']}</td>"
        f"<td>{r['telegram']}</td></tr>"
        for r in log)
    return PAGE_HEAD + rows + "</table></body></html>"

# ---------- API ----------
@app.get("/api/hello")
def hello(nonce: str = ""):
    """Key check for devices that found us automatically: prove we know the shared key
    by returning HMAC-SHA256(key, nonce). The key itself is never sent."""
    if not API_KEY or not (8 <= len(nonce) <= 64):
        raise HTTPException(status_code=400, detail="bad request")
    proof = hmac.new(API_KEY.encode(), nonce.encode(), hashlib.sha256).hexdigest()
    return {"service": "distress", "proof": proof}

@app.post("/api/alert")
def receive_alert(a: Alert, background: BackgroundTasks, x_api_key: str = Header(None)):
    # 1. Check the password
    if not API_KEY or not secrets.compare_digest((x_api_key or "").encode(), API_KEY.encode()):
        record("BAD KEY")
        raise HTTPException(status_code=401, detail="bad api key")

    # 2. Ignore repeats. Still answer 200, so the device deletes its copy.
    key = (a.device_id, a.event_id)
    if key in seen:
        record("DUPLICATE", a)
        return {"status": "duplicate"}
    seen.add(key)

    # 3. Record it, answer the device, and send the Telegram message afterwards.
    when = a.ts if a.ts > 1_700_000_000 else int(time.time())   # device clock not set yet?
    row = record("OK", a)
    print(f"ALERT '{row['kind']}' from {a.device_id} | event {a.event_id} | "
          f"battery {a.battery_mv} mV | rssi {a.rssi} | time {when}")
    background.add_task(notify, a, when, row)
    return {"status": "ok"}