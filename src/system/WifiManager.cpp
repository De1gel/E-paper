#include "system/WifiManager.h"

#include <WiFi.h>

namespace appfw {
namespace {
constexpr const char *kApSsid = "PhotoFrame_Config";
constexpr const char *kApPass = "12345678";
constexpr const char *kStaSsid = "YOUR_WIFI_SSID";
constexpr const char *kStaPass = "YOUR_WIFI_PASSWORD";
}

void WifiManager::begin() {
  WiFi.mode(WIFI_OFF);
  state_ = State::Idle;
}

void WifiManager::update(uint32_t now_ms) {
  if (state_ == State::StaConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      state_ = State::StaConnected;
      Serial.printf("[WIFI] STA connected, ip=%s\n", WiFi.localIP().toString().c_str());
    } else if ((now_ms - sta_start_ms_) >= kStaConnectTimeoutMs) {
      Serial.println("[WIFI] STA connect timeout");
      state_ = State::Idle;
      WiFi.disconnect(true, true);
      WiFi.mode(WIFI_OFF);
    }
  }
}

void WifiManager::startAP() {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP);
  const bool ok = WiFi.softAP(kApSsid, kApPass);
  state_ = ok ? State::ApRunning : State::Idle;
  if (ok) {
    Serial.printf("[WIFI] AP started ssid=%s ip=%s\n", kApSsid,
                  WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("[WIFI] AP start failed");
  }
}

void WifiManager::startSTA() {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(kStaSsid, kStaPass);
  sta_start_ms_ = millis();
  state_ = State::StaConnecting;
  Serial.printf("[WIFI] STA connecting ssid=%s\n", kStaSsid);
}

}  // namespace appfw
