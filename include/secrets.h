#pragma once
// Never commit or share this file.

// Used only when ENABLE_WIFI_SETUP = 0 (otherwise networks are entered on the setup page).
const char* const WIFI_SSID = <your-wifi-name>;      // 2.4 GHz network
const char* const WIFI_PASS =<your-password>;  // network password

// Must match DEVICE_API_KEY in the server's .env
const char* const API_KEY   = "whJqLiEIerY9tGtuVkJeSAEef56GjJu1";

// Password for the device's own setup network ("Distress-Setup-<DEVICE_ID>").
// At least 8 characters, or the setup network won't start.
const char* const SETUP_AP_PASS = "change-me-123";
