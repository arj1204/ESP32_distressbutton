# Telegram Bot Guide for the Distress Button

How to create the Telegram bot that sends alerts, find the chat IDs, and add caregivers.

You need: the Telegram app (phone or desktop) with an account, and about 10 minutes.

**Treat the bot token like a password.** Anyone who has it can control your bot. Never paste it into chats, screenshots, emails or public repositories.

---

## Part 1: Create the bot

1. In Telegram, search for **@BotFather**. Choose the one with the blue verified tick and open it.
2. Press **Start**, then send:
   ```
   /newbot
   ```
3. BotFather asks for a **display name**. This is what caregivers see on every alert. It can be anything and can contain spaces, for example `Care Alert`.
4. It then asks for a **username**:
   - must end in `bot`
   - 5 to 32 characters, using only letters, digits and underscores
   - must be unused anywhere on Telegram, for example `care_alert_yourname_bot`

   If it's taken, BotFather says so. Add your name or a number and try again.
5. BotFather replies with a message containing the **token**, which looks like:
   ```
   123456789:AAH-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
   ```
   Copy it somewhere private. You'll put it in `.env` as `TELEGRAM_BOT_TOKEN`.

To see the token again later: send `/mybots` to BotFather, choose your bot, then **API Token**.

If a token ever leaks, create a new one right away: `/mybots` → your bot → **API Token** → **Revoke current token**. Then update `.env`.

---

## Part 2: Start your bot

A bot can only message people who have started it first.

1. Open your new bot with the `t.me/<username>` link BotFather gave you, or search for its exact username. **Open your bot, not BotFather.**
2. Press **Start**, then send `hello`.

---

## Part 3: Find your chat ID

The chat ID isn't shown in the Telegram app. You read it from Telegram's server.

Replace `<TOKEN>` with your token (keep the word `bot` in front of it).

**Windows (PowerShell):**
```powershell
Invoke-RestMethod "https://api.telegram.org/bot<TOKEN>/getUpdates" | ConvertTo-Json -Depth 6
```

**Mac or Linux:**
```bash
curl -s "https://api.telegram.org/bot<TOKEN>/getUpdates" | python3 -m json.tool
```

**Or in a browser:** paste `https://api.telegram.org/bot<TOKEN>/getUpdates` into the address bar. (The token then stays in your browser history.)

Look for the `id` inside `chat`:
```json
"message": {
  "from": { "id": 987654321, "first_name": "Athul" },
  "chat": {
    "id": 987654321,          <-- this number is the chat ID
    "type": "private"
  },
  "text": "hello"
}
```
Copy just the digits.

If `"result": []` is empty, no message has reached this bot yet. Send it `hello` again, check you messaged **your** bot, and run the command again. Telegram only keeps unread messages for about 24 hours.

---

## Part 4: Send a test message

Replace `<TOKEN>` and `<CHAT_ID>`.

**Windows (PowerShell):**
```powershell
Invoke-RestMethod -Method Post -Uri "https://api.telegram.org/bot<TOKEN>/sendMessage" -Body @{ chat_id = "<CHAT_ID>"; text = "ALERT test" }
```

**Mac or Linux:**
```bash
curl -s -X POST "https://api.telegram.org/bot<TOKEN>/sendMessage" -d chat_id=<CHAT_ID> -d text="ALERT test"
```

The message should appear in the bot chat on your phone.

---

## Part 5: Add more caregivers

Each caregiver does this once:
1. Opens your bot (send them the `t.me/<username>` link).
2. Presses **Start**, then sends `hello`.

Then you run the `getUpdates` command from Part 3 again. Each person who messaged the bot appears as a separate entry with their own `chat.id` and `first_name`. Match the name to the person.

---

## Part 6: Put the values in the server

In the server's `.env` file:
```
TELEGRAM_BOT_TOKEN=123456789:AAH-your-token-here
TELEGRAM_CHAT_IDS=987654321,111222333
DEVICE_API_KEY=your-long-random-key
```
- `DEVICE_API_KEY` is the shared key, the same as `API_KEY` in the firmware's `secrets.h`.
- One caregiver: just one number.
- Several: separate by commas, no quotes.
- **Restart the server** after any change. It only reads `.env` at start-up.

Test: send a fake alert to the server (see `SETUP_GUIDE.md`, section 6.5). The web page should show `sent 2/2` in the Telegram column, and each caregiver gets:
```
✋ Grandma: I need help
Time: 23 Nov 2026, 08:14 PM
Battery: 67%
```

---

## Part 7: Make sure caregivers actually notice alerts

A missed notification defeats the purpose. Ask each caregiver to:

- **Not mute the bot's chat.** Open the chat, tap the name, and make sure notifications are on.
- **Set a distinct sound** for that chat (chat settings → Notifications → Sound).
- **Allow Telegram to override Do Not Disturb or Focus modes** if their phone allows it.
- **On Android:** turn off battery optimisation for Telegram, so notifications aren't delayed.
- **Test it once** with a real button press, with the phone locked and in their pocket.

Telegram isn't a certified emergency system, so don't rely on it alone for anyone's safety.

---

## Optional: polish the bot

Send these to BotFather (it asks which bot, then for the value):

| Command | Does |
|---|---|
| `/setname` | Change the display name |
| `/setdescription` | Text shown when someone first opens the bot |
| `/setuserpic` | Set a profile picture |

Avoid names that suggest an official emergency service. This project is not one.

---

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `getUpdates` gives `"result": []` | No message sent to your bot yet. Send `hello` to your own bot, not BotFather |
| `"ok": false` and `Unauthorized` | The token is wrong, has an extra space, or was revoked. Copy it again |
| Server log: `Telegram FAILED ... 403 ... blocked by the user` | That person never pressed Start, or blocked the bot |
| Server log: `Telegram FAILED ... 400 ... chat not found` | Wrong chat ID, or that person hasn't started the bot |
| Server log: `Telegram FAILED ... 401` | Wrong or revoked token in `.env` |
| Page shows `Telegram: not configured` | `TELEGRAM_BOT_TOKEN` or `TELEGRAM_CHAT_IDS` is empty. Check the `.env` name (not `.env.txt`) and restart the server |
| Page shows `sent 1/2` | One caregiver failed. The server log above names the chat ID and the reason |
| Test message works but alerts don't | The server isn't reaching Telegram, or the ESP32 isn't reaching the server. Check the web page first: if a row appears, the server got the alert |
| Messages arrive late on a phone | Phone battery optimisation or Do Not Disturb. See Part 7 |
