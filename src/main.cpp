#include <Arduino.h>
#include <Preferences.h>
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "config.h"
#include "network.h"

// ---------- Debounce state: one entry per button ----------
bool          lastReading[NUM_BUTTONS];   // raw "is pressed" from the previous pass
bool          stableState[NUM_BUTTONS];   // debounced "is pressed"
unsigned long lastChangeMs[NUM_BUTTONS];  // when the raw state last changed

// ---------- Sleep state ----------
RTC_DATA_ATTR uint32_t bootCount = 0;     // survives deep sleep (not power loss)
esp_sleep_wakeup_cause_t wakeCause;
unsigned long lastActivityMs = 0;

// ---------- Buzzer ----------
// HIGH switches the NPN transistor on, which sounds the buzzer.
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

// Call every pass of loop(). Returns the index (in BUTTONS) of a button that
// was just pressed, or -1 if none was.
int checkButtons() {
  int pressed = -1;

  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    bool reading = isPressed(i);

    // 1. Raw state changed? Restart the timer.
    if (reading != lastReading[i]) {
      lastChangeMs[i] = millis();
      lastReading[i]  = reading;
    }

    // 2. Steady longer than DEBOUNCE_MS and different from accepted state?
    if ((millis() - lastChangeMs[i]) > DEBOUNCE_MS && reading != stableState[i]) {
      stableState[i] = reading;

      // 3. Newly pressed. The first one found wins, so list order = priority.
      if (stableState[i] && pressed < 0) pressed = i;
    }
  }
  return pressed;
}

// ---------- Event number ----------
// Saved in flash so it never repeats, even after a power loss.
// (The server ignores a repeated event_id, so a repeat would hide a real alert.)
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
  Serial.printf("ALERT: %s (type %u)\n", b.name, b.type);
  beep(BEEP_SHORT_MS);                                // 1 beep: button registered

  uint32_t seq = nextEventSeq();
  if (connectWiFi(WIFI_TIMEOUT_MS) && postAlert(seq, b.type, TEST_BATTERY_MV) == 200) {
    beepTimes(2);                                     // 2 beeps: server confirmed
  } else {
    beep(BEEP_LONG_MS);                               // 1 long beep: failed
  }

  wifiOff();
  lastActivityMs = millis();
}

// ---------- Sleep ----------
void printWakeReason() {
  switch (wakeCause) {
    case ESP_SLEEP_WAKEUP_EXT0:  Serial.println("test button (EXT0)"); break;
    case ESP_SLEEP_WAKEUP_EXT1:  Serial.println("button (EXT1)");      break;
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("timer");              break;
    default:                     Serial.println("power-on / reset");
  }
}

// Which button woke us? The hardware remembers, even after a quick tap.
// Returns an index in BUTTONS, or -1 if unknown.
int wakeButtonIndex() {
  if (wakeCause == ESP_SLEEP_WAKEUP_EXT1) {
    uint64_t status = esp_sleep_get_ext1_wakeup_status();   // bit n set = GPIO n
    for (uint8_t i = 0; i < NUM_BUTTONS; i++)               // list order = priority
      if (!BUTTONS[i].activeLow && (status & (1ULL << BUTTONS[i].pin))) return i;
  } else if (wakeCause == ESP_SLEEP_WAKEUP_EXT0) {
    for (uint8_t i = 0; i < NUM_BUTTONS; i++)               // the active-LOW test button
      if (BUTTONS[i].activeLow) return i;
  }
  return -1;
}

void goToSleep() {
  // Wait for buttons to be released, or we would wake instantly.
  unsigned long t0 = millis();
  while (anyButtonHeld() && (millis() - t0) < RELEASE_WAIT_MS) delay(10);
  bool stuck = anyButtonHeld();   // still held after the wait: probably jammed

  Serial.println("Going to deep sleep...");
  Serial.flush();
  digitalWrite(BUZZER_PIN, LOW);

  if (!stuck) {
    uint64_t wakeMask = 0;        // active-HIGH buttons, one bit per GPIO
    int lowPin = -1;              // the active-LOW test button, if enabled

    for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
      if (BUTTONS[i].activeLow) lowPin = BUTTONS[i].pin;
      else                      wakeMask |= 1ULL << BUTTONS[i].pin;
    }

#if BENCH_TEST || ENABLE_BOOT_BUTTON_TEST
    // Keep the RTC pull resistors alive during sleep
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
#endif
#if BENCH_TEST
    // No external resistors: use the chip's pull-downs on the active-HIGH pins
    for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
      if (BUTTONS[i].activeLow) continue;
      rtc_gpio_pullup_dis((gpio_num_t)BUTTONS[i].pin);
      rtc_gpio_pulldown_en((gpio_num_t)BUTTONS[i].pin);
    }
#endif
    if (lowPin >= 0) {
      rtc_gpio_pullup_en((gpio_num_t)lowPin);               // idle HIGH
      rtc_gpio_pulldown_dis((gpio_num_t)lowPin);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)lowPin, 0);  // wake when it goes LOW
    }
    if (wakeMask) esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_HIGH);
  }
  // If a button is stuck, skip button wake (it would loop) and use the timer only.

  esp_sleep_enable_timer_wakeup(SLEEP_TIMER_US);
  esp_deep_sleep_start();         // never returns
}

// ---------- Setup ----------
void setup() {
  setCpuFrequencyMhz(80);                  // saves power, still enough for WiFi
  Serial.begin(115200);
  delay(STARTUP_DELAY_MS);

  Serial.printf("Chip: %s rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("Test (BOOT) button: %s\n", ENABLE_BOOT_BUTTON_TEST ? "ENABLED" : "masked");

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    pinMode(BUTTONS[i].pin, BUTTONS[i].activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
    lastReading[i]  = isPressed(i);
    stableState[i]  = lastReading[i];      // a button still held from the wake is not a new press
    lastChangeMs[i] = millis();
  }

  wakeCause = esp_sleep_get_wakeup_cause();
  bootCount++;
  Serial.printf("Boot #%lu, woke by: ", (unsigned long)bootCount);
  printWakeReason();
  lastActivityMs = millis();

  if (wakeCause == ESP_SLEEP_WAKEUP_EXT0 || wakeCause == ESP_SLEEP_WAKEUP_EXT1) {
    int w = wakeButtonIndex();
    if (w >= 0) handleAlert(BUTTONS[w]);
    else Serial.println("Woke by button but could not tell which one");
  }
}

// ---------- Main loop ----------
void loop() {
  int b = checkButtons();                  // extra presses while awake
  if (b >= 0) handleAlert(BUTTONS[b]);

  if (millis() - lastActivityMs > AWAKE_WINDOW_MS) goToSleep();
  delay(1);
}