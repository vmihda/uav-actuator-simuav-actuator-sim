#pragma once
#include <string>

constexpr int WIFI_AP = 2;
struct WiFiClass {
  bool mode(int) { return ready; }
  bool softAP(const char* name, const char* password) {
    ssid = name; key = password; ++softApCalls; return ready;
  }
  int getMode() const { return ready ? WIFI_AP : 0; }
  bool ready = true;
  int softApCalls = 0;
  const char* ssid = "";
  const char* key = "";
};
extern WiFiClass WiFi;
