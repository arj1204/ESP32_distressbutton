#include <Arduino.h>
#include "config.h"

#include "esp_sleep.h"
#include "driver/rtc_io.h"


// ---------- Debounce state: one entry per button ----------
bool          lastReading[NUM_BUTTONS];   // raw pin level from the previous pass
bool          stableState[NUM_BUTTONS];   // debounced level (HIGH = released, LOW = pressed)
unsigned long lastChangeMs[NUM_BUTTONS];  // when the raw level last changed



RTC_DATA_ATTR uint32_t bootCount = 0;     // survives deep sleep
esp_sleep_wakeup_cause_t wakeCause;
unsigned long lastActivityMs = 0;

void printWakeReason() {
  switch (wakeCause) {
    case ESP_SLEEP_WAKEUP_EXT0:  Serial.println("button (EXT0)"); break;
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("timer");         break;
    default:                     Serial.println("power-on / reset");
  }
}

void goToSleep() {
  // Don't sleep while the wake pin is still LOW, or we would wake instantly
  while (digitalRead(WAKE_PIN) == LOW) delay(10);

  Serial.println("Going to deep sleep...");
  Serial.flush();                          // let the text finish sending
  digitalWrite(BUZZER_PIN, LOW);

  rtc_gpio_pullup_en(WAKE_PIN);            // keep the pull-up alive during sleep
  rtc_gpio_pulldown_dis(WAKE_PIN);
  esp_sleep_enable_ext0_wakeup(WAKE_PIN, 0);       // wake when pin goes LOW
  esp_sleep_enable_timer_wakeup(SLEEP_TIMER_US);   // also wake on timer
  esp_deep_sleep_start();                  // never returns
}
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
    if (i < n - 1) delay(BEEP_GAP_MS);   // gap between beeps, none after the last
  }
}

// ---------- Buttons ----------
// Call every pass of loop(). Returns the index of a button that was just
// pressed, or -1 if none was.
int checkButtons() {
  int pressed = -1;

  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    bool reading = digitalRead(BUTTON_PINS[i]);

    // 1. Raw level changed since last pass? Restart the timer.
    if (reading != lastReading[i]) {
      lastChangeMs[i] = millis();
      lastReading[i]  = reading;
    }

    // 2. Level held steady longer than DEBOUNCE_MS and different from the
    //    accepted state? Accept it as the new stable state.
    if ((millis() - lastChangeMs[i]) > DEBOUNCE_MS && reading != stableState[i]) {
      stableState[i] = reading;

      // 3. HIGH -> LOW is a press (released = HIGH because of the pull-up).
      if (stableState[i] == LOW) {
        pressed = i;
      }
    }
  }
  return pressed;
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(500);                              // give the serial monitor time to connect

  Serial.printf("Chip: %s rev %d, %d cores\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());

  // Buzzer: output, off at start
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Buttons: inputs with internal pull-up (idle = HIGH, pressed = LOW)
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    lastReading[i]  = digitalRead(BUTTON_PINS[i]);
    stableState[i]  = lastReading[i];
    lastChangeMs[i] = millis();
  }

  Serial.printf("Ready: %d buttons\n", NUM_BUTTONS);
  

  // Wake reason and boot count
  wakeCause = esp_sleep_get_wakeup_cause();
  bootCount++;
  Serial.printf("Boot #%lu, woke by: ", (unsigned long)bootCount);
  printWakeReason();

  if (wakeCause == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("Button press detected (woke from sleep)");
    beep(BEEP_SHORT_MS);                   // timer wakes stay silent
  }
  lastActivityMs = millis();
}

// ---------- Main loop ----------
void loop() {
  int b = checkButtons();
  if (b >= 0) {
    Serial.printf("Button %d pressed\n", b);
    beep(BEEP_SHORT_MS);
    lastActivityMs = millis();
  }
  if (millis() - lastActivityMs > AWAKE_WINDOW_MS) goToSleep();
  delay(1);
}