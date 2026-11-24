#pragma once
#include <Arduino.h>
#include "driver/gpio.h"

// ============================================================
//  FEATURE FLAGS
// ============================================================
// 1 = WiFi networks (up to 5) are entered on a setup page from a phone and stored in flash.
//     Open it by holding SETUP_BUTTON_PIN while powering on. It also opens by itself
//     at power-on when no network has been saved yet.
// 0 = use WIFI_SSID / WIFI_PASS from secrets.h.
#define ENABLE_WIFI_SETUP 1

// 1 = find the server automatically on the local network (the server announces itself).
//     Falls back to an address typed on the setup page, then to SERVER_URL.
// 0 = always use SERVER_URL.
#define ENABLE_SERVER_DISCOVERY 1

// 1 = a server found automatically must prove it knows API_KEY before alerts go to it.
#define VERIFY_SERVER 1

// ============================================================
//  TEST FLAGS
// ============================================================
// 1 = BOOT button (GPIO 0) works as an extra TEST button (type 7).
// 0 = masked: ignored completely and not used for wake. Set 0 for the final build.
#define ENABLE_BOOT_BUTTON_TEST 1

// 1 = bench test with no external resistors: uses the chip's internal pull-downs
//     during sleep (draws a little more current).
// 0 = final board with the 1 MOhm pull-down resistors fitted.
#define BENCH_TEST 1

// 1 = also wake every SLEEP_TIMER_US (useful later for retrying unsent alerts).
// 0 = wake only on a button press. (A stuck button always uses the timer.)
#define ENABLE_TIMER_WAKEUP 0

// 1 = slow blink patterns, easy to watch on an LED.
// 0 = normal short beeps for the buzzer.
#define LED_TIMING 1

// ============================================================
//  BUTTONS
// ============================================================
// activeLow = false: GPIO to 3.3 V, 1 MOhm to GND (pressed = HIGH). The real buttons.
// activeLow = true : pressed = LOW (the BOOT button). Only ONE such entry is allowed.
// All pins must be RTC GPIOs (13, 14, 26, 27, 32, 33 are OK).
//
// ORDER = PRIORITY: when several are pressed together, they are sent top to bottom.
// TYPE numbers must match ALERT_TYPES in the server's main.py.
struct ButtonDef { uint8_t pin; uint8_t type; const char* name; bool activeLow; };

const ButtonDef BUTTONS[] = {
  {13, 1, "I need help",     false},
  {26, 3, "I'm overwhelmed", false},
  {33, 6, "I want to leave", false},
  {14, 2, "I need a break",  false},
  {27, 4, "I'm hungry",      false},
  {32, 5, "I'm okay",        false},
#if ENABLE_BOOT_BUTTON_TEST
  {0,  7, "TEST",            true},     // BOOT button, lowest priority
#endif
};
const uint8_t NUM_BUTTONS = sizeof(BUTTONS) / sizeof(BUTTONS[0]);

// ============================================================
//  OTHER PINS
// ============================================================
// Avoid: 1,3 (USB serial), 6-11 (flash), 16,17 (PSRAM), 2,5,12,15 (boot pins)
const uint8_t BUZZER_PIN  = 25;   // buzzer via NPN, or an LED + 330 Ohm. Use 2 for the onboard LED.
const uint8_t BATTERY_PIN = 35;   // ADC1, used later

// ============================================================
//  WIFI SETUP PAGE (ENABLE_WIFI_SETUP = 1)
// ============================================================
const uint8_t  SETUP_BUTTON_PIN       = 33;    // "I want to leave": hold it while powering on
const uint32_t SETUP_HOLD_MS          = 3000;  // how long it must be held
const uint16_t SETUP_PORTAL_TIMEOUT_S = 180;   // the setup page closes after 3 minutes

// ============================================================
//  SERVER
// ============================================================
const char* const DEVICE_ID  = "dev1";
// Last resort: used only if no server is found automatically and none was typed on the
// setup page for this network. Set to "" to rely on discovery and the setup page only.
const char* const SERVER_URL = "http://192.168.100.78:8000/api/alert";
const uint32_t    WIFI_TIMEOUT_MS = 15000;
const uint16_t    TEST_BATTERY_MV = 3900;   // placeholder until the battery divider exists

// ============================================================
//  TIMING (ms)
// ============================================================
const uint16_t DEBOUNCE_MS      = 40;     // button filtering, not feedback
const uint16_t STARTUP_DELAY_MS = 500;    // lets the serial monitor connect; set 0 in production

#if LED_TIMING
const uint16_t BEEP_SHORT_MS    = 300;
const uint16_t BEEP_LONG_MS     = 1500;
const uint16_t BEEP_GAP_MS      = 300;
#else
const uint16_t BEEP_SHORT_MS    = 100;
const uint16_t BEEP_LONG_MS     = 600;
const uint16_t BEEP_GAP_MS      = 120;
#endif

// ============================================================
//  SLEEP
// ============================================================
const uint32_t AWAKE_WINDOW_MS     = LED_TIMING ? 5000 : 3000;  // stay awake this long after the last activity
const uint32_t RELEASE_WAIT_MS     = 3000;    // max wait for buttons to be released before sleeping
const uint64_t SLEEP_TIMER_US      = 30ULL * 1000000ULL;   // timer wake interval (if enabled)
const uint32_t STUCK_RECHECK_MIN_S = 30;      // first re-check of a stuck button
const uint32_t STUCK_RECHECK_MAX_S = 600;     // longest gap (10 min) while it stays stuck
const uint32_t MAX_AWAKE_MS        = 60000;   // safety cap: never stay awake longer than 60 s
                                              // (the setup page is the one exception)


// ============================================================
//  WIFI RESET COMBO (ENABLE_WIFI_SETUP = 1)
// ============================================================
// Press BOTH of these buttons together (within COMBO_JOIN_MS) and hold:
//   5 s  -> 3 blinks. Let go: the setup page opens (add a network, or update a password).
//   10 s -> 1 long blink. ALL saved networks are erased, then the setup page opens.
// Released before 5 s = two ordinary presses, both alerts are sent.
// Use two low-priority buttons: their alerts wait while both are held.
const uint8_t  COMBO_PIN_A          = 27;      // "I'm hungry"
const uint8_t  COMBO_PIN_B          = 32;      // "I'm okay"
const uint32_t COMBO_JOIN_MS        = 500;     // max gap between pressing the two buttons
const uint32_t COMBO_SETUP_HOLD_MS  = 5000;    // hold this long for the setup page
const uint32_t COMBO_ERASE_HOLD_MS  = 10000;   // hold this long to erase all saved networks

// ============================================================
//  ALERTS
// ============================================================
// The same button again within this many seconds after a CONFIRMED send:
// one short beep, not sent again. 0 = send every press.
const uint32_t ALERT_COOLDOWN_S    = 30;