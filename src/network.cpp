#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "config.h"
#include "secrets.h"
#include "network.h"

bool connectWiFi(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(100);
  Serial.printf("WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "connected" : "FAILED");
  return WiFi.status() == WL_CONNECTED;
}

void wifiOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

int postAlert(uint32_t eventId, uint8_t type, uint16_t batteryMv) {
  char body[200];
  snprintf(body, sizeof(body),
    "{\"device_id\":\"%s\",\"event_id\":\"%lu\",\"ts\":%lu,"
    "\"battery_mv\":%u,\"rssi\":%d,\"type\":%u}",
    DEVICE_ID, (unsigned long)eventId, (unsigned long)time(nullptr),
    (unsigned)batteryMv, (int)WiFi.RSSI(), (unsigned)type);
  Serial.printf("Sending: %s\n", body);

  HTTPClient http;
  http.begin(SERVER_URL);
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", API_KEY);
  int code = http.POST(body);
  if (code < 0) Serial.printf("POST error: %s\n", HTTPClient::errorToString(code).c_str());
  else          Serial.printf("Server replied: %d\n", code);
  http.end();
  return code;
}