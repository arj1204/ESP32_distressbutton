#pragma once
#include <Arduino.h>

// Join WiFi (or reuse the connection if already connected). True if connected within timeoutMs.
bool connectWiFi(uint32_t timeoutMs);

// Turn the WiFi radio off (saves power). Safe to call even if WiFi was never started.
void wifiOff();

// Join WiFi, work out where the server is, and send one alert.
// Returns the HTTP status code (200 = server has it), or a negative number on error.
int sendAlert(uint32_t eventId, uint8_t type, uint16_t batteryMv);

// Only used when ENABLE_WIFI_SETUP = 1:
uint8_t savedNetworkCount();               // WiFi networks saved in flash (0 to 5)
bool    runSetupPortal(uint16_t timeoutSec);  // open the setup page; true if a network was saved

void    forgetAllNetworks();               // erase every saved network and typed server address
