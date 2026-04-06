#include "system/WifiManager.h"

#include <FS.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>
#include <esp_adc_cal.h>
#include <esp_wifi.h>

namespace appfw {
namespace {
constexpr const char *kDefaultApSsid = "PhotoFrame_Config";
constexpr const char *kDefaultApPass = "12345678";
constexpr const char *kDefaultStaSsid = "DESKTOP-09PTMRM 4607";
constexpr const char *kDefaultStaPass = "67O9b1-2";
constexpr const char *kDefaultUiLanguage = "zh";
constexpr const char *kDefaultTimezone = "Asia/Shanghai";
constexpr const char *kDefaultHostname = "epaper";
constexpr const char *kDefaultCalendarUrl = "";
constexpr const char *kDefaultWeatherCity = "Shanghai";
constexpr const char *kDefaultWeatherLat = "31.2304";
constexpr const char *kDefaultWeatherLon = "121.4737";
constexpr const char *kDefaultWeatherUrl =
    "https://api.open-meteo.com/v1/forecast?latitude=31.2304&longitude=121.4737&current=temperature_2m,relative_humidity_2m,weather_code&timezone=auto";
constexpr const char *kPortalHtmlPath = "/portal.html";
constexpr uint16_t kHttpPort = 80;
constexpr uint8_t kI2cSdaPin = 21;
constexpr uint8_t kI2cSclPin = 22;
constexpr uint8_t kAht20Address = 0x38;

String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i) {
    const char c = s[i];
    if (c == '\\') {
      out += "\\\\";
    } else if (c == '"') {
      out += "\\\"";
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else {
      out += c;
    }
  }
  return out;
}

String urlEncode(const String &s) {
  static const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(s[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

int hexToInt(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

String urlDecode(const String &s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    const char c = s[i];
    if (c == '%' && (i + 2) < s.length()) {
      const int hi = hexToInt(s[i + 1]);
      const int lo = hexToInt(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out += static_cast<char>((hi << 4) | lo);
        i += 2;
        continue;
      }
    }
    if (c == '+') {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

bool isLeapYear(int year) {
  return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  if (month < 1 || month > 12) {
    return 30;
  }
  return kDays[month - 1];
}

String extractJsonStringField(const String &json, const char *key) {
  if (key == nullptr || key[0] == '\0') {
    return "";
  }
  const String token = String("\"") + key + "\":\"";
  const int start = json.indexOf(token);
  if (start < 0) {
    return "";
  }

  String out;
  out.reserve(48);
  bool escaping = false;
  for (int i = start + static_cast<int>(token.length()); i < static_cast<int>(json.length()); ++i) {
    const char c = json[i];
    if (escaping) {
      switch (c) {
        case '\"':
          out += '\"';
          break;
        case '\\':
          out += '\\';
          break;
        case '/':
          out += '/';
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        default:
          out += c;
          break;
      }
      escaping = false;
      continue;
    }
    if (c == '\\') {
      escaping = true;
      continue;
    }
    if (c == '"') {
      break;
    }
    out += c;
  }
  out.trim();
  return out;
}

bool syncClockWithTimezone(const String &timezone, String &local_time, String &error_msg) {
  local_time = "";
  error_msg = "";
  if (timezone.length() == 0) {
    error_msg = "empty_timezone";
    return false;
  }

  configTzTime(timezone.c_str(), "ntp.aliyun.com", "time.cloudflare.com", "pool.ntp.org");
  constexpr time_t kMinValidEpoch = 1700000000;  // About 2023-11.
  const uint32_t start_ms = millis();
  while ((millis() - start_ms) < 10000) {
    const time_t now_ts = time(nullptr);
    if (now_ts >= kMinValidEpoch) {
      struct tm tm_local {};
      if (localtime_r(&now_ts, &tm_local) == nullptr) {
        error_msg = "localtime_failed";
        return false;
      }
      char buf[40] = {0};
      if (strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %z", &tm_local) > 0) {
        local_time = String(buf);
      } else {
        local_time = String(static_cast<unsigned long>(now_ts));
      }
      return true;
    }
    delay(200);
  }
  error_msg = "ntp_timeout";
  return false;
}

}  // namespace

void WifiManager::registerWifiEvents() {
  if (wifi_events_registered_) {
    return;
  }
  WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
    handleWifiEvent(event, info);
  });
  wifi_events_registered_ = true;
}

void WifiManager::handleWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("[WIFI][EVT] STA_START");
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
      Serial.println("[WIFI][EVT] STA_STOP");
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.printf("[WIFI][EVT] STA_CONNECTED ssid=%s channel=%d auth=%d\n",
                    reinterpret_cast<const char *>(info.wifi_sta_connected.ssid),
                    static_cast<int>(info.wifi_sta_connected.channel),
                    static_cast<int>(info.wifi_sta_connected.authmode));
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.printf("[WIFI][EVT] STA_DISCONNECTED reason=%u (%s) ssid=%s\n",
                    static_cast<unsigned>(info.wifi_sta_disconnected.reason),
                    disconnectReasonName(info.wifi_sta_disconnected.reason),
                    reinterpret_cast<const char *>(info.wifi_sta_disconnected.ssid));
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[WIFI][EVT] STA_GOT_IP ip=%s gw=%s mask=%s\n",
                    IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str(),
                    IPAddress(info.got_ip.ip_info.gw.addr).toString().c_str(),
                    IPAddress(info.got_ip.ip_info.netmask.addr).toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      Serial.println("[WIFI][EVT] STA_LOST_IP");
      break;
    default:
      Serial.printf("[WIFI][EVT] %s (%d)\n", wifiEventName(event), static_cast<int>(event));
      break;
  }
}

const char *WifiManager::wifiStatusName(int status) const {
  switch (status) {
    case WL_IDLE_STATUS:
      return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:
      return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:
      return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:
      return "WL_CONNECTED";
    case WL_CONNECT_FAILED:
      return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST:
      return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:
      return "WL_DISCONNECTED";
    default:
      return "WL_UNKNOWN";
  }
}

const char *WifiManager::wifiEventName(arduino_event_id_t event) const {
  switch (event) {
    case ARDUINO_EVENT_WIFI_READY:
      return "WIFI_READY";
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
      return "WIFI_SCAN_DONE";
    case ARDUINO_EVENT_WIFI_STA_START:
      return "WIFI_STA_START";
    case ARDUINO_EVENT_WIFI_STA_STOP:
      return "WIFI_STA_STOP";
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      return "WIFI_STA_CONNECTED";
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      return "WIFI_STA_DISCONNECTED";
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      return "WIFI_STA_GOT_IP";
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      return "WIFI_STA_LOST_IP";
    case ARDUINO_EVENT_WIFI_AP_START:
      return "WIFI_AP_START";
    case ARDUINO_EVENT_WIFI_AP_STOP:
      return "WIFI_AP_STOP";
    default:
      return "WIFI_EVENT_UNKNOWN";
  }
}

const char *WifiManager::disconnectReasonName(uint8_t reason) const {
  switch (reason) {
    case WIFI_REASON_UNSPECIFIED:
      return "UNSPECIFIED";
    case WIFI_REASON_AUTH_EXPIRE:
      return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_LEAVE:
      return "AUTH_LEAVE";
    case WIFI_REASON_ASSOC_EXPIRE:
      return "ASSOC_EXPIRE";
    case WIFI_REASON_ASSOC_TOOMANY:
      return "ASSOC_TOOMANY";
    case WIFI_REASON_NOT_AUTHED:
      return "NOT_AUTHED";
    case WIFI_REASON_NOT_ASSOCED:
      return "NOT_ASSOCED";
    case WIFI_REASON_ASSOC_LEAVE:
      return "ASSOC_LEAVE";
    case WIFI_REASON_ASSOC_NOT_AUTHED:
      return "ASSOC_NOT_AUTHED";
    case WIFI_REASON_DISASSOC_PWRCAP_BAD:
      return "DISASSOC_PWRCAP_BAD";
    case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
      return "DISASSOC_SUPCHAN_BAD";
    case WIFI_REASON_IE_INVALID:
      return "IE_INVALID";
    case WIFI_REASON_MIC_FAILURE:
      return "MIC_FAILURE";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      return "4WAY_HANDSHAKE_TIMEOUT";
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
      return "GROUP_KEY_UPDATE_TIMEOUT";
    case WIFI_REASON_IE_IN_4WAY_DIFFERS:
      return "IE_IN_4WAY_DIFFERS";
    case WIFI_REASON_GROUP_CIPHER_INVALID:
      return "GROUP_CIPHER_INVALID";
    case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
      return "PAIRWISE_CIPHER_INVALID";
    case WIFI_REASON_AKMP_INVALID:
      return "AKMP_INVALID";
    case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
      return "UNSUPP_RSN_IE_VERSION";
    case WIFI_REASON_INVALID_RSN_IE_CAP:
      return "INVALID_RSN_IE_CAP";
    case WIFI_REASON_802_1X_AUTH_FAILED:
      return "8021X_AUTH_FAILED";
    case WIFI_REASON_CIPHER_SUITE_REJECTED:
      return "CIPHER_SUITE_REJECTED";
    case WIFI_REASON_BEACON_TIMEOUT:
      return "BEACON_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND:
      return "NO_AP_FOUND";
    case WIFI_REASON_AUTH_FAIL:
      return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_FAIL:
      return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "HANDSHAKE_TIMEOUT";
    default:
      return "UNKNOWN_REASON";
  }
}

void WifiManager::logStaScanResults() {
  Serial.println("[WIFI] STA scan begin");
  WiFi.disconnect(false, false);
  delay(80);
  const int count = WiFi.scanNetworks(false, true, false, 400, 0);
  if (count < 0) {
    Serial.printf("[WIFI] STA scan failed code=%d\n", count);
    return;
  }
  Serial.printf("[WIFI] STA scan found=%d\n", count);
  bool matched = false;
  for (int i = 0; i < count; ++i) {
    const String ssid = WiFi.SSID(i);
    const int32_t rssi = WiFi.RSSI(i);
    const wifi_auth_mode_t enc = WiFi.encryptionType(i);
    const bool is_target = (ssid == settings_.sta_ssid);
    if (is_target) {
      matched = true;
    }
    Serial.printf("[WIFI]   %c #%d ssid=%s rssi=%ld ch=%d enc=%d\n",
                  is_target ? '*' : '-',
                  i + 1,
                  ssid.c_str(),
                  static_cast<long>(rssi),
                  WiFi.channel(i),
                  static_cast<int>(enc));
  }
  if (!matched) {
    Serial.printf("[WIFI] target ssid not seen in scan: %s\n", settings_.sta_ssid.c_str());
  }
  WiFi.scanDelete();
}

void WifiManager::begin() {
  pinMode(kPeripheralPowerPin, OUTPUT);
  digitalWrite(kPeripheralPowerPin, LOW);
  registerWifiEvents();
  initSensors();
  loadSettings();
  stop("boot");
}

void WifiManager::update(uint32_t now_ms) {
  updateSensors(now_ms);

  if (server_ != nullptr && (state_ == State::ApRunning || state_ == State::StaRunning)) {
    server_->handleClient();
  }

  if (state_ == State::StaConnecting) {
    const int status = WiFi.status();
    if (status != last_sta_wifi_status_) {
      Serial.printf("[WIFI] STA status -> %s (%d)\n", wifiStatusName(status), status);
      last_sta_wifi_status_ = status;
    }
    if (status == WL_CONNECTED) {
      state_ = State::StaRunning;
      sta_session_start_ms_ = now_ms;
      markActivity(now_ms);
      startServer();
      initSD();
      MDNS.end();
      if (MDNS.begin(kDefaultHostname)) {
        MDNS.addService("http", "tcp", kHttpPort);
        Serial.printf("[WIFI] STA connected ip=%s mdns=http://%s.local/\n",
                      WiFi.localIP().toString().c_str(), kDefaultHostname);
      } else {
        Serial.printf("[WIFI] STA connected ip=%s mdns_start_failed\n",
                      WiFi.localIP().toString().c_str());
      }
      String local_time;
      String sync_error;
      if (syncClockWithTimezone(settings_.timezone, local_time, sync_error)) {
        Serial.printf("[TIME] synced tz=%s local=%s\n",
                      settings_.timezone.c_str(), local_time.c_str());
      } else {
        Serial.printf("[TIME] sync failed tz=%s err=%s\n",
                      settings_.timezone.c_str(), sync_error.c_str());
      }
    } else if ((now_ms - sta_connect_start_ms_) >= kStaConnectTimeoutMs) {
      Serial.printf("[WIFI] STA connect timeout -> stop (last_status=%s/%d)\n",
                    wifiStatusName(status), status);
      sta_connect_failed_ = true;
      stop("sta_connect_timeout");
      auto_exit_requested_ = true;
    }
    return;
  }

  if (state_ == State::StaRunning && WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] STA lost connection -> stop");
    sta_connect_failed_ = true;
    stop("sta_lost_connection");
    auto_exit_requested_ = true;
    return;
  }

  updateTimeout(now_ms);
}

void WifiManager::startAP() {
  stop("switch_to_ap");
  digitalWrite(kPeripheralPowerPin, HIGH);
  delay(3);
  WiFi.mode(WIFI_AP);
  const bool ok = WiFi.softAP(kDefaultApSsid, kDefaultApPass);
  if (!ok) {
    Serial.println("[WIFI] AP start failed");
    state_ = State::Idle;
    return;
  }

  state_ = State::ApRunning;
  markActivity(millis());
  startServer();
  initSD();
  Serial.printf("[WIFI] AP started ssid=%s ip=%s timeout=%lus\n", kDefaultApSsid,
                WiFi.softAPIP().toString().c_str(),
                static_cast<unsigned long>(kApIdleTimeoutMs / 1000));
  Serial.println("[WIFI] portal url: http://192.168.4.1/");
}

void WifiManager::startSTA() {
  stop("switch_to_sta");
  digitalWrite(kPeripheralPowerPin, HIGH);
  delay(3);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(kDefaultHostname);
  logStaScanResults();
  WiFi.begin(settings_.sta_ssid.c_str(), settings_.sta_pass.c_str());
  sta_connect_start_ms_ = millis();
  state_ = State::StaConnecting;
  last_sta_wifi_status_ = WiFi.status();
  Serial.printf("[WIFI] STA connecting ssid=%s pass_len=%u timeout=%lus initial_status=%s/%d\n",
                settings_.sta_ssid.c_str(),
                static_cast<unsigned>(settings_.sta_pass.length()),
                static_cast<unsigned long>(kStaConnectTimeoutMs / 1000),
                wifiStatusName(last_sta_wifi_status_),
                last_sta_wifi_status_);
}

void WifiManager::stop(const char *reason) {
  if (state_ != State::Idle) {
    Serial.printf("[WIFI] stop reason=%s\n", reason ? reason : "none");
  }
  stopServer();
  deinitSD();
  MDNS.end();
  const wifi_mode_t current_mode = WiFi.getMode();
  if (current_mode != WIFI_MODE_NULL) {
    if (current_mode == WIFI_MODE_STA || current_mode == WIFI_MODE_APSTA) {
      WiFi.disconnect(false, false);
      delay(60);
    }
    WiFi.mode(WIFI_OFF);
    delay(60);
  }
  digitalWrite(kPeripheralPowerPin, LOW);
  state_ = State::Idle;
  sta_connect_start_ms_ = 0;
  sta_session_start_ms_ = 0;
  last_activity_ms_ = 0;
  last_sta_wifi_status_ = WL_IDLE_STATUS;
}

bool WifiManager::consumeAutoExitRequested() {
  const bool value = auto_exit_requested_;
  auto_exit_requested_ = false;
  return value;
}

bool WifiManager::consumeStaConnectFailed() {
  const bool value = sta_connect_failed_;
  sta_connect_failed_ = false;
  return value;
}

bool WifiManager::isStaConnected() const {
  return state_ == State::StaRunning;
}

const WifiManager::Settings &WifiManager::settings() const {
  return settings_;
}

size_t WifiManager::calendarEventCount() const {
  return calendar_event_count_;
}

bool WifiManager::calendarEventAt(size_t index, CalendarEvent &event) const {
  if (index >= calendar_event_count_) {
    return false;
  }
  event = calendar_events_[index];
  return true;
}

void WifiManager::loadSettings() {
  if (prefs_ == nullptr) {
    prefs_ = new Preferences();
  }
  if (!prefs_->begin("config", false)) {
    applyDefaultSettings();
    return;
  }

  applyDefaultSettings();
  if (prefs_->isKey("sta_ssid")) settings_.sta_ssid = prefs_->getString("sta_ssid", kDefaultStaSsid);
  if (prefs_->isKey("sta_pass")) settings_.sta_pass = prefs_->getString("sta_pass", kDefaultStaPass);
  if (prefs_->isKey("ui_lang")) settings_.ui_language = prefs_->getString("ui_lang", kDefaultUiLanguage);
  if (prefs_->isKey("timezone")) settings_.timezone = prefs_->getString("timezone", kDefaultTimezone);
  if (prefs_->isKey("photo_sec")) settings_.photo_interval_sec = prefs_->getUInt("photo_sec", 3600);
  if (prefs_->isKey("cal_en")) settings_.calendar_enabled = prefs_->getBool("cal_en", false);
  if (prefs_->isKey("cal_layout")) settings_.calendar_layout = prefs_->getString("cal_layout", "landscape_split");
  if (prefs_->isKey("cal_sec")) settings_.calendar_refresh_sec = prefs_->getUInt("cal_sec", 900);
  if (prefs_->isKey("cal_url")) settings_.calendar_url = prefs_->getString("cal_url", kDefaultCalendarUrl);
  if (prefs_->isKey("weather_city")) settings_.weather_city = prefs_->getString("weather_city", kDefaultWeatherCity);
  if (prefs_->isKey("weather_lat")) settings_.weather_lat = prefs_->getString("weather_lat", kDefaultWeatherLat);
  if (prefs_->isKey("weather_lon")) settings_.weather_lon = prefs_->getString("weather_lon", kDefaultWeatherLon);
  if (prefs_->isKey("weather_url")) settings_.weather_url = prefs_->getString("weather_url", kDefaultWeatherUrl);
  settings_.calendar_layout.trim();
  settings_.calendar_layout.toLowerCase();
  if (!(settings_.calendar_layout == "landscape_split" || settings_.calendar_layout == "portrait_split")) {
    settings_.calendar_layout = "landscape_split";
  }
  settings_.ui_language.trim();
  settings_.ui_language.toLowerCase();
  if (!(settings_.ui_language == "zh" || settings_.ui_language == "en" || settings_.ui_language == "fr")) {
    settings_.ui_language = kDefaultUiLanguage;
  }
  next_calendar_event_id_ =
      static_cast<uint16_t>(prefs_->getUInt("cal_next_id", static_cast<uint32_t>(next_calendar_event_id_)));
  if (next_calendar_event_id_ == 0) {
    next_calendar_event_id_ = 1;
  }
  deserializeCalendarEvents(prefs_->getString("cal_events", ""));
  prefs_->end();
  Serial.printf("[CFG] loaded sta_ssid=%s (%s)\n",
                settings_.sta_ssid.c_str(),
                (settings_.sta_ssid == kDefaultStaSsid) ? "default" : "prefs");
}

void WifiManager::saveSettings() {
  if (prefs_ == nullptr) {
    prefs_ = new Preferences();
  }
  if (!prefs_->begin("config", false)) {
    Serial.println("[CFG] preferences open failed");
    return;
  }
  prefs_->putString("sta_ssid", settings_.sta_ssid);
  prefs_->putString("sta_pass", settings_.sta_pass);
  prefs_->putString("ui_lang", settings_.ui_language);
  prefs_->putString("timezone", settings_.timezone);
  prefs_->putUInt("photo_sec", settings_.photo_interval_sec);
  prefs_->putBool("cal_en", settings_.calendar_enabled);
  prefs_->putString("cal_layout", settings_.calendar_layout);
  prefs_->putUInt("cal_sec", settings_.calendar_refresh_sec);
  prefs_->putString("cal_url", settings_.calendar_url);
  prefs_->putString("weather_city", settings_.weather_city);
  prefs_->putString("weather_lat", settings_.weather_lat);
  prefs_->putString("weather_lon", settings_.weather_lon);
  prefs_->putString("weather_url", settings_.weather_url);
  prefs_->putUInt("cal_next_id", next_calendar_event_id_);
  prefs_->putString("cal_events", serializeCalendarEvents());
  prefs_->end();
  Serial.println("[CFG] settings saved");
}

void WifiManager::applyDefaultSettings() {
  settings_.sta_ssid = kDefaultStaSsid;
  settings_.sta_pass = kDefaultStaPass;
  settings_.ui_language = kDefaultUiLanguage;
  settings_.timezone = kDefaultTimezone;
  settings_.photo_interval_sec = 3600;
  settings_.calendar_enabled = false;
  settings_.calendar_layout = "landscape_split";
  settings_.calendar_refresh_sec = 900;
  settings_.calendar_url = kDefaultCalendarUrl;
  settings_.weather_city = kDefaultWeatherCity;
  settings_.weather_lat = kDefaultWeatherLat;
  settings_.weather_lon = kDefaultWeatherLon;
  settings_.weather_url = kDefaultWeatherUrl;
  calendar_event_count_ = 0;
  next_calendar_event_id_ = 1;
}

String WifiManager::serializeCalendarEvents() const {
  String out;
  for (size_t i = 0; i < calendar_event_count_; ++i) {
    const CalendarEvent &e = calendar_events_[i];
    if (i > 0) {
      out += "\n";
    }
    out += String(e.id);
    out += "|";
    out += urlEncode(e.date);
    out += "|";
    out += urlEncode(e.time_hhmm);
    out += "|";
    out += urlEncode(e.end_time_hhmm);
    out += "|";
    out += urlEncode(e.color);
    out += "|";
    out += urlEncode(e.repeat);
    out += "|";
    out += String(e.weekday);
    out += "|";
    out += urlEncode(e.title);
    out += "|";
    out += urlEncode(e.source);
    out += "|";
    out += urlEncode(e.external_id);
    out += "|";
    out += urlEncode(e.updated_at);
  }
  return out;
}

void WifiManager::deserializeCalendarEvents(const String &packed) {
  calendar_event_count_ = 0;
  uint16_t max_id = 0;
  int start = 0;
  while (start <= packed.length() &&
         calendar_event_count_ < static_cast<size_t>(kMaxCalendarEvents)) {
    int end = packed.indexOf('\n', start);
    if (end < 0) {
      end = packed.length();
    }
    String line = packed.substring(start, end);
    line.trim();
    start = end + 1;
    if (line.length() == 0) {
      continue;
    }

    String fields[11];
    int field_count = 0;
    int field_start = 0;
    while (field_start <= line.length() && field_count < 11) {
      const int sep = line.indexOf('|', field_start);
      if (sep < 0) {
        fields[field_count++] = line.substring(field_start);
        field_start = line.length() + 1;
        break;
      }
      fields[field_count++] = line.substring(field_start, sep);
      field_start = sep + 1;
    }
    if (!(field_count == 7 || field_count == 11)) {
      continue;
    }

    CalendarEvent e;
    e.id = static_cast<uint16_t>(fields[0].toInt());
    e.date = urlDecode(fields[1]);
    e.time_hhmm = urlDecode(fields[2]);
    if (field_count >= 11) {
      e.end_time_hhmm = urlDecode(fields[3]);
      e.color = urlDecode(fields[4]);
      e.repeat = urlDecode(fields[5]);
      e.weekday = static_cast<int8_t>(fields[6].toInt());
      e.title = urlDecode(fields[7]);
      e.source = urlDecode(fields[8]);
      e.external_id = urlDecode(fields[9]);
      e.updated_at = urlDecode(fields[10]);
    } else {
      e.end_time_hhmm = "";
      e.color = urlDecode(fields[3]);
      e.repeat = urlDecode(fields[4]);
      e.weekday = static_cast<int8_t>(fields[5].toInt());
      e.title = urlDecode(fields[6]);
      e.source = "manual";
      e.external_id = "";
      e.updated_at = "";
    }
    e.title.trim();

    String normalized_time;
    String normalized_end_time;
    String normalized_date;
    if (!normalizeCalendarTime(e.time_hhmm, normalized_time)) {
      continue;
    }
    e.time_hhmm = normalized_time;
    if (e.end_time_hhmm.length() > 0) {
      if (!normalizeCalendarTime(e.end_time_hhmm, normalized_end_time)) {
        e.end_time_hhmm = "";
      } else {
        e.end_time_hhmm = normalized_end_time;
      }
    }
    e.color = normalizeCalendarColor(e.color);
    e.repeat = normalizeCalendarRepeat(e.repeat);
    e.source = normalizeCalendarSource(e.source);
    e.external_id = normalizeCalendarExternalId(e.external_id);
    e.updated_at = normalizeCalendarUpdatedAt(e.updated_at);
    if (e.repeat == "once") {
      if (!normalizeCalendarDate(e.date, normalized_date)) {
        continue;
      }
      e.date = normalized_date;
      e.weekday = -1;
    } else if (e.repeat == "weekly") {
      if (e.weekday < 0 || e.weekday > 6) {
        continue;
      }
      e.date = "";
    } else {
      e.weekday = -1;
      e.date = "";
    }
    if (e.id == 0) {
      continue;
    }
    if (e.title.length() == 0) {
      e.title = "Event";
    }
    if (e.id > max_id) {
      max_id = e.id;
    }
    calendar_events_[calendar_event_count_++] = e;
  }

  if (next_calendar_event_id_ <= max_id) {
    next_calendar_event_id_ = static_cast<uint16_t>(max_id + 1);
    if (next_calendar_event_id_ == 0) {
      next_calendar_event_id_ = 1;
    }
  }
}

String WifiManager::calendarEventsJson() const {
  String json = "{\"ok\":true,\"items\":[";
  for (size_t i = 0; i < calendar_event_count_; ++i) {
    const CalendarEvent &e = calendar_events_[i];
    if (i > 0) {
      json += ",";
    }
    json += "{";
    json += "\"id\":";
    json += String(e.id);
    json += ",\"title\":\"";
    json += jsonEscape(e.title);
    json += "\",\"date\":\"";
    json += jsonEscape(e.date);
    json += "\",\"time\":\"";
    json += jsonEscape(e.time_hhmm);
    json += "\",\"end_time\":\"";
    json += jsonEscape(e.end_time_hhmm);
    json += "\",\"color\":\"";
    json += jsonEscape(e.color);
    json += "\",\"repeat\":\"";
    json += jsonEscape(e.repeat);
    json += "\",\"weekday\":";
    json += String(e.weekday);
    json += ",\"source\":\"";
    json += jsonEscape(e.source);
    json += "\",\"external_id\":\"";
    json += jsonEscape(e.external_id);
    json += "\",\"updated_at\":\"";
    json += jsonEscape(e.updated_at);
    json += "}";
  }
  json += "]}";
  return json;
}

bool WifiManager::normalizeCalendarTime(const String &raw, String &normalized) const {
  String value = raw;
  value.trim();
  if (value.length() != 5 || value[2] != ':') {
    return false;
  }
  if (!isDigit(value[0]) || !isDigit(value[1]) || !isDigit(value[3]) || !isDigit(value[4])) {
    return false;
  }
  const int hh = (value[0] - '0') * 10 + (value[1] - '0');
  const int mm = (value[3] - '0') * 10 + (value[4] - '0');
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) {
    return false;
  }
  normalized = value;
  return true;
}

bool WifiManager::normalizeCalendarDate(const String &raw, String &normalized) const {
  String value = raw;
  value.trim();
  if (value.length() != 10 || value[4] != '-' || value[7] != '-') {
    return false;
  }
  for (int i = 0; i < 10; ++i) {
    if (i == 4 || i == 7) continue;
    if (!isDigit(value[i])) return false;
  }
  const int year = value.substring(0, 4).toInt();
  const int month = value.substring(5, 7).toInt();
  const int day = value.substring(8, 10).toInt();
  if (year < 2000 || year > 2099) {
    return false;
  }
  if (month < 1 || month > 12) {
    return false;
  }
  const int max_day = daysInMonth(year, month);
  if (day < 1 || day > max_day) {
    return false;
  }
  normalized = value;
  return true;
}

String WifiManager::normalizeCalendarColor(const String &raw) const {
  String value = raw;
  value.trim();
  value.toLowerCase();
  if (value == "black" || value == "white" || value == "yellow" || value == "red" ||
      value == "blue" || value == "green") {
    return value;
  }
  return "blue";
}

String WifiManager::normalizeCalendarRepeat(const String &raw) const {
  String value = raw;
  value.trim();
  value.toLowerCase();
  if (value == "once" || value == "daily" || value == "weekly") {
    return value;
  }
  return "weekly";
}

String WifiManager::normalizeCalendarSource(const String &raw) const {
  String value = raw;
  value.trim();
  value.toLowerCase();
  if (value.length() == 0) {
    return "manual";
  }
  String out;
  out.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.') {
      out += c;
    }
  }
  if (out.length() == 0) {
    out = "manual";
  }
  if (out.length() > 24) {
    out = out.substring(0, 24);
  }
  return out;
}

String WifiManager::normalizeCalendarExternalId(const String &raw) const {
  String value = raw;
  value.trim();
  if (value.length() > 80) {
    value = value.substring(0, 80);
  }
  return value;
}

String WifiManager::normalizeCalendarUpdatedAt(const String &raw) const {
  String value = raw;
  value.trim();
  if (value.length() > 32) {
    value = value.substring(0, 32);
  }
  return value;
}

int WifiManager::findCalendarEventIndexById(uint16_t id) const {
  for (size_t i = 0; i < calendar_event_count_; ++i) {
    if (calendar_events_[i].id == id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int WifiManager::findCalendarEventIndexByExternal(const String &source,
                                                  const String &external_id) const {
  if (source.length() == 0 || external_id.length() == 0) {
    return -1;
  }
  for (size_t i = 0; i < calendar_event_count_; ++i) {
    if (calendar_events_[i].source == source &&
        calendar_events_[i].external_id == external_id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void WifiManager::startServer() {
  stopServer();
  server_ = new WebServer(kHttpPort);

  server_->on("/", HTTP_GET, [this]() { handleRoot(); });
  server_->on("/app.js", HTTP_GET, [this]() {
    markActivity(millis());
    initWebFs();
    if (web_fs_ready_) {
      File file = SPIFFS.open("/app.js", FILE_READ);
      if (file) {
        server_->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        server_->sendHeader("Pragma", "no-cache");
        server_->streamFile(file, "application/javascript; charset=utf-8");
        file.close();
        return;
      }
    }
    server_->send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
  });
  server_->on("/favicon.ico", HTTP_ANY, [this]() {
    markActivity(millis());
    server_->send(204, "text/plain", "");
  });
  server_->on("/api/status", HTTP_GET, [this]() { handleStatus(); });
  server_->on("/api/settings", HTTP_GET, [this]() { handleSettingsGet(); });
  server_->on("/api/settings", HTTP_POST, [this]() { handleSettingsPost(); });
  server_->on("/api/calendar/events", HTTP_GET, [this]() { handleCalendarEventsGet(); });
  server_->on("/api/calendar/events", HTTP_POST, [this]() { handleCalendarEventsPost(); });
  server_->on("/api/calendar/events", HTTP_DELETE, [this]() { handleCalendarEventsDelete(); });
  server_->on("/api/geocode", HTTP_GET, [this]() { handleGeocode(); });
  server_->on("/api/files", HTTP_GET, [this]() { handleFilesList(); });
  server_->on("/api/dir", HTTP_POST, [this]() { handleDirCreate(); });
  server_->on("/api/file", HTTP_GET, [this]() { handleFileDownload(); });
  server_->on("/api/file", HTTP_DELETE, [this]() { handleFileDelete(); });
  server_->on("/api/weather_test", HTTP_GET, [this]() { handleWeatherTest(); });
  server_->on("/api/stop", HTTP_POST, [this]() { handleStopPortal(); });
  server_->on("/api/reboot", HTTP_POST, [this]() { handleReboot(); });
  server_->on(
      "/api/upload", HTTP_POST,
      [this]() {
        markActivity(millis());
        if (upload_ok_) {
          server_->send(200, "application/json", "{\"ok\":true}");
          return;
        }
        String json = "{\"ok\":false,\"error\":\"";
        json += jsonEscape(upload_error_);
        json += "\"}";
        server_->send(500, "application/json", json);
      },
      [this]() { handleFileUpload(); });
  server_->onNotFound([this]() { handleNotFound(); });
  server_->begin();
  Serial.println("[WIFI] web server started");
}

void WifiManager::stopServer() {
  if (server_ == nullptr) {
    return;
  }
  server_->stop();
  delete server_;
  server_ = nullptr;
}

void WifiManager::updateTimeout(uint32_t now_ms) {
  if (state_ == State::ApRunning) {
    if (last_activity_ms_ == 0) {
      markActivity(now_ms);
      return;
    }
    // Keep AP alive while at least one station is connected.
    const int sta_num = WiFi.softAPgetStationNum();
    if (sta_num > 0) {
      markActivity(now_ms);
    }
    const uint32_t idle_ms = now_ms - last_activity_ms_;
    if (idle_ms >= kApIdleTimeoutMs) {
      Serial.printf("[WIFI] AP idle timeout -> stop (idle_ms=%lu, sta_num=%d)\n",
                    static_cast<unsigned long>(idle_ms), sta_num);
      stop("ap_idle_timeout");
      auto_exit_requested_ = true;
    }
    return;
  }
  if (state_ == State::StaRunning) {
    if (kStaSessionTimeoutMs == 0) {
      return;
    }
    const uint32_t base_ms =
        (last_activity_ms_ >= sta_session_start_ms_) ? last_activity_ms_ : sta_session_start_ms_;
    if (base_ms == 0) {
      sta_session_start_ms_ = now_ms;
      markActivity(now_ms);
      Serial.printf("[WIFI] STA idle baseline reset now=%lu\n",
                    static_cast<unsigned long>(now_ms));
      return;
    }
    const uint32_t idle_ms = now_ms - base_ms;
    if (idle_ms >= kStaSessionTimeoutMs) {
      Serial.printf("[WIFI] STA idle timeout -> stop (idle_ms=%lu base_ms=%lu now_ms=%lu last_activity_ms=%lu session_start_ms=%lu)\n",
                    static_cast<unsigned long>(idle_ms),
                    static_cast<unsigned long>(base_ms),
                    static_cast<unsigned long>(now_ms),
                    static_cast<unsigned long>(last_activity_ms_),
                    static_cast<unsigned long>(sta_session_start_ms_));
      stop("sta_idle_timeout");
      auto_exit_requested_ = true;
    }
  }
}

void WifiManager::markActivity(uint32_t now_ms) {
  last_activity_ms_ = now_ms;
}

void WifiManager::initSD() {
  if (sd_ready_) {
    return;
  }
  if (!sd_spi_started_) {
    sd_spi_.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
    sd_spi_started_ = true;
  }
  sd_ready_ = SD.begin(kSdCsPin, sd_spi_);
  Serial.printf("[SD] init %s\n", sd_ready_ ? "ok" : "failed");
}

void WifiManager::deinitSD() {
  if (sd_ready_) {
    SD.end();
    sd_ready_ = false;
  }
}

void WifiManager::initWebFs() {
  if (web_fs_ready_) {
    return;
  }
  web_fs_ready_ = SPIFFS.begin(false);
  Serial.printf("[WEB] SPIFFS init %s\n", web_fs_ready_ ? "ok" : "failed");
}

String WifiManager::listDirectoryJson(const char *path) {
  if (!sd_ready_) {
    return "{\"ok\":false,\"error\":\"sd_not_ready\"}";
  }

  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) {
    return "{\"ok\":false,\"error\":\"invalid_path\"}";
  }

  String out = "{\"ok\":true,\"path\":\"";
  out += jsonEscape(path);
  out += "\",\"items\":[";
  uint32_t skipped = 0;
  bool first = true;
  File entry = dir.openNextFile();
  while (entry) {
    const String name = String(entry.name());
    if (String(path) == "/pic" && !entry.isDirectory() && !isEpd4Path(name)) {
      ++skipped;
      entry = dir.openNextFile();
      continue;
    }
    if (!first) {
      out += ",";
    }
    first = false;
    out += "{\"name\":\"";
    out += jsonEscape(name);
    out += "\",\"dir\":";
    out += entry.isDirectory() ? "true" : "false";
    out += ",\"size\":";
    out += String(static_cast<uint32_t>(entry.size()));
    out += "}";
    entry = dir.openNextFile();
  }
  out += "],\"skipped\":";
  out += String(skipped);
  out += "}";
  Serial.printf("[SD] list path=%s items_done skipped=%lu\n", path,
                static_cast<unsigned long>(skipped));
  return out;
}

String WifiManager::contentTypeForPath(const String &path) const {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css")) return "text/css";
  if (path.endsWith(".js")) return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".bmp")) return "image/bmp";
  return "application/octet-stream";
}

bool WifiManager::isSafePath(const String &path) const {
  if (path.length() == 0 || path[0] != '/') {
    return false;
  }
  if (path.indexOf("..") >= 0 || path.indexOf('\\') >= 0) {
    return false;
  }
  return true;
}

bool WifiManager::isEpd4Path(const String &path) const {
  String lower = path;
  lower.toLowerCase();
  return lower.endsWith(".epd4");
}

bool WifiManager::removePathRecursive(const String &path) const {
  File entry = SD.open(path);
  if (!entry) {
    return false;
  }
  if (!entry.isDirectory()) {
    entry.close();
    return SD.remove(path);
  }

  File child = entry.openNextFile();
  while (child) {
    const String child_name = String(child.name());
    const bool child_is_dir = child.isDirectory();
    child.close();
    if (!removePathRecursive(child_name)) {
      entry.close();
      return false;
    }
    child = entry.openNextFile();
  }
  entry.close();
  return SD.rmdir(path);
}

String WifiManager::currentIp() const {
  if (state_ == State::ApRunning) {
    return WiFi.softAPIP().toString();
  }
  return WiFi.localIP().toString();
}

void WifiManager::initSensors() {
  Wire.begin(kI2cSdaPin, kI2cSclPin);
  battery_adc_pin_ = 39;  // User-requested trial pin.
  pinMode(battery_adc_pin_, INPUT);
  analogSetPinAttenuation(static_cast<uint8_t>(battery_adc_pin_), ADC_11db);
  analogReadResolution(12);
  battery_mv_ = readBatteryMilliVolts(battery_adc_pin_);
  last_sensor_poll_ms_ = millis();
  Serial.printf("[SENSOR] battery adc pin=%d mv=%d\n", battery_adc_pin_, battery_mv_);
}

void WifiManager::updateSensors(uint32_t now_ms) {
  if ((now_ms - last_sensor_poll_ms_) < kSensorPollIntervalMs) {
    return;
  }
  last_sensor_poll_ms_ = now_ms;
  if (battery_adc_pin_ >= 0) {
    battery_mv_ = readBatteryMilliVolts(battery_adc_pin_);
  }
  if (state_ != State::Idle) {
    float temperature_c = NAN;
    float humidity_pct = NAN;
    if (readAHT20(temperature_c, humidity_pct)) {
      temperature_c_ = temperature_c;
      humidity_pct_ = humidity_pct;
    } else {
      temperature_c_ = NAN;
      humidity_pct_ = NAN;
    }
  }
}

bool WifiManager::readAHT20(float &temperature_c, float &humidity_pct) {
  auto readStatus = []() -> int {
    Wire.beginTransmission(kAht20Address);
    Wire.write(0x71);
    if (Wire.endTransmission(false) != 0) {
      return -1;
    }
    const int n = Wire.requestFrom(static_cast<int>(kAht20Address), 1);
    if (n != 1 || !Wire.available()) {
      return -1;
    }
    return Wire.read();
  };

  int status = readStatus();
  if (status < 0) {
    aht_ready_ = false;
    return false;
  }

  if ((status & 0x08) == 0) {
    Wire.beginTransmission(kAht20Address);
    Wire.write(0xBE);
    Wire.write(0x08);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
      aht_ready_ = false;
      return false;
    }
    delay(10);
    status = readStatus();
    if (status < 0 || (status & 0x08) == 0) {
      aht_ready_ = false;
      return false;
    }
  }

  Wire.beginTransmission(kAht20Address);
  Wire.write(0xAC);
  Wire.write(0x33);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) {
    aht_ready_ = false;
    return false;
  }

  delay(85);
  status = readStatus();
  if (status < 0 || (status & 0x80) != 0) {
    aht_ready_ = false;
    return false;
  }

  const int len = Wire.requestFrom(static_cast<int>(kAht20Address), 6);
  if (len != 6) {
    aht_ready_ = false;
    return false;
  }
  uint8_t data[6] = {0};
  for (int i = 0; i < 6; ++i) {
    if (!Wire.available()) {
      aht_ready_ = false;
      return false;
    }
    data[i] = static_cast<uint8_t>(Wire.read());
  }

  const uint32_t raw_humidity =
      (static_cast<uint32_t>(data[1]) << 12) |
      (static_cast<uint32_t>(data[2]) << 4) |
      (static_cast<uint32_t>(data[3]) >> 4);
  const uint32_t raw_temperature =
      ((static_cast<uint32_t>(data[3]) & 0x0F) << 16) |
      (static_cast<uint32_t>(data[4]) << 8) |
      static_cast<uint32_t>(data[5]);

  humidity_pct = (static_cast<float>(raw_humidity) * 100.0f) / 1048576.0f;
  temperature_c = (static_cast<float>(raw_temperature) * 200.0f) / 1048576.0f - 50.0f;
  aht_ready_ = true;
  return true;
}

int WifiManager::readBatteryMilliVolts(int pin) const {
  const int raw_mv = analogReadMilliVolts(static_cast<uint8_t>(pin));
  if (raw_mv <= 0) {
    return raw_mv;
  }
  // This board commonly feeds VBAT through a 1:1 divider to the ADC input.
  const int scaled_mv = raw_mv * 2;
  if (scaled_mv >= 3000 && scaled_mv <= 5000) {
    return scaled_mv;
  }
  return raw_mv;
}

void WifiManager::detectBatteryPin() {}

float WifiManager::estimateBatteryPercent(int battery_mv) const {
  if (battery_mv <= 0) {
    return -1.0f;
  }
  constexpr float kMinMv = 3300.0f;
  constexpr float kMaxMv = 4200.0f;
  float pct = (static_cast<float>(battery_mv) - kMinMv) * 100.0f / (kMaxMv - kMinMv);
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  return pct;
}

void WifiManager::handleRoot() {
  markActivity(millis());
  const String mode = (state_ == State::ApRunning) ? "AP" : ((state_ == State::StaRunning) ? "STA" : "IDLE");
  Serial.printf("[HTTP] GET / from %s mode=%s\n", server_->client().remoteIP().toString().c_str(),
                mode.c_str());
  initWebFs();
  if (web_fs_ready_) {
    File file = SPIFFS.open(kPortalHtmlPath, FILE_READ);
    if (file) {
      server_->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
      server_->sendHeader("Pragma", "no-cache");
      server_->streamFile(file, "text/html; charset=utf-8");
      file.close();
      return;
    }
    Serial.println("[WEB] missing /portal.html in SPIFFS, falling back to embedded page");
  } else {
    Serial.println("[WEB] SPIFFS not ready, falling back to embedded page");
  }
  const char *fallback =
      "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' "
      "content='width=device-width,initial-scale=1'><title>Portal Missing</title></head>"
      "<body style='font-family:sans-serif;padding:16px'>"
      "<h3>Portal page missing</h3>"
      "<p>SPIFFS file not found: /portal.html</p>"
      "<p>Please upload filesystem assets (uploadfs), then refresh.</p>"
      "</body></html>";
  server_->send(200, "text/html; charset=utf-8", fallback);
  return;
  String html;
  html.reserve(18000);
  html += R"HTML(
<!doctype html>
<html>
<head>
  <meta charset='utf-8'>
  <meta name='viewport' content='width=device-width,initial-scale=1'>
  <title>E-paper 管理台</title>
  <style>
    :root {
      --bg1:#eef3f8;
      --bg2:#f7f9fc;
      --card:#ffffffd9;
      --line:#d7dee7;
      --text:#1f2a36;
      --muted:#5d6b79;
      --accent:#0f7ae5;
      --accent2:#2aa3f7;
      --danger:#df4d4d;
      --shadow:0 10px 24px rgba(17,34,68,.08);
      --soft:#eef5fc;
    }
    * { box-sizing: border-box; }
    body {
      margin:0;
      font-family:"Noto Sans SC","Source Han Sans SC","PingFang SC","Microsoft YaHei",sans-serif;
      background:
        radial-gradient(1200px 500px at -10% -20%, #d5e7ff 0%, transparent 60%),
        radial-gradient(1000px 600px at 110% -10%, #d8f1ff 0%, transparent 60%),
        linear-gradient(180deg,var(--bg1),var(--bg2));
      color:var(--text);
      min-height:100vh;
    }
    .wrap { max-width: 980px; margin: 20px auto; padding: 0 12px; }
    .top {
      background:var(--card);
      border:1px solid var(--line);
      border-radius:14px;
      padding:14px;
      margin-bottom:12px;
      backdrop-filter: blur(3px);
      box-shadow: var(--shadow);
    }
    .title { font-size:24px; font-weight:700; margin-bottom:6px; letter-spacing:.3px; }
    .meta { color:var(--muted); font-size:14px; }
    .tabs { display:flex; gap:8px; margin-top:10px; }
    .tab-btn {
      border:1px solid var(--line);
      background:#fff;
      color:var(--text);
      padding:8px 13px;
      border-radius:10px;
      cursor:pointer;
      transition:.2s ease;
    }
    .tab-btn:hover { transform: translateY(-1px); border-color:#b9c7d6; }
    .tab-btn.active {
      background: linear-gradient(135deg,var(--accent),var(--accent2));
      color:#fff;
      border-color:transparent;
      box-shadow: 0 4px 12px rgba(15,122,229,.28);
    }
    .card {
      background:var(--card);
      border:1px solid var(--line);
      border-radius:14px;
      padding:14px;
      margin-bottom:12px;
      backdrop-filter: blur(3px);
      box-shadow: var(--shadow);
    }
    .tab-panel { display:none; }
    .tab-panel.active { display:block; }
    .grid2 { display:grid; grid-template-columns:1fr 1fr; gap:10px; }
    .field label { display:block; font-size:13px; color:var(--muted); margin-bottom:4px; }
    .field input {
      width:100%;
      padding:9px 10px;
      border:1px solid var(--line);
      border-radius:10px;
      background:#fff;
      transition:.2s ease;
    }
    .field input:focus { outline:none; border-color:var(--accent); box-shadow:0 0 0 3px rgba(15,122,229,.12); }
    .btn {
      padding:8px 12px;
      border:1px solid var(--line);
      border-radius:10px;
      background:#fff;
      cursor:pointer;
      transition:.2s ease;
    }
    .btn:hover { transform: translateY(-1px); }
    .btn.primary { background: linear-gradient(135deg,var(--accent),var(--accent2)); color:#fff; border-color:transparent; }
    .btn.warn { background: var(--danger); color:#fff; border-color:var(--danger); }
    .row { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
    pre {
      margin:0;
      padding:10px;
      background:#f5f7fa;
      border:1px solid var(--line);
      border-radius:10px;
      overflow:auto;
      line-height:1.45;
    }
    table { width:100%; border-collapse:collapse; margin-top:8px; }
    th,td { padding:8px; border-bottom:1px solid var(--line); text-align:left; font-size:14px; }
    th { color:var(--muted); font-weight:600; }
    .small { font-size:12px; color:#6b7785; }
    .status-grid { display:grid; grid-template-columns:repeat(2,minmax(220px,1fr)); gap:10px; margin-top:10px; }
    .status-item { border:1px solid var(--line); border-radius:10px; background:#f7f9fc; padding:10px; }
    .status-item .k { font-size:12px; color:var(--muted); margin-bottom:4px; }
    .status-item .v { font-size:16px; font-weight:600; color:var(--text); }
    .notice {
      margin-top:10px;
      padding:10px 12px;
      border:1px solid #cfe0f3;
      background:#f3f8fe;
      border-radius:10px;
      color:#37516d;
      white-space:pre-wrap;
      display:none;
    }
    .path-bar {
      display:flex;
      gap:8px;
      align-items:center;
      justify-content:space-between;
      flex-wrap:wrap;
      margin-bottom:10px;
    }
    .path-tools { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
    .crumbs { display:flex; gap:6px; align-items:center; flex-wrap:wrap; margin-bottom:10px; }
    .crumb {
      border:1px solid var(--line);
      background:var(--soft);
      color:var(--text);
      padding:6px 10px;
      border-radius:999px;
      cursor:pointer;
    }
    .crumb.current { background:#fff; font-weight:600; }
    .quick-links { display:flex; gap:8px; flex-wrap:wrap; margin-bottom:10px; }
    .file-table td.actions { width:220px; }
    .name-btn {
      border:none;
      background:none;
      color:var(--text);
      cursor:pointer;
      padding:0;
      font:inherit;
      text-align:left;
    }
    .name-btn.dir { color:var(--accent); font-weight:600; }
    .badge {
      display:inline-block;
      min-width:56px;
      padding:4px 8px;
      border-radius:999px;
      background:#eef4fb;
      color:#46627f;
      font-size:12px;
      text-align:center;
    }
    .badge.dir { background:#dff0ff; color:#185b91; }
    .upload-extra {
      margin-top:10px;
      padding:12px;
      border:1px solid var(--line);
      border-radius:12px;
      background:#f8fbff;
      display:none;
    }
    .upload-extra.show { display:block; }
    .hint-line { margin-top:8px; font-size:12px; color:#617180; line-height:1.6; }
    .tooltip {
      position:relative;
      display:inline-flex;
      align-items:center;
      justify-content:center;
      width:18px;
      height:18px;
      border-radius:50%;
      border:1px solid #b9c7d6;
      color:#4c6075;
      font-size:12px;
      cursor:help;
      background:#fff;
    }
    .tooltip .tip {
      position:absolute;
      left:50%;
      bottom:calc(100% + 8px);
      transform:translateX(-50%);
      width:min(320px, 80vw);
      padding:10px 12px;
      border-radius:10px;
      background:#1f2a36;
      color:#fff;
      font-size:12px;
      line-height:1.6;
      box-shadow:0 10px 24px rgba(17,34,68,.22);
      opacity:0;
      pointer-events:none;
      transition:.18s ease;
      white-space:normal;
      z-index:10;
    }
    .tooltip:hover .tip { opacity:1; }
    .range-wrap { display:flex; align-items:center; gap:8px; flex-wrap:wrap; }
    .range-wrap input[type='range'] { width:200px; }
    .mode-note {
      margin-top:8px;
      padding:8px 10px;
      border-radius:10px;
      background:#fff;
      border:1px dashed #c9d7e6;
      color:#5b6a78;
      font-size:12px;
      line-height:1.5;
    }
    @media (max-width: 700px) {
      .grid2 { grid-template-columns:1fr; }
      .status-grid { grid-template-columns:1fr; }
      .file-table td.actions { width:auto; }
    }
  </style>
</head>
<body>
  <div class='wrap'>
    <div class='top'>
      <div class='title'>E-paper 管理台</div>
      <div class='meta'>默认首页：设备状态</div>
      <div class='tabs'>
        <button class='tab-btn active' data-tab='status'>设备状态</button>
        <button class='tab-btn' data-tab='config'>配置</button>
        <button class='tab-btn' data-tab='files'>目录</button>
      </div>
    </div>

    <section id='tab-status' class='tab-panel active'>
      <div class='card'>
        <div class='row'>
          <button class='btn primary' onclick='loadStatus()'>刷新状态</button>
          <button class='btn warn' onclick='stopPortal()'>停止 WiFi 门户</button>
        </div>
        <div class='small' style='margin-top:8px'>当前设备状态</div>
        <div class='status-grid'>
          <div class='status-item'><div class='k'>运行模式</div><div class='v' id='st_mode'>--</div></div>
          <div class='status-item'><div class='k'>设备 IP</div><div class='v' id='st_ip'>--</div></div>
          <div class='status-item'><div class='k'>AP IP</div><div class='v' id='st_apip'>--</div></div>
          <div class='status-item'><div class='k'>SD 卡状态</div><div class='v' id='st_sd'>--</div></div>
          <div class='status-item'><div class='k'>已运行时长</div><div class='v' id='st_uptime'>--</div></div>
          <div class='status-item'><div class='k'>超时剩余</div><div class='v' id='st_timeout'>--</div></div>
          <div class='status-item'><div class='k'>温度</div><div class='v' id='st_temp'>未接入</div></div>
          <div class='status-item'><div class='k'>电量</div><div class='v' id='st_battery'>未接入</div></div>
        </div>
        <div id='statusNote' class='notice'></div>
      </div>
    </section>

    <section id='tab-config' class='tab-panel'>
      <div class='card'>
        <div class='grid2'>
          <div class='field'><label>STA SSID</label><input id='ssid'></div>
          <div class='field'><label>STA 密码</label><input id='pass' type='password'></div>
          <div class='field'><label>时区</label><input id='tz'></div>
          <div class='field'><label>轮播间隔（秒）</label><input id='sec' type='number' min='30'></div>
          <div class='field' style='grid-column:1/3'><label>天气接口 URL</label><input id='wurl'></div>
        </div>
        <div class='row' style='margin-top:10px'>
          <button class='btn primary' onclick='saveCfg()'>保存配置</button>
          <button class='btn' onclick='testWeather()'>测试天气服务</button>
        </div>
        <pre id='cfgBox' style='margin-top:8px'>等待操作...</pre>
      </div>
    </section>

    <section id='tab-files' class='tab-panel'>
      <div class='card'>
        <div class='path-bar'>
          <div class='path-tools'>
            <button class='btn' onclick='goRoot()'>根目录</button>
            <button class='btn' onclick='goUpDir()'>上一级</button>
            <button class='btn primary' onclick='listFiles()'>刷新目录</button>
          </div>
          <div class='small'>双击目录名或点击进入按钮即可浏览下一层</div>
        </div>
        <div id='crumbs' class='crumbs'></div>
        <div class='quick-links'>
          <button class='btn' onclick='openDir("/pic")'>图片目录 /pic</button>
          <button class='btn' onclick='openDir("/")'>存储根目录 /</button>
        </div>
        <table class='file-table'>
          <thead><tr><th>名称</th><th>大小</th><th>类型</th><th>操作</th></tr></thead>
          <tbody id='fileRows'></tbody>
        </table>
        <div class='small' id='fileSummary' style='margin-top:8px'>等待加载...</div>
      </div>
      <div class='card'>
        <div class='row'>
          <input id='uploadFile' type='file'>
          <select id='uploadMode' style='padding:8px;border:1px solid var(--line);border-radius:8px;min-width:220px;'>
            <option value='normal'>普通文件上传</option>
            <option value='fit'>图片缩放上传</option>
            <option value='crop'>图片裁剪上传</option>
          </select>
          <button class='btn primary' onclick='uploadFile()'>执行上传</button>
        </div>
        <div id='uploadExtra' class='upload-extra'>
          <div class='grid2'>
            <div class='field'>
              <label>抖动算法</label>
              <select id='ditherMode' style='padding:8px;border:1px solid var(--line);border-radius:8px;'>
                <option value='fs_serpentine' selected>Floyd-Steinberg（通用 / 人像推荐）</option>
                <option value='atkinson'>Atkinson（插画 / 图标推荐）</option>
                <option value='jjn'>Jarvis-Judice-Ninke（风景照推荐）</option>
              </select>
              <div id='ditherHint' class='hint-line'></div>
            </div>
            <div class='field'>
              <label style='display:flex;align-items:center;gap:6px;'>
                Gamma
                <span class='tooltip'>?
                  <span class='tip'>推荐值：1.00 为默认通用值；0.90-0.98 适合偏暗照片，可提亮暗部；1.05-1.15 适合风景或高亮场景，层次更稳；高于 1.20 会让画面更重、更深，但暗部细节更容易丢失；低于 0.90 会整体更亮，但可能发灰、对比下降。</span>
                </span>
              </label>
              <div class='range-wrap'>
                <input id='gammaCtrl' type='range' min='0.80' max='1.40' step='0.01' value='1.00'>
                <span id='gammaVal'>1.00</span>
              </div>
              <div id='gammaHint' class='hint-line'></div>
            </div>
          </div>
          <div id='modeNote' class='mode-note'></div>
        </div>
        <pre id='uploadBox' style='margin-top:8px'>等待上传...</pre>
      </div>
    </section>
  </div>

  <script>
    const tabs = [...document.querySelectorAll('.tab-btn')];
    const panels = {
      status: document.getElementById('tab-status'),
      config: document.getElementById('tab-config'),
      files: document.getElementById('tab-files'),
    };
    tabs.forEach(btn => btn.addEventListener('click', () => {
      tabs.forEach(b => b.classList.remove('active'));
      Object.values(panels).forEach(p => p.classList.remove('active'));
      btn.classList.add('active');
      panels[btn.dataset.tab].classList.add('active');
    }));
    const statusNote = document.getElementById('statusNote');
    const crumbs = document.getElementById('crumbs');
    const fileRows = document.getElementById('fileRows');
    const fileSummary = document.getElementById('fileSummary');
    const gammaCtrl = document.getElementById('gammaCtrl');
    const gammaVal = document.getElementById('gammaVal');
    const uploadModeSel = document.getElementById('uploadMode');
    const ditherModeSel = document.getElementById('ditherMode');
    const uploadExtra = document.getElementById('uploadExtra');
    const uploadBtn = document.querySelector("button[onclick='uploadFile()']");
    const uploadBox = document.getElementById('uploadBox');
    const ditherHint = document.getElementById('ditherHint');
    const gammaHint = document.getElementById('gammaHint');
    const modeNote = document.getElementById('modeNote');
    let currentDir = '/pic';

    function setNotice(text) {
      if (!statusNote) return;
      if (!text) {
        statusNote.style.display = 'none';
        statusNote.textContent = '';
        return;
      }
      statusNote.style.display = 'block';
      statusNote.textContent = text;
    }

    const syncGammaLabel = () => {
      if (!gammaCtrl || !gammaVal) return;
      const g = Number.parseFloat(gammaCtrl.value);
      gammaVal.textContent = Number.isFinite(g) ? g.toFixed(2) : '1.00';
    };
    const ditherAdvice = {
      fs_serpentine: '通用默认。人像、日常照片优先用这个，边缘和肤色过渡更自然。',
      atkinson: '对比更干净，适合图标、漫画、线稿和高反差插画。',
      jjn: '扩散更平滑，适合风景照、大面积渐变和天空云层。'
    };
    function normalizeDir(path) {
      let out = path || '/';
      if (!out.startsWith('/')) out = '/' + out;
      out = out.replace(/\/+/g, '/');
      if (out.length > 1 && out.endsWith('/')) out = out.slice(0, -1);
      return out || '/';
    }
    function parentDir(path) {
      const dir = normalizeDir(path);
      if (dir === '/') return '/';
      const idx = dir.lastIndexOf('/');
      return idx <= 0 ? '/' : dir.slice(0, idx);
    }
    function joinPath(dir, name){
      const base = normalizeDir(dir);
      if (!name) return base;
      if (name.startsWith('/')) return normalizeDir(name);
      return normalizeDir((base === '/' ? '' : base) + '/' + name);
    }
    function formatSize(bytes) {
      const n = Number(bytes || 0);
      if (n < 1024) return `${n} B`;
      if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
      return `${(n / (1024 * 1024)).toFixed(1)} MB`;
    }
    function renderBreadcrumb(path) {
      const dir = normalizeDir(path);
      crumbs.innerHTML = '';
      const parts = dir === '/' ? [] : dir.slice(1).split('/');
      const rootBtn = document.createElement('button');
      rootBtn.className = 'crumb' + (dir === '/' ? ' current' : '');
      rootBtn.textContent = '/';
      rootBtn.onclick = () => openDir('/');
      crumbs.appendChild(rootBtn);
      let acc = '';
      parts.forEach((part, idx) => {
        const sep = document.createElement('span');
        sep.className = 'small';
        sep.textContent = '/';
        crumbs.appendChild(sep);
        acc += '/' + part;
        const btn = document.createElement('button');
        btn.className = 'crumb' + (idx === parts.length - 1 ? ' current' : '');
        btn.textContent = part;
        const target = acc;
        btn.onclick = () => openDir(target);
        crumbs.appendChild(btn);
      });
    }
    function syncGammaHint() {
      if (!gammaHint || !gammaCtrl) return;
      const g = Number.parseFloat(gammaCtrl.value);
      if (!Number.isFinite(g)) {
        gammaHint.textContent = '当前为默认值。';
      } else if (g < 0.95) {
        gammaHint.textContent = '当前偏亮：暗部会被抬起，适合原图偏暗，但可能降低整体对比。';
      } else if (g <= 1.08) {
        gammaHint.textContent = '当前均衡：适合作为通用起点，先看效果再微调。';
      } else if (g <= 1.20) {
        gammaHint.textContent = '当前偏重：中间调和颜色更扎实，适合风景和高亮场景。';
      } else {
        gammaHint.textContent = '当前较重：画面对比更强，但暗部细节更容易丢失。';
      }
    }
    function syncUploadOptions() {
      const mode = uploadModeSel ? uploadModeSel.value : 'normal';
      const isImageMode = mode === 'fit' || mode === 'crop';
      if (uploadExtra) uploadExtra.classList.toggle('show', isImageMode);
      if (ditherHint && ditherModeSel) {
        ditherHint.textContent = ditherAdvice[ditherModeSel.value] || '';
      }
      if (modeNote) {
        if (mode === 'normal') {
          modeNote.textContent = '普通文件上传会直接保存到当前浏览目录。';
        } else if (mode === 'fit') {
          modeNote.textContent = '图片缩放上传会按比例适配到 800x480，并生成 .epd4 文件后保存到 /pic。';
        } else {
          modeNote.textContent = '图片裁剪上传会优先铺满 800x480，可能裁掉边缘，再生成 .epd4 文件后保存到 /pic。';
        }
      }
      syncGammaHint();
    }
    if (uploadModeSel) {
      uploadModeSel.value = 'normal';
      uploadModeSel.addEventListener('change', syncUploadOptions);
    }
    if (ditherModeSel) {
      ditherModeSel.value = 'fs_serpentine';
      ditherModeSel.addEventListener('change', syncUploadOptions);
    }
    if (gammaCtrl) {
      gammaCtrl.addEventListener('input', syncGammaLabel);
      gammaCtrl.addEventListener('input', syncGammaHint);
    }
    syncGammaLabel();
    syncUploadOptions();

    function fmtMs(ms){
      const s = Math.floor((ms||0)/1000);
      const h = Math.floor(s/3600);
      const m = Math.floor((s%3600)/60);
      const ss = s%60;
      return `${h}h ${m}m ${ss}s`;
    }
    async function loadStatus() {
      const r = await fetch('/api/status');
      const txt = await r.text();
      try {
        const j = JSON.parse(txt);
        document.getElementById('st_mode').textContent = j.state || '--';
        document.getElementById('st_ip').textContent = j.ip || '--';
        document.getElementById('st_apip').textContent = j.ap_ip || '--';
        document.getElementById('st_sd').textContent = j.sd_ready ? '已挂载' : '未挂载';
        document.getElementById('st_uptime').textContent = fmtMs(j.uptime_ms || 0);
        const tmo = j.idle_remaining_ms ?? j.connect_remaining_ms ?? j.session_remaining_ms ?? 0;
        document.getElementById('st_timeout').textContent = fmtMs(tmo);
        document.getElementById('st_temp').textContent = (typeof j.temperature_c === 'number' && j.temperature_c >= -100) ? `${j.temperature_c.toFixed(1)} °C` : '未接入';
        const mv = Number(j.battery_mv || 0);
        const pct = Number(j.battery_pct);
        const pin = j.battery_pin ?? '?';
        if (mv > 0 && Number.isFinite(pct) && pct > 0.1) {
          document.getElementById('st_battery').textContent = `${pct.toFixed(1)}%（${mv} mV，IO${pin}）`;
        } else if (mv > 0) {
          document.getElementById('st_battery').textContent = `${mv} mV（IO${pin}，仅显示电压）`;
        } else {
          document.getElementById('st_battery').textContent = '未接入';
        }
        setNotice('');
      } catch {
        setNotice(txt || '状态读取失败');
      }
    }

    async function loadCfg() {
      const r = await fetch('/api/settings');
      const j = await r.json();
      ssid.value = j.sta_ssid || '';
      pass.value = j.sta_pass || '';
      tz.value = j.timezone || '';
      sec.value = j.photo_interval_sec || 300;
      wurl.value = j.weather_url || '';
    }

    async function saveCfg() {
      const body = `sta_ssid=${encodeURIComponent(ssid.value)}&sta_pass=${encodeURIComponent(pass.value)}&timezone=${encodeURIComponent(tz.value)}&photo_interval_sec=${encodeURIComponent(sec.value)}&weather_url=${encodeURIComponent(wurl.value)}`;
      const r = await fetch('/api/settings', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body });
      document.getElementById('cfgBox').textContent = await r.text();
    }

    async function testWeather() {
      const r = await fetch('/api/weather_test');
      document.getElementById('cfgBox').textContent = await r.text();
    }

    async function stopPortal() {
      const r = await fetch('/api/stop', { method:'POST' });
      const txt = await r.text();
      let msg = '停止请求已发送。';
      try {
        const j = JSON.parse(txt);
        if (j && j.ok) {
          msg = j.stopping ? '停止请求已发送，WiFi 门户即将关闭。' : '请求已完成。';
        }
      } catch {}
      setNotice(msg);
    }

    async function openDir(path) {
      currentDir = normalizeDir(path);
      await listFiles(currentDir);
    }
    function goRoot() {
      openDir('/');
    }
    function goUpDir() {
      openDir(parentDir(currentDir));
    }
    async function listFiles(path) {
      const dir = normalizeDir(path || currentDir || '/pic');
      currentDir = dir;
      renderBreadcrumb(dir);
      const r = await fetch('/api/files?path=' + encodeURIComponent(dir));
      const txt = await r.text();
      let j = null;
      try { j = JSON.parse(txt); } catch {}
      fileRows.innerHTML = '';
      if (!j || !j.ok || !Array.isArray(j.items)) {
        fileSummary.textContent = txt || '目录读取失败';
        return;
      }
      currentDir = normalizeDir(j.path || dir);
      renderBreadcrumb(currentDir);
      const items = [...j.items].sort((a, b) => {
        if (!!a.dir !== !!b.dir) return a.dir ? -1 : 1;
        return String(a.name || '').localeCompare(String(b.name || ''), 'zh-Hans-CN');
      });
      let count = 0;
      items.forEach(it => {
        count++;
        const p = joinPath(currentDir, it.name || '');
        const tr = document.createElement('tr');
        tr.innerHTML = `<td></td><td>${it.dir ? '--' : formatSize(it.size || 0)}</td><td><span class="badge${it.dir ? ' dir' : ''}">${it.dir ? '目录' : '文件'}</span></td><td class='actions'></td>`;
        const nameCell = tr.children[0];
        const nameBtn = document.createElement('button');
        nameBtn.className = 'name-btn' + (it.dir ? ' dir' : '');
        nameBtn.textContent = it.name || '';
        if (it.dir) {
          nameBtn.onclick = () => openDir(p);
          nameBtn.ondblclick = () => openDir(p);
        } else {
          nameBtn.onclick = () => window.open('/api/file?path=' + encodeURIComponent(p), '_blank');
        }
        nameCell.appendChild(nameBtn);
        const ops = tr.children[3];
        if (it.dir) {
          const openBtn = document.createElement('button');
          openBtn.className = 'btn';
          openBtn.textContent = '进入';
          openBtn.onclick = () => openDir(p);
          ops.appendChild(openBtn);
        } else {
          const a = document.createElement('a');
          a.href = '/api/file?path=' + encodeURIComponent(p);
          a.textContent = '下载';
          a.style.marginRight = '8px';
          ops.appendChild(a);
          const del = document.createElement('button');
          del.className = 'btn';
          del.textContent = '删除';
          del.onclick = async () => {
            const rr = await fetch('/api/file?path=' + encodeURIComponent(p), { method:'DELETE' });
            alert(await rr.text());
            listFiles(currentDir);
          };
          ops.appendChild(del);
        }
        fileRows.appendChild(tr);
      });
      fileSummary.textContent = `当前位置：${currentDir}，共 ${count} 项`;
    }

    function dist2Weighted(a, b) {
      const dr = a[0] - b[0];
      const dg = a[1] - b[1];
      const db = a[2] - b[2];
      return 3 * dr * dr + 6 * dg * dg + db * db;
    }

    function nearestTwoPalette(rgb, palette) {
      let first = 0;
      let second = 0;
      let best = Number.MAX_SAFE_INTEGER;
      let next = Number.MAX_SAFE_INTEGER;
      for (let pi = 0; pi < palette.length; pi++) {
        const d = dist2Weighted(rgb, palette[pi].rgb);
        if (d < best) {
          next = best;
          second = first;
          best = d;
          first = pi;
        } else if (d < next) {
          next = d;
          second = pi;
        }
      }
      return { first, second, best, next };
    }

    function quantizeImageToEpd4(rgba, W, H, palette, ditherMode, gammaValue) {
      const out = new Uint8Array((W * H) >> 1);
      const pix = new Uint8Array(W * H);

      // Error-diffusion working buffer.
      const work = new Float32Array(W * H * 3);
      const gamma = (Number.isFinite(gammaValue) && gammaValue > 0) ? gammaValue : 1.0;
      const invGamma = 1.0 / gamma;
      for (let i = 0, p = 0; i < W * H; i++, p += 4) {
        const a = rgba[p + 3];
        let r = (a < 8) ? 255 : rgba[p + 0];
        let g = (a < 8) ? 255 : rgba[p + 1];
        let b = (a < 8) ? 255 : rgba[p + 2];
        r = 255 * Math.pow(r / 255, invGamma);
        g = 255 * Math.pow(g / 255, invGamma);
        b = 255 * Math.pow(b / 255, invGamma);
        work[i * 3 + 0] = r;
        work[i * 3 + 1] = g;
        work[i * 3 + 2] = b;
      }

      const clamp255 = (v) => (v < 0 ? 0 : (v > 255 ? 255 : v));
      const addErr = (x, y, er, eg, eb, ratio) => {
        if (x < 0 || x >= W || y < 0 || y >= H) return;
        const idx = (y * W + x) * 3;
        work[idx + 0] += er * ratio;
        work[idx + 1] += eg * ratio;
        work[idx + 2] += eb * ratio;
      };

      const mode = (ditherMode || 'fs_serpentine').toLowerCase();
      for (let y = 0; y < H; y++) {
        const reverse = (mode === 'fs_serpentine' || mode === 'jjn') && ((y & 1) === 1);
        const xStart = reverse ? (W - 1) : 0;
        const xEnd = reverse ? -1 : W;
        const step = reverse ? -1 : 1;
        for (let x = xStart; x !== xEnd; x += step) {
          const wi = (y * W + x) * 3;
          const rgb = [
            clamp255(work[wi + 0]),
            clamp255(work[wi + 1]),
            clamp255(work[wi + 2]),
          ];
          const pick = nearestTwoPalette(rgb, palette);
          const chosen = palette[pick.first].rgb;
          const er = rgb[0] - chosen[0];
          const eg = rgb[1] - chosen[1];
          const eb = rgb[2] - chosen[2];
          pix[y * W + x] = palette[pick.first].nib;

          if (mode === 'fs_serpentine') {
            if (!reverse) {
              addErr(x + 1, y, er, eg, eb, 7 / 16);
              addErr(x - 1, y + 1, er, eg, eb, 3 / 16);
              addErr(x, y + 1, er, eg, eb, 5 / 16);
              addErr(x + 1, y + 1, er, eg, eb, 1 / 16);
            } else {
              addErr(x - 1, y, er, eg, eb, 7 / 16);
              addErr(x + 1, y + 1, er, eg, eb, 3 / 16);
              addErr(x, y + 1, er, eg, eb, 5 / 16);
              addErr(x - 1, y + 1, er, eg, eb, 1 / 16);
            }
          } else if (mode === 'atkinson') {
            const r = 1 / 8;
            addErr(x + 1, y, er, eg, eb, r);
            addErr(x + 2, y, er, eg, eb, r);
            addErr(x - 1, y + 1, er, eg, eb, r);
            addErr(x, y + 1, er, eg, eb, r);
            addErr(x + 1, y + 1, er, eg, eb, r);
            addErr(x, y + 2, er, eg, eb, r);
          } else if (mode === 'jjn') {
            if (!reverse) {
              addErr(x + 1, y, er, eg, eb, 7 / 48);
              addErr(x + 2, y, er, eg, eb, 5 / 48);
              addErr(x - 2, y + 1, er, eg, eb, 3 / 48);
              addErr(x - 1, y + 1, er, eg, eb, 5 / 48);
              addErr(x, y + 1, er, eg, eb, 7 / 48);
              addErr(x + 1, y + 1, er, eg, eb, 5 / 48);
              addErr(x + 2, y + 1, er, eg, eb, 3 / 48);
              addErr(x - 2, y + 2, er, eg, eb, 1 / 48);
              addErr(x - 1, y + 2, er, eg, eb, 3 / 48);
              addErr(x, y + 2, er, eg, eb, 5 / 48);
              addErr(x + 1, y + 2, er, eg, eb, 3 / 48);
              addErr(x + 2, y + 2, er, eg, eb, 1 / 48);
            } else {
              addErr(x - 1, y, er, eg, eb, 7 / 48);
              addErr(x - 2, y, er, eg, eb, 5 / 48);
              addErr(x + 2, y + 1, er, eg, eb, 3 / 48);
              addErr(x + 1, y + 1, er, eg, eb, 5 / 48);
              addErr(x, y + 1, er, eg, eb, 7 / 48);
              addErr(x - 1, y + 1, er, eg, eb, 5 / 48);
              addErr(x - 2, y + 1, er, eg, eb, 3 / 48);
              addErr(x + 2, y + 2, er, eg, eb, 1 / 48);
              addErr(x + 1, y + 2, er, eg, eb, 3 / 48);
              addErr(x, y + 2, er, eg, eb, 5 / 48);
              addErr(x - 1, y + 2, er, eg, eb, 3 / 48);
              addErr(x - 2, y + 2, er, eg, eb, 1 / 48);
            }
          }
        }
      }
      let outIdx = 0;
      for (let i = 0; i < W * H; i += 2) {
        out[outIdx++] = ((pix[i] & 0x0F) << 4) | (pix[i + 1] & 0x0F);
      }
      return out;
    }

    async function preprocessImageToEpd4Blob(file, cropMode, ditherMode = 'fs_serpentine', gammaValue = 1.0) {
      const img = new Image();
      const dataUrl = await new Promise((resolve, reject) => {
        const fr = new FileReader();
        fr.onload = () => resolve(fr.result);
        fr.onerror = () => reject(new Error('read_failed'));
        fr.readAsDataURL(file);
      });
      await new Promise((resolve, reject) => {
        img.onload = () => resolve();
        img.onerror = () => reject(new Error('decode_failed'));
        img.src = dataUrl;
      });

      const W = 800;
      const H = 480;
      const canvas = document.createElement('canvas');
      canvas.width = W;
      canvas.height = H;
      const ctx = canvas.getContext('2d', { willReadFrequently: true });
      const srcLandscape = img.width >= img.height;
      const frameLandscape = W >= H;
      const rotate90 = (srcLandscape !== frameLandscape);
      const srcW = rotate90 ? img.height : img.width;
      const srcH = rotate90 ? img.width : img.height;
      const sx = W / srcW;
      const sy = H / srcH;
      const scale = cropMode ? Math.max(sx, sy) : Math.min(sx, sy);
      const dw = srcW * scale;
      const dh = srcH * scale;
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, W, H);
      if (!rotate90) {
        const dx = (W - dw) * 0.5;
        const dy = (H - dh) * 0.5;
        ctx.drawImage(img, dx, dy, dw, dh);
      } else {
        // Rotate portrait input to landscape first, then apply fit/crop scaling.
        // Use -90deg to match expected orientation on this frame.
        ctx.save();
        ctx.translate(W * 0.5, H * 0.5);
        ctx.rotate(-Math.PI / 2);
        ctx.drawImage(img, -dh * 0.5, -dw * 0.5, dh, dw);
        ctx.restore();
      }
      const rgba = ctx.getImageData(0, 0, W, H).data;

      const paletteNative = [
        { rgb:[0,0,0], nib:0x00 },
        { rgb:[255,255,255], nib:0x01 },
        { rgb:[255,255,0], nib:0x02 }, // yellow
        { rgb:[255,0,0], nib:0x03 },   // red
        { rgb:[0,0,255], nib:0x05 },   // blue
        { rgb:[0,255,0], nib:0x06 },   // green
      ];
      const palette = paletteNative;

      const out = quantizeImageToEpd4(rgba, W, H, palette, ditherMode, gammaValue);
      return new Blob([out], { type: 'application/octet-stream' });
    }

    async function uploadFile() {
      const dir = currentDir || '/';
      const mode = document.getElementById('uploadMode').value || 'normal';
      const ditherMode = document.getElementById('ditherMode')
        ? (document.getElementById('ditherMode').value || 'fs_serpentine')
        : 'fs_serpentine';
      const gammaValue = document.getElementById('gammaCtrl')
        ? Number.parseFloat(document.getElementById('gammaCtrl').value || '1.0')
        : 1.0;
      const gammaText = Number.isFinite(gammaValue) ? gammaValue.toFixed(2) : '1.00';
      const f = document.getElementById('uploadFile').files[0];
      if (uploadBox && f) {
        uploadBox.textContent = `开始上传：模式=${mode}，算法=${ditherMode}，Gamma=${gammaText}，文件=${f.name || 'unknown'}`;
      }
      console.log('[UPLOAD]', { mode, ditherMode, gamma: gammaText, file: f ? (f.name || '') : '' });
      if (!f) {
        document.getElementById('uploadBox').textContent = '请选择要上传的文件';
        return;
      }
      if (mode === 'normal') {
        if (uploadBtn) uploadBtn.disabled = true;
        const fd = new FormData();
        fd.append('file', f);
        const q = '/api/upload?dir=' + encodeURIComponent(dir) + '&mode=normal'
          + '&algo=' + encodeURIComponent(ditherMode)
          + '&gamma=' + encodeURIComponent(gammaText);
        const r = await fetch(q, { method:'POST', body:fd });
        document.getElementById('uploadBox').textContent = await r.text();
        listFiles(currentDir);
        if (uploadBtn) uploadBtn.disabled = false;
        return;
      }
      const lower = (f.name || '').toLowerCase();
      if (!(lower.endsWith('.jpg') || lower.endsWith('.jpeg') || lower.endsWith('.png') || lower.endsWith('.bmp'))) {
        document.getElementById('uploadBox').textContent = '缩放/裁剪上传仅支持 bmp/jpg/png';
        return;
      }
      document.getElementById('uploadBox').textContent = '正在预处理并转换为 EPD4...';
      if (uploadBtn) uploadBtn.disabled = true;
      try {
        const epdBlob = await preprocessImageToEpd4Blob(f, mode === 'crop', ditherMode, gammaValue);
        const outName = (f.name.replace(/\.[^.]+$/, '') || 'image') + '.epd4';
        const fd = new FormData();
        fd.append('file', epdBlob, outName);
        const q = '/api/upload?dir=' + encodeURIComponent('/pic') + '&mode=normal'
          + '&algo=' + encodeURIComponent(ditherMode)
          + '&gamma=' + encodeURIComponent(gammaText);
        const r = await fetch(q, { method:'POST', body:fd });
        document.getElementById('uploadBox').textContent = await r.text();
        currentDir = '/pic';
        listFiles('/pic');
        if (uploadBtn) uploadBtn.disabled = false;
      } catch (e) {
        if (uploadBtn) uploadBtn.disabled = false;
        document.getElementById('uploadBox').textContent = '预处理失败: ' + (e && e.message ? e.message : String(e));
      }
    }

    loadStatus();
    loadCfg();
    listFiles();
  </script>
</body>
</html>
)HTML";
  server_->send(200, "text/html", html);
}

void WifiManager::handleStatus() {
  markActivity(millis());
  Serial.printf("[HTTP] GET /api/status from %s\n",
                server_->client().remoteIP().toString().c_str());
  const char *state_str = "idle";
  if (state_ == State::ApRunning) state_str = "ap_running";
  if (state_ == State::StaConnecting) state_str = "sta_connecting";
  if (state_ == State::StaRunning) state_str = "sta_running";
  String json = "{";
  json += "\"state\":\"";
  json += state_str;
  json += "\",\"ip\":\"";
  json += jsonEscape(WiFi.localIP().toString());
  json += "\",\"ap_ip\":\"";
  json += jsonEscape(WiFi.softAPIP().toString());
  json += "\",\"sd_ready\":";
  json += sd_ready_ ? "true" : "false";
  json += ",\"sd_total_bytes\":";
  json += String(sd_ready_ ? static_cast<uint32_t>(SD.totalBytes()) : 0);
  json += ",\"sd_used_bytes\":";
  json += String(sd_ready_ ? static_cast<uint32_t>(SD.usedBytes()) : 0);
  json += ",\"uptime_ms\":";
  json += String(millis());
  json += ",\"temperature_c\":";
  if (isnan(temperature_c_)) {
    json += "-1000";
  } else {
    json += String(temperature_c_, 1);
  }
  json += ",\"humidity_pct\":";
  if (isnan(humidity_pct_)) {
    json += "-1";
  } else {
    json += String(humidity_pct_, 1);
  }
  json += ",\"battery_mv\":";
  json += String(battery_mv_);
  json += ",\"battery_pin\":";
  json += String(battery_adc_pin_);
  json += ",\"battery_pct\":";
  const float battery_pct = estimateBatteryPercent(battery_mv_);
  if (battery_pct < 0.0f) {
    json += "-1";
  } else {
    json += String(battery_pct, 1);
  }
  if (state_ == State::ApRunning) {
    const uint32_t remain =
        (millis() - last_activity_ms_ >= kApIdleTimeoutMs)
            ? 0
            : (kApIdleTimeoutMs - (millis() - last_activity_ms_));
    json += ",\"idle_remaining_ms\":";
    json += String(remain);
  } else if (state_ == State::StaConnecting) {
    const uint32_t remain =
        (millis() - sta_connect_start_ms_ >= kStaConnectTimeoutMs)
            ? 0
            : (kStaConnectTimeoutMs - (millis() - sta_connect_start_ms_));
    json += ",\"connect_remaining_ms\":";
    json += String(remain);
  } else if (state_ == State::StaRunning) {
    const uint32_t remain =
        (millis() - last_activity_ms_ >= kStaSessionTimeoutMs)
            ? 0
            : (kStaSessionTimeoutMs - (millis() - last_activity_ms_));
    json += ",\"idle_remaining_ms\":";
    json += String(remain);
  }
  json += "}";
  server_->send(200, "application/json", json);
}

void WifiManager::handleSettingsGet() {
  markActivity(millis());
  Serial.printf("[HTTP] GET /api/settings from %s\n",
                server_->client().remoteIP().toString().c_str());
  String json = "{";
  json += "\"sta_ssid\":\"" + jsonEscape(settings_.sta_ssid) + "\",";
  json += "\"sta_pass\":\"" + jsonEscape(settings_.sta_pass) + "\",";
  json += "\"ui_language\":\"" + jsonEscape(settings_.ui_language) + "\",";
  json += "\"timezone\":\"" + jsonEscape(settings_.timezone) + "\",";
  json += "\"photo_interval_sec\":" + String(settings_.photo_interval_sec) + ",";
  json += "\"calendar_enabled\":";
  json += settings_.calendar_enabled ? "true," : "false,";
  json += "\"calendar_layout\":\"" + jsonEscape(settings_.calendar_layout) + "\",";
  json += "\"calendar_refresh_sec\":" + String(settings_.calendar_refresh_sec) + ",";
  json += "\"calendar_url\":\"" + jsonEscape(settings_.calendar_url) + "\",";
  json += "\"weather_city\":\"" + jsonEscape(settings_.weather_city) + "\",";
  json += "\"weather_lat\":\"" + jsonEscape(settings_.weather_lat) + "\",";
  json += "\"weather_lon\":\"" + jsonEscape(settings_.weather_lon) + "\",";
  json += "\"weather_url\":\"" + jsonEscape(settings_.weather_url) + "\"";
  json += "}";
  server_->send(200, "application/json", json);
}

void WifiManager::handleSettingsPost() {
  markActivity(millis());
  Serial.printf("[HTTP] POST /api/settings from %s\n",
                server_->client().remoteIP().toString().c_str());
  if (server_->hasArg("sta_ssid")) settings_.sta_ssid = server_->arg("sta_ssid");
  if (server_->hasArg("sta_pass")) settings_.sta_pass = server_->arg("sta_pass");
  if (server_->hasArg("ui_language")) settings_.ui_language = server_->arg("ui_language");
  if (server_->hasArg("timezone")) settings_.timezone = server_->arg("timezone");
  if (server_->hasArg("photo_interval_sec")) {
    settings_.photo_interval_sec = static_cast<uint32_t>(server_->arg("photo_interval_sec").toInt());
    if (settings_.photo_interval_sec < 30) settings_.photo_interval_sec = 30;
    if (settings_.photo_interval_sec > 86400) settings_.photo_interval_sec = 86400;
  }
  if (server_->hasArg("calendar_enabled")) {
    const String enabled = server_->arg("calendar_enabled");
    settings_.calendar_enabled = (enabled == "1" || enabled == "true" || enabled == "on");
  }
  if (server_->hasArg("calendar_layout")) settings_.calendar_layout = server_->arg("calendar_layout");
  if (server_->hasArg("calendar_refresh_sec")) {
    settings_.calendar_refresh_sec =
        static_cast<uint32_t>(server_->arg("calendar_refresh_sec").toInt());
    if (settings_.calendar_refresh_sec < 60) settings_.calendar_refresh_sec = 60;
    if (settings_.calendar_refresh_sec > 86400) settings_.calendar_refresh_sec = 86400;
  }
  if (server_->hasArg("calendar_url")) settings_.calendar_url = server_->arg("calendar_url");
  if (server_->hasArg("weather_city")) settings_.weather_city = server_->arg("weather_city");
  if (server_->hasArg("weather_lat")) settings_.weather_lat = server_->arg("weather_lat");
  if (server_->hasArg("weather_lon")) settings_.weather_lon = server_->arg("weather_lon");
  if (server_->hasArg("weather_url")) settings_.weather_url = server_->arg("weather_url");

  settings_.sta_ssid.trim();
  settings_.ui_language.trim();
  settings_.timezone.trim();
  settings_.calendar_layout.trim();
  settings_.calendar_url.trim();
  settings_.weather_city.trim();
  settings_.weather_lat.trim();
  settings_.weather_lon.trim();
  settings_.weather_url.trim();
  if (settings_.sta_ssid.length() == 0) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"empty_sta_ssid\"}");
    return;
  }
  if (settings_.timezone.length() == 0) {
    settings_.timezone = kDefaultTimezone;
  }
  settings_.ui_language.toLowerCase();
  if (!(settings_.ui_language == "zh" || settings_.ui_language == "en" ||
        settings_.ui_language == "fr")) {
    settings_.ui_language = kDefaultUiLanguage;
  }
  settings_.calendar_layout.toLowerCase();
  if (!(settings_.calendar_layout == "landscape_split" || settings_.calendar_layout == "portrait_split")) {
    settings_.calendar_layout = "landscape_split";
  }
  if (settings_.calendar_url.length() == 0) {
    settings_.calendar_url = kDefaultCalendarUrl;
  }
  if (settings_.weather_url.length() == 0) {
    settings_.weather_url = kDefaultWeatherUrl;
  }
  if (settings_.weather_city.length() == 0) {
    settings_.weather_city = kDefaultWeatherCity;
  }
  if (settings_.weather_lat.length() == 0) {
    settings_.weather_lat = kDefaultWeatherLat;
  }
  if (settings_.weather_lon.length() == 0) {
    settings_.weather_lon = kDefaultWeatherLon;
  }
  saveSettings();
  server_->send(200, "application/json", "{\"ok\":true}");
}

void WifiManager::handleCalendarEventsGet() {
  markActivity(millis());
  Serial.printf("[HTTP] GET /api/calendar/events from %s\n",
                server_->client().remoteIP().toString().c_str());
  server_->send(200, "application/json", calendarEventsJson());
}

void WifiManager::handleCalendarEventsPost() {
  markActivity(millis());
  Serial.printf("[HTTP] POST /api/calendar/events from %s\n",
                server_->client().remoteIP().toString().c_str());

  CalendarEvent e;
  e.title = server_->hasArg("title") ? server_->arg("title") : "Event";
  e.title.trim();
  if (e.title.length() == 0) {
    e.title = "Event";
  }
  if (e.title.length() > 32) {
    e.title = e.title.substring(0, 32);
  }
  if (!server_->hasArg("time")) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_time\"}");
    return;
  }
  String normalized_time;
  String normalized_end_time;
  if (!normalizeCalendarTime(server_->arg("time"), normalized_time)) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_time\"}");
    return;
  }
  e.time_hhmm = normalized_time;
  if (server_->hasArg("end_time")) {
    const String end_time = server_->arg("end_time");
    if (end_time.length() > 0) {
      if (!normalizeCalendarTime(end_time, normalized_end_time)) {
        server_->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_end_time\"}");
        return;
      }
      e.end_time_hhmm = normalized_end_time;
    } else {
      e.end_time_hhmm = "";
    }
  } else {
    e.end_time_hhmm = "";
  }
  e.color = normalizeCalendarColor(server_->hasArg("color") ? server_->arg("color") : "blue");
  e.repeat = normalizeCalendarRepeat(server_->hasArg("repeat") ? server_->arg("repeat") : "weekly");
  e.source = normalizeCalendarSource(server_->hasArg("source") ? server_->arg("source") : "manual");
  e.external_id =
      normalizeCalendarExternalId(server_->hasArg("external_id") ? server_->arg("external_id") : "");
  if (server_->hasArg("updated_at")) {
    e.updated_at = normalizeCalendarUpdatedAt(server_->arg("updated_at"));
  } else {
    const time_t now_ts = time(nullptr);
    if (now_ts > 0) {
      e.updated_at = String(static_cast<unsigned long>(now_ts));
    } else {
      e.updated_at = "";
    }
  }

  if (e.repeat == "once") {
    if (!server_->hasArg("date")) {
      server_->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_date\"}");
      return;
    }
    String normalized_date;
    if (!normalizeCalendarDate(server_->arg("date"), normalized_date)) {
      server_->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_date\"}");
      return;
    }
    e.date = normalized_date;
    e.weekday = -1;
  } else if (e.repeat == "weekly") {
    if (!server_->hasArg("weekday")) {
      server_->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_weekday\"}");
      return;
    }
    const int weekday = server_->arg("weekday").toInt();
    if (weekday < 0 || weekday > 6) {
      server_->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_weekday\"}");
      return;
    }
    e.weekday = static_cast<int8_t>(weekday);
    e.date = "";
  } else {
    e.weekday = -1;
    e.date = "";
  }
  const int existing_idx = findCalendarEventIndexByExternal(e.source, e.external_id);
  if (existing_idx >= 0) {
    e.id = calendar_events_[existing_idx].id;
    calendar_events_[existing_idx] = e;
  } else {
    if (calendar_event_count_ >= static_cast<size_t>(kMaxCalendarEvents)) {
      server_->send(409, "application/json", "{\"ok\":false,\"error\":\"calendar_events_full\"}");
      return;
    }
    e.id = next_calendar_event_id_++;
    if (next_calendar_event_id_ == 0) {
      next_calendar_event_id_ = 1;
    }
    calendar_events_[calendar_event_count_++] = e;
  }
  saveSettings();
  server_->send(200, "application/json", calendarEventsJson());
}

void WifiManager::handleCalendarEventsDelete() {
  markActivity(millis());
  Serial.printf("[HTTP] DELETE /api/calendar/events from %s\n",
                server_->client().remoteIP().toString().c_str());
  int idx = -1;
  if (server_->hasArg("id")) {
    const uint16_t id = static_cast<uint16_t>(server_->arg("id").toInt());
    idx = findCalendarEventIndexById(id);
  } else if (server_->hasArg("source") && server_->hasArg("external_id")) {
    const String source = normalizeCalendarSource(server_->arg("source"));
    const String external_id = normalizeCalendarExternalId(server_->arg("external_id"));
    idx = findCalendarEventIndexByExternal(source, external_id);
  } else {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_id_or_external\"}");
    return;
  }
  if (idx < 0) {
    server_->send(404, "application/json", "{\"ok\":false,\"error\":\"event_not_found\"}");
    return;
  }

  for (size_t i = static_cast<size_t>(idx); i + 1 < calendar_event_count_; ++i) {
    calendar_events_[i] = calendar_events_[i + 1];
  }
  if (calendar_event_count_ > 0) {
    --calendar_event_count_;
  }
  saveSettings();
  server_->send(200, "application/json", calendarEventsJson());
}

void WifiManager::handleGeocode() {
  markActivity(millis());
  if (state_ != State::StaRunning || WiFi.status() != WL_CONNECTED) {
    server_->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"sta_required\",\"msg\":\"geocode requires STA connected\"}");
    return;
  }
  if (!server_->hasArg("city")) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_city\"}");
    return;
  }
  String city = server_->arg("city");
  city.trim();
  if (city.length() == 0) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"empty_city\"}");
    return;
  }

  String url = "https://geocoding-api.open-meteo.com/v1/search?count=1&language=zh&format=json&name=";
  url += urlEncode(city);
  Serial.printf("[HTTP] GET /api/geocode city=%s\n", city.c_str());

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  if (!http.begin(url)) {
    server_->send(500, "application/json", "{\"ok\":false,\"error\":\"http_begin_failed\"}");
    return;
  }
  const int code = http.GET();
  if (code != 200) {
    http.end();
    server_->send(502, "application/json", "{\"ok\":false,\"error\":\"geocode_http_failed\"}");
    return;
  }
  const String body = http.getString();
  http.end();

  const int results_idx = body.indexOf("\"results\":[");
  if (results_idx < 0) {
    server_->send(404, "application/json", "{\"ok\":false,\"error\":\"city_not_found\"}");
    return;
  }

  const int name_key = body.indexOf("\"name\":\"", results_idx);
  const int lat_key = body.indexOf("\"latitude\":", results_idx);
  const int lon_key = body.indexOf("\"longitude\":", results_idx);
  if (name_key < 0 || lat_key < 0 || lon_key < 0) {
    server_->send(500, "application/json", "{\"ok\":false,\"error\":\"geocode_parse_failed\"}");
    return;
  }

  const int name_start = name_key + 8;
  const int name_end = body.indexOf('"', name_start);
  const int lat_start = lat_key + 11;
  int lat_end = body.indexOf(',', lat_start);
  const int lon_start = lon_key + 12;
  int lon_end = body.indexOf(',', lon_start);
  if (lat_end < 0) lat_end = body.indexOf('}', lat_start);
  if (lon_end < 0) lon_end = body.indexOf('}', lon_start);
  if (name_end < 0 || lat_end < 0 || lon_end < 0) {
    server_->send(500, "application/json", "{\"ok\":false,\"error\":\"geocode_parse_failed\"}");
    return;
  }

  String resolved_name = body.substring(name_start, name_end);
  String lat = body.substring(lat_start, lat_end);
  String lon = body.substring(lon_start, lon_end);
  lat.trim();
  lon.trim();

  String json = "{\"ok\":true,\"city\":\"";
  json += jsonEscape(resolved_name);
  json += "\",\"lat\":\"";
  json += jsonEscape(lat);
  json += "\",\"lon\":\"";
  json += jsonEscape(lon);
  json += "\",\"weather_url\":\"https://api.open-meteo.com/v1/forecast?latitude=";
  json += jsonEscape(lat);
  json += "&longitude=";
  json += jsonEscape(lon);
  json += "&current=temperature_2m,relative_humidity_2m,weather_code&timezone=auto\"}";
  server_->send(200, "application/json", json);
}

void WifiManager::handleFilesList() {
  markActivity(millis());
  const String path = server_->hasArg("path") ? server_->arg("path") : "/pic";
  Serial.printf("[HTTP] GET /api/files path=%s\n", path.c_str());
  if (!isSafePath(path)) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_path\"}");
    return;
  }
  server_->send(200, "application/json", listDirectoryJson(path.c_str()));
}

void WifiManager::handleDirCreate() {
  markActivity(millis());
  if (!sd_ready_) {
    server_->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_ready\"}");
    return;
  }
  if (!server_->hasArg("path")) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_path\"}");
    return;
  }
  String path = server_->arg("path");
  path.trim();
  Serial.printf("[HTTP] POST /api/dir path=%s\n", path.c_str());
  if (!isSafePath(path) || path == "/") {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_path\"}");
    return;
  }
  if (SD.exists(path)) {
    server_->send(409, "application/json", "{\"ok\":false,\"error\":\"already_exists\"}");
    return;
  }
  const bool ok = SD.mkdir(path);
  server_->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void WifiManager::handleFileDownload() {
  markActivity(millis());
  if (!sd_ready_) {
    server_->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_ready\"}");
    return;
  }
  if (!server_->hasArg("path")) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_path\"}");
    return;
  }
  const String path = server_->arg("path");
  Serial.printf("[HTTP] GET /api/file path=%s\n", path.c_str());
  if (!isSafePath(path)) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_path\"}");
    return;
  }
  File file = SD.open(path, FILE_READ);
  if (!file) {
    server_->send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
    return;
  }
  server_->streamFile(file, contentTypeForPath(path));
  file.close();
}

void WifiManager::handleFileDelete() {
  markActivity(millis());
  if (!sd_ready_) {
    server_->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_ready\"}");
    return;
  }
  if (!server_->hasArg("path")) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_path\"}");
    return;
  }
  const String path = server_->arg("path");
  Serial.printf("[HTTP] DELETE /api/file path=%s\n", path.c_str());
  if (!isSafePath(path) || path == "/") {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_path\"}");
    return;
  }
  const bool ok = removePathRecursive(path);
  server_->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void WifiManager::handleWeatherTest() {
  markActivity(millis());
  Serial.printf("[HTTP] GET /api/weather_test url=%s\n", settings_.weather_url.c_str());

  if (state_ != State::StaRunning || WiFi.status() != WL_CONNECTED) {
    server_->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"sta_required\",\"msg\":\"weather test requires STA connected\"}");
    return;
  }
  if (!(settings_.weather_url.startsWith("http://") || settings_.weather_url.startsWith("https://"))) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_weather_url\"}");
    return;
  }

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  if (!http.begin(settings_.weather_url)) {
    server_->send(500, "application/json", "{\"ok\":false,\"error\":\"http_begin_failed\"}");
    return;
  }

  const int code = http.GET();
  if (code <= 0) {
    http.end();
    server_->send(502, "application/json", "{\"ok\":false,\"error\":\"weather_request_failed\"}");
    return;
  }

  String body = http.getString();
  http.end();

  String resolved_timezone = extractJsonStringField(body, "timezone");
  bool timezone_updated = false;
  if (resolved_timezone.length() == 0) {
    resolved_timezone = settings_.timezone;
  } else if (resolved_timezone != settings_.timezone) {
    settings_.timezone = resolved_timezone;
    saveSettings();
    timezone_updated = true;
    Serial.printf("[TIME] timezone updated from weather: %s\n", settings_.timezone.c_str());
  }

  String local_time;
  String time_sync_error;
  const bool time_sync_ok = syncClockWithTimezone(resolved_timezone, local_time, time_sync_error);
  if (time_sync_ok) {
    Serial.printf("[TIME] weather sync ok tz=%s local=%s\n",
                  resolved_timezone.c_str(), local_time.c_str());
  } else {
    Serial.printf("[TIME] weather sync failed tz=%s err=%s\n",
                  resolved_timezone.c_str(), time_sync_error.c_str());
  }

  String preview = body;
  preview.replace("\r", " ");
  preview.replace("\n", " ");
  if (preview.length() > 240) {
    preview = preview.substring(0, 240);
  }

  String json = "{";
  json += "\"ok\":true,";
  json += "\"msg\":\"weather_request_ok\",";
  json += "\"http_status\":";
  json += String(code);
  json += ",\"url\":\"";
  json += jsonEscape(settings_.weather_url);
  json += "\",\"preview\":\"";
  json += jsonEscape(preview);
  json += "\",\"ip\":\"";
  json += jsonEscape(WiFi.localIP().toString());
  json += "\",\"timezone\":\"";
  json += jsonEscape(resolved_timezone);
  json += "\",\"timezone_updated\":";
  json += timezone_updated ? "true" : "false";
  json += ",\"time_sync_ok\":";
  json += time_sync_ok ? "true" : "false";
  json += ",\"local_time\":\"";
  json += jsonEscape(local_time);
  json += "\",\"time_sync_error\":\"";
  json += jsonEscape(time_sync_error);
  json += "\"}";
  server_->send(200, "application/json", json);
}

void WifiManager::handleStopPortal() {
  markActivity(millis());
  Serial.printf("[HTTP] POST /api/stop from %s\n",
                server_->client().remoteIP().toString().c_str());
  server_->send(200, "application/json", "{\"ok\":true,\"stopping\":true}");
  stop("manual_http_stop");
  auto_exit_requested_ = true;
}

void WifiManager::handleReboot() {
  markActivity(millis());
  Serial.printf("[HTTP] POST /api/reboot from %s\n",
                server_->client().remoteIP().toString().c_str());
  server_->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
  delay(200);
  ESP.restart();
}

void WifiManager::handleFileUpload() {
  markActivity(millis());
  static File upload_file;
  static String upload_algo;
  static String upload_gamma;
  HTTPUpload &upload = server_->upload();

  if (upload.status == UPLOAD_FILE_START) {
    upload_ok_ = true;
    upload_error_ = "";
    upload_mode_ = server_->hasArg("mode") ? server_->arg("mode") : "normal";
    upload_mode_.toLowerCase();
    upload_algo = server_->hasArg("algo") ? server_->arg("algo") : "none";
    upload_gamma = server_->hasArg("gamma") ? server_->arg("gamma") : "1.00";
    upload_tmp_path_ = "";
    Serial.printf("[UPLOAD] begin mode=%s algo=%s gamma=%s remote=%s\n",
                  upload_mode_.c_str(),
                  upload_algo.c_str(),
                  upload_gamma.c_str(),
                  server_->client().remoteIP().toString().c_str());
  }

  if (!sd_ready_) {
    upload_ok_ = false;
    upload_error_ = "sd_not_ready";
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("[UPLOAD] reject err=%s\n", upload_error_.c_str());
    }
    return;
  }

  String filename = upload.filename;
  filename.replace("\\", "");
  filename.replace("/", "");
  if (filename.length() == 0) {
    upload_ok_ = false;
    upload_error_ = "bad_filename";
    return;
  }

  const bool preprocess_mode = (upload_mode_ == "fit" || upload_mode_ == "crop");
  const String dir = server_->hasArg("dir") ? server_->arg("dir") : "/";
  if (!isSafePath(dir)) {
    upload_ok_ = false;
    upload_error_ = "bad_dir";
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("[SD] upload rejected bad dir=%s\n", dir.c_str());
    }
    return;
  }
  String filepath = dir;
  if (!filepath.endsWith("/")) {
    filepath += "/";
  }
  filepath += filename;
  upload_tmp_path_ = filepath;

  if (upload.status == UPLOAD_FILE_START) {
    if (preprocess_mode) {
      upload_ok_ = false;
      upload_error_ = "server_preprocess_disabled_use_browser";
      Serial.printf("[UPLOAD] reject mode=%s err=%s\n", upload_mode_.c_str(),
                    upload_error_.c_str());
      return;
    }
    const int slash = filepath.lastIndexOf('/');
    const String parent = (slash > 0) ? filepath.substring(0, slash) : "/";
    if (parent.length() > 0 && !SD.exists(parent)) {
      SD.mkdir(parent);
    }
    if (SD.exists(filepath)) {
      SD.remove(filepath);
    }
    upload_file = SD.open(filepath, FILE_WRITE);
    if (!upload_file) {
      upload_ok_ = false;
      upload_error_ = "open_failed";
      Serial.printf("[SD] upload open failed %s\n", filepath.c_str());
      return;
    }
    Serial.printf("[SD] upload start %s\n", filepath.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (upload_file) {
      upload_file.write(upload.buf, upload.currentSize);
    } else {
      upload_ok_ = false;
      upload_error_ = "write_target_missing";
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (upload_file) {
      upload_file.close();
    }
    Serial.printf("[SD] upload done %s size=%u mode=%s algo=%s gamma=%s\n",
                  filepath.c_str(), upload.totalSize,
                  upload_mode_.c_str(),
                  upload_algo.c_str(),
                  upload_gamma.c_str());

    if (!upload_ok_) {
      SD.remove(filepath);
      Serial.printf("[UPLOAD] end with prior error=%s cleaned=%s\n", upload_error_.c_str(),
                    filepath.c_str());
      return;
    }

    if (upload_ok_) {
      Serial.printf("[UPLOAD] done ok mode=%s algo=%s gamma=%s final=%s\n",
                    upload_mode_.c_str(),
                    upload_algo.c_str(),
                    upload_gamma.c_str(),
                    filepath.c_str());
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    upload_ok_ = false;
    upload_error_ = "aborted";
    if (upload_file) {
      upload_file.close();
    }
    if (upload_tmp_path_.length() > 0) {
      SD.remove(upload_tmp_path_);
      Serial.printf("[SD] upload aborted %s\n", upload_tmp_path_.c_str());
    }
    Serial.printf("[UPLOAD] aborted err=%s\n", upload_error_.c_str());
  }
}

void WifiManager::handleNotFound() {
  markActivity(millis());
  server_->send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
}

}  // namespace appfw
