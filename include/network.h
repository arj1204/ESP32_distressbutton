#pragma once
#include <Arduino.h>

// Join WiFi. Returns true if connected within timeoutMs.
bool connectWiFi(uint32_t timeoutMs);

// Turn the WiFi radio off (saves power).
void wifiOff();

// Send one alert to the server.
// Returns the HTTP status code (200 = server has it), or a negative number on error.
int postAlert(uint32_t eventId, uint8_t type, uint16_t batteryMv);