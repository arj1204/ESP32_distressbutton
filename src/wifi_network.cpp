#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <time.h>
#include "config.h"
#include "secrets.h"
#include "wifi_network.h"

#if ENABLE_WIFI_SETUP
#include <WiFiMulti.h>
#include <WiFiManager.h>
#endif
#if ENABLE_SERVER_DISCOVERY
#include <ESPmDNS.h>
#endif
#if ENABLE_SERVER_DISCOVERY && VERIFY_SERVER
#include "mbedtls/md.h"
#endif

static const size_t URL_LEN = 101;   // longest server address we keep, plus 1

// Turns what someone typed ("192.168.1.5:8000", "http://host:8000/") into a full alert URL.
static void makeAlertUrl(const char* typed, char* out, size_t len) {
  const char* scheme = strstr(typed, "://") ? "" : "http://";
  const char* path   = strstr(typed, "/api/alert") ? "" : "/api/alert";
  size_t n = strlen(typed);
  if (n > 0 && typed[n - 1] == '/' && path[0]) n--;           // drop a trailing slash
  snprintf(out, len, "%s%.*s%s", scheme, (int)n, typed, path);
}

// ============================================================
//  SAVED WIFI NETWORKS AND SETUP PAGE  (ENABLE_WIFI_SETUP = 1)
//  Flash namespace "wifi": n = count, sN = network name, pN = password,
//  uN = server address typed for that network ("" = find it automatically)
// ============================================================
#if ENABLE_WIFI_SETUP
static const uint8_t MAX_NETWORKS = 5;

// Builds a flash key such as "s0", "p0" or "u0".
static void makeKey(char* out, char prefix, uint8_t i) {
  snprintf(out, 4, "%c%u", prefix, (unsigned)i);
}

uint8_t savedNetworkCount() {
  Preferences p;
  p.begin("wifi", true);
  uint8_t n = p.getUChar("n", 0);
  p.end();
  return n;
}

// Saves a network. Same name again: updates it. List full: drops the oldest.
static void saveNetwork(const String& ssid, const String& pass, const String& server) {
  char ks[4], kp[4], ku[4];
  Preferences p;
  p.begin("wifi", false);
  uint8_t n = p.getUChar("n", 0);

  for (uint8_t i = 0; i < n; i++) {                  // already saved? update it
    makeKey(ks, 's', i);
    if (p.getString(ks, "") == ssid) {
      makeKey(kp, 'p', i);  p.putString(kp, pass);
      makeKey(ku, 'u', i);  p.putString(ku, server);
      p.end();
      return;
    }
  }

  if (n >= MAX_NETWORKS) {                           // full: shift down, losing slot 0
    char k2[4];
    for (uint8_t i = 1; i < MAX_NETWORKS; i++) {
      const char prefixes[] = {'s', 'p', 'u'};
      for (char c : prefixes) {
        makeKey(ks, c, i - 1);
        makeKey(k2, c, i);
        p.putString(ks, p.getString(k2, ""));
      }
    }
    n = MAX_NETWORKS - 1;
  }

  makeKey(ks, 's', n);  p.putString(ks, ssid);
  makeKey(kp, 'p', n);  p.putString(kp, pass);
  makeKey(ku, 'u', n);  p.putString(ku, server);
  p.putUChar("n", n + 1);
  p.end();
}

// Server address typed on the setup page for this network ("" if none).
static String typedServerFor(const String& ssid) {
  char ks[4], ku[4];
  String server;
  Preferences p;
  p.begin("wifi", true);
  uint8_t n = p.getUChar("n", 0);
  for (uint8_t i = 0; i < n; i++) {
    makeKey(ks, 's', i);
    if (p.getString(ks, "") == ssid) {
      makeKey(ku, 'u', i);
      server = p.getString(ku, "");
      break;
    }
  }
  p.end();
  return server;
}

// Opens the device's own WiFi network and a setup page. Blocks until saved or timed out.
bool runSetupPortal(uint16_t timeoutSec) {
  char apName[40];
  snprintf(apName, sizeof(apName), "Distress-Setup-%s", DEVICE_ID);
  char infoHtml[120];
  snprintf(infoHtml, sizeof(infoHtml), "<p>Device ID: <b>%s</b></p>", DEVICE_ID);

  WiFiManager wm;
  WiFiManagerParameter info(infoHtml);
  WiFiManagerParameter serverField("server",
      "Server address, e.g. 192.168.1.5:8000 (leave empty to find it automatically)",
      "", URL_LEN - 1);
  wm.addParameter(&info);
  wm.addParameter(&serverField);
  wm.setConfigPortalTimeout(timeoutSec);

  Serial.printf("Setup page open: join WiFi \"%s\", then open http://192.168.4.1\n", apName);
  bool ok = wm.startConfigPortal(apName, SETUP_AP_PASS);

  if (ok) {
    saveNetwork(WiFi.SSID(), WiFi.psk(), String(serverField.getValue()));
    Serial.printf("Setup: saved \"%s\" (%u of %u slots used), server: %s\n",
                  WiFi.SSID().c_str(), (unsigned)savedNetworkCount(), (unsigned)MAX_NETWORKS,
                  strlen(serverField.getValue()) ? serverField.getValue() : "find automatically");
  } else {
    Serial.println("Setup page closed without saving");
  }
  return ok;
}
#endif  // ENABLE_WIFI_SETUP

// ============================================================
//  WIFI
// ============================================================
bool connectWiFi(uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;   // already connected earlier in this wake

  uint32_t t0 = millis();
#if ENABLE_WIFI_SETUP
  WiFiMulti multi;                                  // tries every saved network, joins the strongest
  char ks[4], kp[4];
  Preferences p;
  p.begin("wifi", true);
  uint8_t n = p.getUChar("n", 0);
  for (uint8_t i = 0; i < n; i++) {
    makeKey(ks, 's', i);
    makeKey(kp, 'p', i);
    String s  = p.getString(ks, "");
    String pw = p.getString(kp, "");
    multi.addAP(s.c_str(), pw.c_str());
  }
  p.end();
  if (n == 0) {
    Serial.println("WiFi: no saved networks. Hold the setup button while powering on to add one.");
    return false;
  }
  WiFi.mode(WIFI_STA);
  while (multi.run(5000) != WL_CONNECTED && millis() - t0 < timeoutMs) delay(100);
#else
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(100);
#endif

  bool ok = (WiFi.status() == WL_CONNECTED);
  if (ok) Serial.printf("WiFi: connected to %s | ESP32 IP: %s\n",
                        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  else    Serial.println("WiFi: FAILED");
  return ok;
}

void wifiOff() {
  if (WiFi.getMode() == WIFI_OFF) return;           // never turned on during this wake
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// ============================================================
//  FINDING THE SERVER AUTOMATICALLY  (ENABLE_SERVER_DISCOVERY = 1)
//  The server announces itself as "_distress._tcp" on the local network (mDNS).
// ============================================================
#if ENABLE_SERVER_DISCOVERY
// Remembered between wakes (lost on power-off), so we don't search before every alert.
RTC_DATA_ATTR static char cachedSsid[33]     = "";
RTC_DATA_ATTR static char cachedUrl[URL_LEN] = "";

#if VERIFY_SERVER
// HMAC-SHA256 of msg using key, as 64 lowercase hex characters.
static void hmacHex(const char* key, const char* msg, char out[65]) {
  uint8_t mac[32];
  const mbedtls_md_info_t* sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(sha256, (const unsigned char*)key, strlen(key),
                  (const unsigned char*)msg, strlen(msg), mac);
  for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", mac[i]);
  out[64] = '\0';
}

// Asks a found server to prove it knows API_KEY: we send a random number, it must reply
// with HMAC(API_KEY, number). A server without the key can't answer correctly.
static bool verifyServer(const char* base) {
  char nonce[17];
  snprintf(nonce, sizeof(nonce), "%08lx%08lx",
           (unsigned long)esp_random(), (unsigned long)esp_random());
  char url[URL_LEN + 40];
  snprintf(url, sizeof(url), "%s/api/hello?nonce=%s", base, nonce);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(4000);
  int code = http.GET();
  String body = (code == 200) ? http.getString() : String();
  http.end();
  if (code != 200) {
    Serial.printf("Discovery: %s did not answer the key check (%d)\n", base, code);
    return false;
  }

  char expected[65];
  hmacHex(API_KEY, nonce, expected);
  const char* p = strstr(body.c_str(), "\"proof\"");     // reply looks like {"proof":"<64 hex>"}
  if (p) p = strchr(p + 7, ':');
  if (p) p = strchr(p, '"');
  bool ok = (p != nullptr) && strncmp(p + 1, expected, 64) == 0;
  Serial.printf("Discovery: %s %s\n", base, ok ? "passed the key check" : "FAILED the key check, ignored");
  return ok;
}
#endif  // VERIFY_SERVER

// Looks for a distress server on this network. Writes its alert URL to out.
static bool discoverServer(char* out, size_t len) {
  if (!MDNS.begin(DEVICE_ID)) {
    Serial.println("Discovery: could not start mDNS");
    return false;
  }
  Serial.println("Discovery: looking for a distress server on this network...");
  int n = MDNS.queryService("distress", "tcp");          // the server announces _distress._tcp
  bool found = false;
  for (int i = 0; i < n && !found; i++) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    IPAddress ip = MDNS.address(i);
#else
    IPAddress ip = MDNS.IP(i);
#endif
    char base[48];
    snprintf(base, sizeof(base), "http://%s:%u", ip.toString().c_str(), (unsigned)MDNS.port(i));
#if VERIFY_SERVER
    if (!verifyServer(base)) continue;
#endif
    snprintf(out, len, "%s/api/alert", base);
    found = true;
  }
  MDNS.end();
  if (found) Serial.printf("Discovery: using %s\n", out);
  else       Serial.println("Discovery: no server found");
  return found;
}
#endif  // ENABLE_SERVER_DISCOVERY

// ============================================================
//  WHERE TO SEND
//  Order: address typed on the setup page for this network
//       > remembered discovery for this network > new discovery > SERVER_URL
// ============================================================
static bool findServer(char* out, size_t len, bool searchAgain, bool* discovered) {
  *discovered = false;
  String ssid = WiFi.SSID();
  (void)ssid;
  (void)searchAgain;

#if ENABLE_WIFI_SETUP
  String typed = typedServerFor(ssid);
  if (typed.length() > 0) {
    makeAlertUrl(typed.c_str(), out, len);
    return true;
  }
#endif

#if ENABLE_SERVER_DISCOVERY
  if (!searchAgain && cachedUrl[0] && strcmp(cachedSsid, ssid.c_str()) == 0) {
    snprintf(out, len, "%s", cachedUrl);
    *discovered = true;
    return true;
  }
  if (discoverServer(out, len)) {
    snprintf(cachedSsid, sizeof(cachedSsid), "%s", ssid.c_str());
    snprintf(cachedUrl, sizeof(cachedUrl), "%s", out);
    *discovered = true;
    return true;
  }
  cachedUrl[0] = '\0';
#endif

  if (SERVER_URL[0]) {
    snprintf(out, len, "%s", SERVER_URL);
    return true;
  }
  return false;
}

// ============================================================
//  ERASE ALL SAVED NETWORKS  (two-button reset combo)
// ============================================================
#if ENABLE_WIFI_SETUP
void forgetAllNetworks() {
  Preferences p;
  p.begin("wifi", false);
  p.clear();                        // removes every saved network and typed server address
  p.end();
#if ENABLE_SERVER_DISCOVERY
  cachedSsid[0] = '\0';             // forget the remembered server too
  cachedUrl[0]  = '\0';
#endif
}
#endif

// ============================================================
//  SENDING
// ============================================================
static int postAlert(const char* url, uint32_t eventId, uint8_t type, uint16_t batteryMv) {
  char body[200];
  snprintf(body, sizeof(body),
    "{\"device_id\":\"%s\",\"event_id\":\"%lu\",\"ts\":%lu,"
    "\"battery_mv\":%u,\"rssi\":%d,\"type\":%u}",
    DEVICE_ID, (unsigned long)eventId, (unsigned long)time(nullptr),
    (unsigned)batteryMv, (int)WiFi.RSSI(), (unsigned)type);
  Serial.printf("Sending to %s: %s\n", url, body);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", API_KEY);
  int code = http.POST(body);
  if (code < 0) Serial.printf("POST error: %s\n", HTTPClient::errorToString(code).c_str());
  else          Serial.printf("Server replied: %d\n", code);
  http.end();
  return code;
}

int sendAlert(uint32_t eventId, uint8_t type, uint16_t batteryMv) {
  if (!connectWiFi(WIFI_TIMEOUT_MS)) return -100;                // no WiFi

  char url[URL_LEN];
  bool discovered = false;
  if (!findServer(url, sizeof(url), false, &discovered)) {
    Serial.println("No server address: none found, none typed on the setup page, SERVER_URL empty");
    return -101;
  }

  int code = postAlert(url, eventId, type, batteryMv);

#if ENABLE_SERVER_DISCOVERY
  // A remembered/discovered server didn't answer: it may have restarted with a new address.
  if (code < 0 && discovered) {
    Serial.println("Send failed: looking for the server again");
    if (findServer(url, sizeof(url), true, &discovered))
      code = postAlert(url, eventId, type, batteryMv);
  }
#endif
  return code;
}