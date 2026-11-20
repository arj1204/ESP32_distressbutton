#pragma once
#include <Arduino.h>
#include "driver/gpio.h"

// ============================================================
//  TEST FLAGS
// ============================================================
// 1 = BOOT button (GPIO 0) works as an extra TEST button (type 7).
// 0 = masked: ignored completely and not used for wake. Set 0 for the final build.
#define ENABLE_BOOT_BUTTON_TEST 1

// 1 = bench test with no external resistors: uses the chip's internal pull-downs
//     during sleep (draws a little more current).
// 0 = final board with the 1 MOhm resistors fitted.
#define BENCH_TEST 1

// ============================================================
//  BUTTONS
// ============================================================
// activeLow = false: GPIO to 3.3 V, 1 MOhm to GND (pressed = HIGH). The real buttons.
// activeLow = true : pressed = LOW (the BOOT button). Only ONE such entry is allowed
//                    (hardware limit), and it must be an RTC GPIO.
// ORDER = PRIORITY: if two are pressed together, the first one in the list wins.
// All pins must be RTC GPIOs (13, 14, 26, 27, 32, 33 are OK).
struct ButtonDef { uint8_t pin; uint8_t type; const char* name; bool activeLow; };

const ButtonDef BUTTONS[] = {
  {13, 1, "MEDICAL",  false},
  {14, 2, "FIRE",     false},
  {26, 3, "SECURITY", false},
  {27, 4, "FALL",     false},
  {32, 5, "HELP",     false},
  {33, 6, "CALL_ME",  false},
#if ENABLE_BOOT_BUTTON_TEST
  {0,  7, "TEST",     true},     // BOOT button, lowest priority
#endif
};
const uint8_t NUM_BUTTONS = sizeof(BUTTONS) / sizeof(BUTTONS[0]);

// ============================================================
//  OTHER PINS
// ============================================================
// Avoid: 1,3 (USB serial), 6-11 (flash), 16,17 (PSRAM), 2,5,12,15 (boot pins)
const uint8_t BUZZER_PIN  = 25;
const uint8_t BATTERY_PIN = 35;   // ADC1, used later

// ============================================================
//  TIMING (ms)
// ============================================================
const uint16_t DEBOUNCE_MS      = 40;
const uint16_t BEEP_SHORT_MS    = 100;
const uint16_t BEEP_LONG_MS     = 600;
const uint16_t BEEP_GAP_MS      = 120;
const uint16_t STARTUP_DELAY_MS = 500;   // lets the serial monitor connect; set 0 in production

// ============================================================
//  SLEEP
// ============================================================
const uint32_t AWAKE_WINDOW_MS = 3000;   // stay awake this long after the last activity
const uint32_t RELEASE_WAIT_MS = 3000;   // max wait for buttons to be released before sleeping
const uint64_t SLEEP_TIMER_US  = 30ULL * 1000000ULL;   // TEST: timer wake every 30 s