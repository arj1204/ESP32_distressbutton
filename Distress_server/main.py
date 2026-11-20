from typing import Optional
from collections import deque
from datetime import datetime
from html import escape
from zoneinfo import ZoneInfo
import time

from fastapi import FastAPI, Header, HTTPException
from fastapi.responses import HTMLResponse
from pydantic import BaseModel

API_KEY = "test-key"                 # must match API_KEY in the firmware's secrets.h
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
    """Remember one request so the web page can show it."""
    log.appendleft({
        "at": datetime.now(IST).strftime("%H:%M:%S"),
        "status": status,
        "device": a.device_id if a else "-",
        "event": a.event_id if a else "-",
        "kind": TYPE_NAMES.get(a.type, str(a.type)) if a else "-",
        "battery": a.battery_mv if a else "-",
        "rssi": a.rssi if a else "-",
    })

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
<th>Type</th><th>Battery mV</th><th>RSSI</th></tr>"""

@app.get("/", response_class=HTMLResponse)
def page():
    css = {"OK": "ok", "DUPLICATE": "duplicate", "BAD KEY": "bad"}
    rows = "".join(
        f"<tr class='{css.get(r['status'], '')}'><td>{r['at']}</td><td>{r['status']}</td>"
        f"<td>{escape(str(r['device']))}</td><td>{escape(str(r['event']))}</td>"
        f"<td>{r['kind']}</td><td>{r['battery']}</td><td>{r['rssi']}</td></tr>"
        for r in log)
    return PAGE_HEAD + rows + "</table></body></html>"

@app.post("/api/alert")
def receive_alert(a: Alert, x_api_key: str = Header(None)):
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

    # 3. Handle it. Later this is where it gets saved and sent to Telegram.
    record("OK", a)
    when = a.ts if a.ts > 1_700_000_000 else int(time.time())
    kind = TYPE_NAMES.get(a.type, str(a.type))
    print(f"ALERT {kind} from {a.device_id} | event {a.event_id} | "
          f"battery {a.battery_mv} mV | rssi {a.rssi} | time {when}")
    return {"status": "ok"}