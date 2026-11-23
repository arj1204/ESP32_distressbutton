# Distress Button

A low-cost, battery-powered button for someone who may find it hard to speak up. Each of six buttons sends a different message, such as "I need help" or "I'm hungry". The message travels over WiFi to a small server, which forwards it on Telegram to every caregiver's phone within seconds.

> **Not a certified emergency system.** It is a prototype. Test it regularly and don't rely on it alone for anyone's safety.

---

## Contents

1. [What it does](#1-what-it-does)
2. [Project status](#2-project-status)
3. [How it works](#3-how-it-works)
4. [Hardware](#4-hardware)
5. [Project files](#5-project-files)
6. [Quick start](#6-quick-start)
7. [Configuration](#7-configuration)
8. [Using it in several places](#8-using-it-in-several-places)
9. [Buttons and messages](#9-buttons-and-messages)
10. [Device behaviour](#10-device-behaviour)
11. [Server API](#11-server-api)
12. [LED and buzzer signals](#12-led-and-buzzer-signals)
13. [Testing](#13-testing)
14. [Troubleshooting](#14-troubleshooting)
15. [Security](#15-security)
16. [Limitations and roadmap](#16-limitations-and-roadmap)
17. [Documents](#17-documents)

---

## 1. What it does

1. Someone presses a button, for example **"I need help"**.
2. The device wakes from deep sleep, blinks once, joins WiFi and finds the server by itself.
3. It sends the alert. The server confirms, and the device blinks twice.
4. Every caregiver gets a Telegram message:
   ```
   ✋ Grandma: I need help
   Time: 23 Nov 2026, 08:14 PM
   Battery: 67%
   ```

The device can move between places. At each place you start the server on any computer and add that WiFi to the device **from a phone**. You never change the firmware.

---

## 2. Project status

| Area | Status |
|---|---|
| Six message buttons plus a test button, debounce, priority order | Done, tested |
| Deep sleep, wake on any button, stuck-button handling | Done, tested |
| Several presses: every different button sent, repeats within 30 s not resent | Done |
| LED or buzzer feedback | Done, tested |
| Server: key check, duplicate handling, live web page | Done, tested |
| Telegram messages with an icon per button | Done, tested |
| WiFi setup page from a phone, up to 5 saved networks | Done. First hardware test worked |
| Automatic server discovery on the local network | Done. First hardware test found the server |
| Server key check during discovery | Done, waiting for a hardware test |
| Saving failed alerts and retrying them later | **Not yet** |
| Real battery reading | **Not yet** |
| Real time from the internet (NTP) | **Not yet** |
| HTTPS, database, caregiver dashboard, "device offline" alerts | **Not yet** |

---

## 3. How it works

```
[Button] → [ESP32 wakes] → [WiFi] → [find server] → [send alert] → [Server] → [Telegram] → [Phones]
```

### On the device

- **Asleep almost all the time.** Only a tiny always-on part of the chip watches the button pins.
- **A press wakes it.** The chip remembers which button did it, even after a very short tap.
- **It joins the strongest saved WiFi network in range** (up to 5 are saved).
- **It finds the server** in this order:
  1. a server address typed on the setup page for this network,
  2. the server it already found on this network (remembered until power-off),
  3. a new search of the network,
  4. `SERVER_URL` in `config.h`, as a last resort.
- **It checks the server is genuine:** a found server must prove it knows the shared API key.
- **It sends the alert.** It trusts that the alert arrived only when the server replies `200`.
- **It goes back to sleep** about 5 s after the last activity.

### On the server

- **Announces itself** on the local network, so devices can find it.
- **Checks the device's API key,** ignores repeated events, and shows every request on a live web page.
- **Sends the Telegram message** to every caregiver after answering the device.

**Why a server in the middle?** It keeps the Telegram token off the device, holds the caregiver list in one place, ignores duplicates, and keeps a history.

---

## 4. Hardware

| Part | Details |
|---|---|
| Board | ESP32-WROVER-E (chip ESP32-D0WD-V3) or any classic ESP32 |
| Power (final build) | 3.7 V LiPo, low-idle-current 3.3 V LDO, USB-C charging with a TP4056 |
| Buttons | 6 push buttons |
| Feedback | LED (testing) or an active buzzer through an NPN transistor |
| Battery sensing | Voltage divider into GPIO 35 (not used by the firmware yet) |

### Pins

| GPIO | Use |
|:---:|---|
| 13, 26, 33, 14, 27, 32 | The six buttons (see [section 9](#9-buttons-and-messages)) |
| 33 | Also the **setup button**: hold it at power-on to open the WiFi setup page |
| 0 | BOOT button = Test button (testing only) |
| 25 | Buzzer or LED (use 2 for the board's own LED) |
| 35 | Battery voltage (later) |

**Avoid:** 1 and 3 (USB serial), 6 to 11 (flash), 16 and 17 (PSRAM), 2, 5, 12 and 15 (boot-mode pins).

### Wiring

```
Buttons (x6):  3V3 ──[button]── GPIO          + 1 MΩ from GPIO to GND on the final board
LED (testing): GPIO 25 ──[330 Ω]── LED ── GND
Buzzer:        3V3 ── buzzer ── NPN collector;  GPIO 25 ──[1 kΩ]── NPN base;  emitter ── GND
               (+ 100 kΩ from base to GND so it stays off during sleep)
Battery:       Battery+ ──[1 MΩ]──┬──[1 MΩ]── GND
                                  └── GPIO 35 (+ 100 nF to GND)
```

- **Why the buttons connect to 3V3:** the chip can wake when *any* button pin goes HIGH, and it records which one did it. It can't do that for LOW.
- **Bench testing:** with `BENCH_TEST 1`, the chip's internal pull-downs replace the 1 MΩ resistors.
- **Never** connect 5V or VIN to a GPIO.
- **Judge sleep current on your own board, not a dev board.** A dev board's USB chip and LEDs use far more than the ESP32 does asleep.

---

## 5. Project files

```
Distress_button/                  firmware (PlatformIO project)
├── platformio.ini                board settings + WiFiManager library
├── include/
│   ├── config.h                  feature flags, buttons, pins, timings
│   ├── secrets.h                 API key, setup password (never share)
│   └── network.h                 network functions used by main.cpp
└── src/
    ├── main.cpp                  buttons, sleep, wake, alerts, feedback
    └── network.cpp               WiFi, setup page, discovery, key check, sending

Distress_server/                  server
├── main.py                       receives alerts, announces itself, sends Telegram
├── requirements.txt              fastapi, uvicorn[standard], httpx, python-dotenv, tzdata, zeroconf
└── .env                          token, chat IDs, API key (never share)
```

Keep secrets out of git with a `.gitignore`:
```
.env
.venv/
include/secrets.h
```

---

## 6. Quick start

Full step-by-step instructions, with Windows commands and checks, are in **`SETUP_GUIDE.md`**.

**1. Telegram bot** (`TELEGRAM_BOT_GUIDE.md`)
In @BotFather, send `/newbot` and copy the token. Each caregiver presses **Start** in the bot. Get their chat IDs from `https://api.telegram.org/bot<TOKEN>/getUpdates`.

**2. Server**
```bash
cd Distress_server
python3 -m venv .venv
source .venv/bin/activate              # Windows: .venv\Scripts\Activate.ps1
pip install -r requirements.txt
```
Create `.env`:
```
TELEGRAM_BOT_TOKEN=123456789:AAH-your-token
TELEGRAM_CHAT_IDS=987654321,111222333
DEVICE_API_KEY=your-long-random-key
```
Start it, and allow Python through the firewall if asked:
```bash
uvicorn main:app --host 0.0.0.0 --port 8000
```
Look for `Announcing on the local network: _distress._tcp at ...`, then open `http://localhost:8000/`.

**3. Firmware**
- Open `Distress_button` in VS Code with PlatformIO, in a path with no spaces.
- In `secrets.h`, set `API_KEY` (same as `DEVICE_API_KEY`) and `SETUP_AP_PASS` (at least 8 characters).
- Build (✓), upload (→), and open the serial monitor (115200).

**4. WiFi setup**
On the first power-on, the setup page opens by itself (3 blinks).
1. On a phone, join **`Distress-Setup-dev1`** with `SETUP_AP_PASS`.
2. Choose your 2.4 GHz WiFi and enter its password.
3. Leave **Server address** empty, then save (2 blinks).

**5. Press a button**
The serial monitor should show `passed the key check` and `Server replied: 200`, and the Telegram message arrives.

---

## 7. Configuration

### Feature and test flags (`config.h`)

Set them to `1` or `0`, then rebuild and upload. Details are in **`FLAGS_GUIDE.md`**.

| Flag | Default | What `1` does | Final build |
|---|:---:|---|:---:|
| `ENABLE_WIFI_SETUP` | 1 | WiFi entered on a phone setup page (`0` = WiFi from `secrets.h`) | 1 |
| `ENABLE_SERVER_DISCOVERY` | 1 | Finds the server on the network by itself (`0` = typed address or `SERVER_URL`) | 1 |
| `VERIFY_SERVER` | 1 | A found server must prove it knows the API key | 1 |
| `ENABLE_BOOT_BUTTON_TEST` | 1 | BOOT button sends a Test alert | **0** |
| `BENCH_TEST` | 1 | Internal pull-downs instead of 1 MΩ resistors | **0** (resistors fitted) |
| `ENABLE_TIMER_WAKEUP` | 0 | Also wakes every 30 s | 0 |
| `LED_TIMING` | 1 | Slow blinks for an LED (`0` = short buzzer beeps) | **0** |

> Never set `BENCH_TEST 0` without the six 1 MΩ resistors. The pins would float, causing random wake-ups and false alerts.

### Main settings

| File | Setting | Purpose |
|---|---|---|
| `config.h` | `DEVICE_ID` | Device name sent with alerts (`dev1`) |
| `config.h` | `SERVER_URL` | Last-resort server address. `""` disables it |
| `config.h` | `BUZZER_PIN` | 25 for an external LED or buzzer, 2 for the onboard LED |
| `config.h` | `SETUP_BUTTON_PIN` | 33, held at power-on to open the setup page |
| `config.h` | `ALERT_COOLDOWN_S` | 30. A repeat of the same button within this time is not resent |
| `config.h` | `BUTTONS[]` | Pin, type and name of each button. **Row order = priority** |
| `secrets.h` | `API_KEY` | Shared key, the same as the server's `DEVICE_API_KEY` |
| `secrets.h` | `SETUP_AP_PASS` | Password for the device's setup network (at least 8 characters) |
| `secrets.h` | `WIFI_SSID`, `WIFI_PASS` | Used only when `ENABLE_WIFI_SETUP 0` |
| `.env` | `TELEGRAM_BOT_TOKEN`, `TELEGRAM_CHAT_IDS` | Bot and caregivers |
| `.env` | `DEVICE_API_KEY` | Shared key. Without it, the server rejects every alert |
| `.env` | `SERVER_PORT`, `ANNOUNCE_IP` | Optional: announced port, and an address override (for example with a VPN) |
| `main.py` | `DEVICE_NAMES` | `device_id` to the name in messages |
| `main.py` | `ALERT_TYPES` | Type number to the button text and icon |

### Values that must match

| What | Server | Firmware |
|---|---|---|
| API key | `DEVICE_API_KEY` in `.env`, on **every** server | `API_KEY` in `secrets.h` |
| Device ID | key in `DEVICE_NAMES` | `DEVICE_ID` in `config.h` |
| Button types | `ALERT_TYPES` | type numbers in `BUTTONS[]` |
| Port | `SERVER_PORT` in `.env` | (found automatically) |

Generate a key with:
```bash
python3 -c "import secrets; print(secrets.token_urlsafe(24))"
```

---

## 8. Using it in several places

**At a new place:**
1. Start the server on any computer there, with the **same `.env`**.
2. Hold **GPIO 33** while pressing EN (or switching on), and keep holding for 3 s until you see 3 blinks.
3. On the setup page, add that WiFi and leave the server address empty.
4. Press a button. The device finds the new server by itself.

**Saved networks:**
- Up to 5. When full, adding another replaces the oldest.
- Saving a network with the same name updates it.
- When sending, the device joins the strongest saved network in range.

**If a network blocks discovery** (some guest, college or office WiFi), add that network again and type the server computer's address, for example `192.168.1.5:8000`. That address is used only on that network.

**If the server restarts with a new address,** the device's first send fails, so it searches again and retries straight away.

---

## 9. Buttons and messages

| Priority | GPIO | Type | Button | Icon in Telegram |
|:---:|:---:|:---:|---|:---:|
| 1 | 13 | 1 | I need help | ✋ |
| 2 | 26 | 3 | I'm overwhelmed | 😵 |
| 3 | 33 | 6 | I want to leave | 🚪 |
| 4 | 14 | 2 | I need a break | ⏸️ |
| 5 | 27 | 4 | I'm hungry | 🍎 |
| 6 | 32 | 5 | I'm okay | 👍 |
| 7 | 0 (BOOT) | 7 | Test button | 🧪 |

**To rename or reorder buttons:** edit `BUTTONS[]` in `config.h` and `ALERT_TYPES` in `main.py`, and keep the type numbers matching on both sides. Row order in `BUTTONS[]` sets the priority.

---

## 10. Device behaviour

| Situation | What happens |
|---|---|
| One press | 1 blink, then the alert is sent, then 2 blinks (or 1 long blink on failure) |
| Several different buttons together | All are sent, highest priority first, over one WiFi connection |
| The same button again within 30 s of a confirmed send | 1 short blink. Noted, not resent |
| The same button after a **failed** send | Sent again (the 30 s cooldown starts only after success) |
| A second button pressed while the device is awake | Sent too |
| A very quick tap **during** a send | Can be missed. A held press is noticed afterwards |
| A button held or jammed | One alert only. That button is ignored until released, other buttons still work, and it's re-checked after 30 s, then at longer intervals up to 10 min |
| No activity | Sleeps about 5 s after the last activity. Never stays awake longer than 60 s (except on the setup page) |
| Power-on with a button held | No alert. Holding GPIO 33 for 3 s opens the setup page |

**What's stored where:**

| Data | Where | After power-off |
|---|---|---|
| Saved WiFi networks and typed server addresses | Flash | Kept (also kept after a firmware upload) |
| Event number counter | Flash | Kept |
| Server found by discovery | RTC memory | Searched again |
| Cooldowns, stuck-button state | RTC memory | Reset |

Reading the saved networks takes a few milliseconds. Most of the time per alert goes on joining WiFi (about 2 to 6 s) and, the first time on a network, finding the server (about 1 to 3 s).

---

## 11. Server API

### `POST /api/alert`

Header `X-API-Key: <key>`, with a JSON body:
```json
{"device_id":"dev1","event_id":"24","ts":1763600000,"battery_mv":3900,"rssi":-59,"type":1}
```

| Field | Meaning |
|---|---|
| `device_id` | Which device sent it |
| `event_id` | Unique per press from that device |
| `ts` | Unix time. Until NTP is added, the server uses its own arrival time |
| `battery_mv` | Battery voltage (placeholder for now) |
| `rssi` | WiFi signal strength |
| `type` | Button type (section 9) |

| Reply | Meaning |
|---|---|
| `200 {"status":"ok"}` | New alert accepted. Telegram is sent afterwards |
| `200 {"status":"duplicate"}` | Already received. Ignored, but `200` so the device stops retrying |
| `401` | Wrong key, or no `DEVICE_API_KEY` on the server |
| `422` | Missing or wrong fields |

### `GET /api/hello?nonce=<8 to 64 characters>`

The key check used by devices that found the server automatically. It returns `{"service":"distress","proof":"<HMAC-SHA256 of nonce with the key, hex>"}`. The key itself is never sent. It returns `400` if the server has no key.

### `GET /`

A live page with the last 50 requests, refreshing every 3 s:
- green = new alert,
- grey = duplicate,
- red = wrong key.

The last column shows the Telegram result.

### Discovery

The server announces the mDNS service `_distress._tcp` on its port, using the `zeroconf` package. On a Mac, check it with `dns-sd -B _distress._tcp`.

---

## 12. LED and buzzer signals

| Pattern | Meaning |
|---|---|
| 3 short | Setup page opened |
| 1 short, pause, 2 short | Alert sent and confirmed |
| 1 short, pause, 1 long | Alert failed |
| 1 short only | Repeat within 30 s: noted, not resent |
| 2 short (setup) | WiFi saved |
| 1 long (setup) | Setup page closed without saving |

With `LED_TIMING 1`, a short blink is 0.3 s and a long one 1.5 s. With `LED_TIMING 0` (buzzer), they're 0.1 s and 0.6 s.

---

## 13. Testing

The full 13-step checklist is in `SETUP_GUIDE.md`. The essentials:

1. Each button sends the right message and icon.
2. Two buttons together both arrive, in priority order.
3. The same button 3 times within 30 s sends one message.
4. Holding one button while pressing another sends both.
5. With the server stopped you get a long blink; with it started again the next press is sent.
6. A server restarted on another computer is found again automatically.
7. A wrong key fails the key check.
8. A second WiFi network added from the phone works.

**Testing with only the dev board:** tap BOOT (Test button), or touch a jumper from **3V3** to a button pin.

---

## 14. Troubleshooting

| Symptom | Likely cause |
|---|---|
| `did not answer the key check (400)` and `Server replied: 401` | `DEVICE_API_KEY` missing from the server's `.env`, or the server wasn't restarted after editing it |
| `FAILED the key check` | Keys differ between `.env` and `secrets.h` |
| `Discovery: no server found` | Server not running or not announcing, firewall blocking Python, a different WiFi, or a network that blocks discovery. Type the address on the setup page |
| `POST error: connection refused` | Server not running, or started without `--host 0.0.0.0` |
| `WiFi: no saved networks` | Open the setup page and add one |
| `WiFi: FAILED` | Wrong password, a 5 GHz-only network, or out of range |
| `[E][WiFiMulti...] Connecting Failed (6)` | Harmless if it then connects. Its first attempt timed out |
| `Distress-Setup-dev1` doesn't appear | `SETUP_AP_PASS` is shorter than 8 characters |
| Setup page won't open when holding the button | Hold GPIO 33 **before** pressing EN and keep holding 3 s |
| Telegram `not configured` | `.env` missing or misnamed (`.env.txt`), or the server wasn't restarted |
| Telegram `403` or `400` | That person didn't press Start, or the chat ID is wrong |
| Random wake-ups | `BENCH_TEST 0` without resistors |
| Upload stuck on "Connecting..." | Hold BOOT while it starts |
| CMake error in VS Code | Disable the CMake Tools extension for the workspace |

More are listed in `SETUP_GUIDE.md`, section 14.

---

## 15. Security

- **Bot token:** only in the server's `.env`. Never put it in the firmware, chats or git. Revoke it in BotFather if it leaks.
- **API key:** use a long random key, the same on every server and device. It's compiled into the firmware, so share firmware only with people you trust.
- **Key check:** a server found automatically must prove it knows the key, so a stranger's computer can't collect your alerts.
- **Setup password:** change `SETUP_AP_PASS` from the example before giving the device to anyone.
- **Plain HTTP on the local network:** traffic isn't encrypted. Use HTTPS before running a server on the internet.
- **WiFi passwords** are stored unencrypted in the device's flash.
- **The server page** has no login, so anyone on the same network can view it.

---

## 16. Limitations and roadmap

**Limitations now:**
- A failed alert isn't stored, so it's lost after the long blink.
- No real clock and no battery reading yet.
- A very quick tap during a send can be missed.
- The server forgets its list and its duplicate memory on restart.
- Each location needs a computer running the server, which needs internet to reach Telegram.

**Planned, in order:**

| # | Step |
|:---:|---|
| 1 | Flash queue: save each alert first, retry with backoff, wake on a timer to resend |
| 2 | Real battery reading and low-battery alerts |
| 3 | NTP time |
| 4 | Faster WiFi reconnect (remember the last channel and access point) |
| 5 | Database, caregiver dashboard with login, per-device keys |
| 6 | Automatic device ID from the chip, so every board is unique without editing code |
| 7 | Hourly heartbeat and a "device offline" alert |
| 8 | Optional central internet server with HTTPS |
| 9 | Over-the-air firmware updates |

---

## 17. Documents

| File | What's in it |
|---|---|
| `README.md` | This overview |
| `SETUP_GUIDE.md` | Complete step-by-step setup (Mac and Windows), tests and troubleshooting |
| `FLAGS_GUIDE.md` | Every flag and setting explained, with recommended combinations |
| `TELEGRAM_BOT_GUIDE.md` | Creating the bot, finding chat IDs, adding caregivers |

`SERVER_SETUP_WINDOWS.md` is replaced by `SETUP_GUIDE.md`, which now covers Windows too.
