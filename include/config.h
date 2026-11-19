#pragma once
#include <Arduino.h>

// ============================================================
//  TEST MODE
//  Uncomment the next line to test with just the dev board:
//  uses the BOOT button (GPIO 0) as the only button.
//  Comment it out again once the real buttons are wired.
// ============================================================
#define TEST_BOOT_BUTTON_ONLY

// ---------- Pins ----------
// Classic ESP32 (WROVER-E). Avoid these pins:
//   GPIO 1, 3      USB serial (UART0)
//   GPIO 6 to 11   internal flash
//   GPIO 16, 17    PSRAM
//   GPIO 0,2,5,12,15  strapping pins, risky at boot (GPIO 0 is OK for the BOOT button)

#ifdef TEST_BOOT_BUTTON_ONLY
  const uint8_t BUTTON_PINS[] = {0};
#else
  const uint8_t BUTTON_PINS[] = {13, 14, 26, 27, 32, 33};
#endif

const uint8_t BUZZER_PIN  = 25;
const uint8_t BATTERY_PIN = 35;   // ADC1 (works with WiFi on), used later

const uint8_t NUM_BUTTONS = sizeof(BUTTON_PINS) / sizeof(BUTTON_PINS[0]);

// ---------- Timing (ms) ----------
const uint16_t DEBOUNCE_MS   = 40;
const uint16_t BEEP_SHORT_MS = 100;
const uint16_t BEEP_LONG_MS  = 600;
const uint16_t BEEP_GAP_MS   = 120;