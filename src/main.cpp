#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "config.h"
#include "network.h"

// ---------- Debounce state: one entry per button ----------
bool          lastReading[NUM_BUTTONS];   // raw "is pressed" from the previous pass
bool          stableState[NUM_BUTTONS];   // debounced "is pressed"
unsigned long lastChangeMs[NUM_BUTTONS];  // when the raw state last changed

// ---------- Kept in RTC memory: survives deep sleep, cleared at power-on ----------
RTC_DATA_ATTR uint32_t bootCount     = 0;
RTC_DATA_ATTR uint32_t stuckRecheckS = 0;    // grows while a button stays stuck (0 = none stuck)
RTC_DATA_ATTR uint64_t stuckMask     = 0;    // pins that were held (stuck) when we last went to sleep
RTC_DATA_ATTR time_t   lastSentAt[8] = {0};  // per alert type (types 0-7): last confirmed send time

esp_sleep_wakeup_cause_t wakeCause;
unsigned long lastActivityMs = 0;

// ---------- Buzzer / LED ----------
// HIGH switches the NPN transistor (or LED) on.
void beep(uint16_t ms) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms);
  digitalWrite(BUZZER_PIN, LOW);
}

void beepTimes(uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    beep(BEEP_SHORT_MS);
    if (i < n - 1) delay(BEEP_GAP_MS);
  }
}

// ---------- Buttons ----------
// True if button i is physically pressed right now (handles both polarities).
bool isPressed(uint8_t i) {
  return digitalRead(BUTTONS[i].pin) == (BUTTONS[i].activeLow ? LOW : HIGH);
}

bool anyButtonHeld() {
  for (uint8_t i = 0; i < NUM_BUTTONS; i++)
    if (isPressed(i)) return true;
  return false;
}

// Call every pass of loop(). Returns a bit mask of buttons that were just pressed:
// bit i set = BUTTONS[i]. Several buttons can be reported in the same pass.
uint16_t checkButtons() {
  uint16_t pressed = 0;

  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    bool reading = isPressed(i);

    // 1. Raw state changed? Restart the timer.
    if (reading != lastReading[i]) {
      lastChangeMs[i] = millis();
      lastReading[i]  = reading;
    }

    // 2. Steady longer than DEBOUNCE_MS and different from the accepted state?
    if ((millis() - lastChangeMs[i]) > DEBOUNCE_MS && reading != stableState[i]) {
      stableState[i] = reading;
      if (stableState[i]) pressed |= (1u << i);   // newly pressed
    }
  }
  return pressed;
}

// ---------- Event number ----------
// Saved in flash so it never repeats, even after a power loss.
uint32_t nextEventSeq() {
  Preferences p;
  p.begin("distress", false);
  uint32_t s = p.getUInt("seq", 0) + 1;
  p.putUInt("seq", s);
  p.end();
  return s;
}

// ---------- Alert ----------
void handleAlert(const ButtonDef& b) {
  time_t now = time(nullptr);

  // Same button already confirmed very recently: acknowledge the press, but don't send it again.
  if (ALERT_COOLDOWN_S > 0 && lastSentAt[b.type] != 0 &&
      (now - lastSentAt[b.type]) < (time_t)ALERT_COOLDOWN_S) {
    Serial.printf("ALERT: %s again within %lu s of the last one: not resent\n",
                  b.name, (unsigned long)ALERT_COOLDOWN_S);
    beep(BEEP_SHORT_MS);                              // 1 beep: press noted
    lastActivityMs = millis();
    return;
  }

  Serial.printf("ALERT: %s (type %u)\n", b.name, b.type);
  beep(BEEP_SHORT_MS);                                // 1 beep: button registered

  uint32_t seq = nextEventSeq();
  if (sendAlert(seq, b.type, TEST_BATTERY_MV) == 200) {   // joins WiFi, finds the server, sends
    lastSentAt[b.type] = now;                         // cooldown starts only after a confirmed send
    beepTimes(2);                                     // 2 beeps: server confirmed
  } else {
    beep(BEEP_LONG_MS);                               // 1 long beep: failed
  }
  lastActivityMs = millis();                          // WiFi stays on until goToSleep()
}

// ---------- Wake ----------
void printWakeReason() {
  switch (wakeCause) {
    case ESP_SLEEP_WAKEUP_EXT0:  Serial.println("test button (EXT0)"); break;
    case ESP_SLEEP_WAKEUP_EXT1:  Serial.println("button (EXT1)");      break;
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("timer");              break;
    default:                     Serial.println("power-on / reset");
  }
}

// Bit mask of the button pins that woke us (bit n = GPIO n).
// The hardware latches them, so this works even after a very quick tap.
uint64_t wakePinMask() {
  if (wakeCause == ESP_SLEEP_WAKEUP_EXT1) return esp_sleep_get_ext1_wakeup_status();
  if (wakeCause == ESP_SLEEP_WAKEUP_EXT0) {
    for (uint8_t i = 0; i < NUM_BUTTONS; i++)
      if (BUTTONS[i].activeLow) return 1ULL << BUTTONS[i].pin;
  }
  return 0;
}

// ---------- Sleep ----------
void goToSleep() {
  // 1. Give a held button a chance to be released, while still watching for NEW presses.
  unsigned long t0 = millis();
  while (anyButtonHeld() && (millis() - t0) < RELEASE_WAIT_MS) {
    uint16_t presses = checkButtons();
    if (presses) {                                    // someone pressed another button: send it
      for (uint8_t i = 0; i < NUM_BUTTONS; i++)       // priority order
        if (presses & (1u << i)) handleAlert(BUTTONS[i]);
      return;                                         // back to loop(); sleep is tried again later
    }
    delay(10);
  }

  wifiOff();                                          // radio off before sleeping

  // 2. Arm every button that is NOT held. A held one would wake us instantly, so leave it out.
  uint64_t wakeMask = 0;     // active-HIGH buttons
  int lowPin = -1;           // active-LOW test button
  stuckMask = 0;
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    uint64_t bit = 1ULL << BUTTONS[i].pin;
    if (isPressed(i)) {
      stuckMask |= bit;
      Serial.printf("Button '%s' still held: ignored until released\n", BUTTONS[i].name);
      continue;
    }
    if (BUTTONS[i].activeLow) lowPin = BUTTONS[i].pin;
    else                      wakeMask |= bit;
  }

  // 3. Timer: re-check a stuck button, waiting twice as long each time it is still stuck.
  if (stuckMask) {
    uint32_t next = stuckRecheckS ? stuckRecheckS * 2 : STUCK_RECHECK_MIN_S;
    if (next > STUCK_RECHECK_MAX_S) next = STUCK_RECHECK_MAX_S;
    stuckRecheckS = next;
    esp_sleep_enable_timer_wakeup((uint64_t)stuckRecheckS * 1000000ULL);
    Serial.printf("Re-checking in %lu s\n", (unsigned long)stuckRecheckS);
  } else {
    stuckRecheckS = 0;
    if (ENABLE_TIMER_WAKEUP) esp_sleep_enable_timer_wakeup(SLEEP_TIMER_US);
  }

  Serial.println("Going to deep sleep...");
  Serial.flush();
  digitalWrite(BUZZER_PIN, LOW);

#if BENCH_TEST || ENABLE_BOOT_BUTTON_TEST
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);   // keep RTC pull resistors alive
#endif
#if BENCH_TEST
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {                     // internal pull-downs (no resistors)
    if (BUTTONS[i].activeLow) continue;
    rtc_gpio_pullup_dis((gpio_num_t)BUTTONS[i].pin);
    rtc_gpio_pulldown_en((gpio_num_t)BUTTONS[i].pin);
  }
#endif
  if (lowPin >= 0) {
    rtc_gpio_pullup_en((gpio_num_t)lowPin);
    rtc_gpio_pulldown_dis((gpio_num_t)lowPin);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)lowPin, 0);          // BOOT: wake when it goes LOW
  }
  if (wakeMask) esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_HIGH);

  esp_deep_sleep_start();         // never returns
}

#if ENABLE_WIFI_SETUP
// True if the setup button is held continuously for SETUP_HOLD_MS.
bool setupButtonHeld() {
  uint32_t t0 = millis();
  while (millis() - t0 < SETUP_HOLD_MS) {
    if (digitalRead(SETUP_BUTTON_PIN) != HIGH) return false;   // let go early
    delay(20);
  }
  return true;
}
#endif

// ---------- Setup: runs once per wake ----------
void setup() {
  setCpuFrequencyMhz(80);                  // saves power, still enough for WiFi
  Serial.begin(115200);
  delay(STARTUP_DELAY_MS);

  Serial.printf("Chip: %s rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("Test (BOOT) button: %s\n", ENABLE_BOOT_BUTTON_TEST ? "ENABLED" : "masked");
  Serial.printf("Timer wake: %s\n", ENABLE_TIMER_WAKEUP ? "ENABLED" : "disabled");
#if ENABLE_WIFI_SETUP
  Serial.printf("WiFi: setup page enabled, %u network(s) saved\n", (unsigned)savedNetworkCount());
#else
  Serial.println("WiFi: fixed network from secrets.h");
#endif
  Serial.printf("Server: %s\n", ENABLE_SERVER_DISCOVERY ? "found automatically" : SERVER_URL);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  wakeCause = esp_sleep_get_wakeup_cause();
  uint64_t woke    = wakePinMask();                     // buttons that woke us (sent below)
  bool     powerOn = (wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED);

  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    pinMode(BUTTONS[i].pin, BUTTONS[i].activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
    uint64_t bit  = 1ULL << BUTTONS[i].pin;
    bool     held = isPressed(i);
    // A held button counts as "already seen" if it woke us (sent below), was stuck before
    // sleep, or was held at power-on. Any other held button is a NEW press: loop() reports it.
    bool seen = powerOn || (woke & bit) || (stuckMask & bit);
    lastReading[i]  = held;
    stableState[i]  = held && seen;
    lastChangeMs[i] = millis();
  }

  bootCount++;
  Serial.printf("Boot #%lu, woke by: ", (unsigned long)bootCount);
  printWakeReason();
  lastActivityMs = millis();

#if ENABLE_WIFI_SETUP
  // Fresh power-on or reset (not a wake from sleep): open the WiFi setup page if no network
  // is saved yet, or if the setup button is held. Never runs on a button wake, so it can't
  // delay an alert.
  if (powerOn && (savedNetworkCount() == 0 || setupButtonHeld())) {
    beepTimes(3);                                          // 3 blinks: setup mode
    if (runSetupPortal(SETUP_PORTAL_TIMEOUT_S)) beepTimes(2);
    else                                        beep(BEEP_LONG_MS);
    wifiOff();
    lastActivityMs = millis();
  }
#endif

  // Send every button that woke us, in priority order (BUTTONS[] order).
  if (woke) {
    for (uint8_t i = 0; i < NUM_BUTTONS; i++)
      if (woke & (1ULL << BUTTONS[i].pin)) handleAlert(BUTTONS[i]);
  } else if (wakeCause == ESP_SLEEP_WAKEUP_EXT0 || wakeCause == ESP_SLEEP_WAKEUP_EXT1) {
    Serial.println("Woke by button but could not tell which one");
  }
}

// ---------- Main loop: runs until goToSleep() ----------
void loop() {
  uint16_t presses = checkButtons();                   // all new presses this pass
  for (uint8_t i = 0; i < NUM_BUTTONS; i++)            // priority order
    if (presses & (1u << i)) handleAlert(BUTTONS[i]);

  if (millis() - lastActivityMs > AWAKE_WINDOW_MS || millis() > MAX_AWAKE_MS) goToSleep();
  delay(1);
}