from typing import Optional
from collections import deque
from datetime import datetime
from html import escape
from zoneinfo import ZoneInfo
import os
import time

import httpx
from dotenv import load_dotenv
from fastapi import BackgroundTasks, FastAPI, Header, HTTPException
from fastapi.responses import HTMLResponse
from pydantic import BaseModel

load_dotenv()                                   # reads the .env file next to this script
BOT_TOKEN = os.getenv("TELEGRAM_BOT_TOKEN", "")
CHAT_IDS = [c.strip() for c in os.getenv("TELEGRAM_CHAT_IDS", "").split(",") if c.strip()]

API_KEY = "test-key"                 # must match API_KEY in the firmware's secrets.h
DEVICE_NAMES = {"dev1": "Grandma"}   # device_id -> name used in the message
TYPE_NAMES = {1: "MEDICAL", 2: "FIRE", 3: "SECURITY", 4: "FALL",
              5: "HELP", 6: "CALL ME", 7: "TEST"}
IST = ZoneInfo("Asia/Kolkata")

app = FastAPI()
seen = set()                         # (device_id, event_id) pairs already received
log = deque(maxlen=50)               # last 50 requests, newest first

class Alert(BaseModel):
    device_id: str
    event_id: str
    ts: int
    battery_mv: int
    rssi: Optional[int] = None
    type: int

def record(status, a=None):
    """Remember one request so the web page can show it. Returns the row."""
    row = {
        "at": datetime.now(IST).strftime("%H:%M:%S"),
        "status": status,
        "device": a.device_id if a else "-",
        "event": a.event_id if a else "-",
        "kind": TYPE_NAMES.get(a.type, str(a.type)) if a else "-",
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

def send_telegram(chat_id, text):
    r = httpx.post(f"https://api.telegram.org/bot{BOT_TOKEN}/sendMessage",
                   data={"chat_id": chat_id, "text": text}, timeout=10)
    r.raise_for_status()

def notify(a, when, row):
    """Runs after the reply to the device has been sent."""
    if not BOT_TOKEN or not CHAT_IDS:
        row["telegram"] = "not configured"
        return
    name = DEVICE_NAMES.get(a.device_id, a.device_id)
    kind = TYPE_NAMES.get(a.type, str(a.type))
    when_txt = datetime.fromtimestamp(when, IST).strftime("%d %b %Y, %I:%M %p")
    text = (f"ALERT: {name} needs help: {kind}. "
            f"Time: {when_txt}. Battery: {battery_pct(a.battery_mv)}%.")

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
<th>Type</th><th>Battery mV</th><th>RSSI</th><th>Telegram</th></tr>"""

@app.get("/", response_class=HTMLResponse)
def page():
    css = {"OK": "ok", "DUPLICATE": "duplicate", "BAD KEY": "bad"}
    rows = "".join(
        f"<tr class='{css.get(r['status'], '')}'><td>{r['at']}</td><td>{r['status']}</td>"
        f"<td>{escape(str(r['device']))}</td><td>{escape(str(r['event']))}</td>"
        f"<td>{r['kind']}</td><td>{r['battery']}</td><td>{r['rssi']}</td>"
        f"<td>{r['telegram']}</td></tr>"
        for r in log)
    return PAGE_HEAD + rows + "</table></body></html>"

# ---------- API ----------
@app.post("/api/alert")
def receive_alert(a: Alert, background: BackgroundTasks, x_api_key: str = Header(None)):
    # 1. Check the password
    if x_api_key != API_KEY:
        record("BAD KEY")
        raise HTTPException(status_code=401, detail="bad api key")

    # 2. Ignore repeats. Still answer 200, so the device deletes its copy.
    key = (a.device_id, a.event_id)
    if key in seen:
        record("DUPLICATE", a)
        return {"status": "duplicate"}
    seen.add(key)

    # 3. Save it, answer the device, and send the Telegram message afterwards.
    when = a.ts if a.ts > 1_700_000_000 else int(time.time())   # device clock not set yet?
    row = record("OK", a)
    print(f"ALERT {row['kind']} from {a.device_id} | event {a.event_id} | "
          f"battery {a.battery_mv} mV | rssi {a.rssi} | time {when}")
    background.add_task(notify, a, when, row)
    return {"status": "ok"}