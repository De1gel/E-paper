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
#include <sys/time.h>
#include <time.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <esp_adc_cal.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <esp_wpa2.h>

#include "system/CalendarEventNormalize.h"
#include "system/CalendarIcsCore.h"
#include "system/LogConfig.h"
#include "system/SdCard.h"
#include "system/SettingsStore.h"

namespace appfw {
namespace {
constexpr const char *kDefaultApSsid = "PhotoFrame_Config";
constexpr const char *kDefaultApPass = "12345678";
constexpr const char *kDefaultHostname = "epaper";
constexpr const char *kPortalHtmlPath = "/portal.html";
constexpr const char *kCalendarCachePath = "/calendar-month.cache";
constexpr const char *kCalendarCacheTempPath = "/calendar-month.tmp";
constexpr const char *kCalendarCacheBackupPath = "/calendar-month.bak";
constexpr const char *kCalendarCacheMagic = "EPAPER_CALENDAR_CACHE_V1";
constexpr size_t kCalendarCacheMaxBytes = 65536u;
constexpr size_t kCalendarSourceMaxBytes = 65536u;
constexpr size_t kGeocodeResponseMaxBytes = 16384u;
constexpr size_t kWeatherResponseMaxBytes = 32768u;
constexpr uint16_t kHttpPort = 80;
constexpr uint8_t kI2cSdaPin = 21;
constexpr uint8_t kI2cSclPin = 22;
constexpr uint8_t kAht20Address = 0x38;
constexpr uint8_t kRx8025Address = 0x32;
constexpr time_t kMinTrustedEpoch = 1704067200;  // 2024-01-01 00:00:00 UTC.

class BoundedStringStream : public Stream {
 public:
  BoundedStringStream(String &target, size_t max_bytes)
      : target_(target), max_bytes_(max_bytes) {}

  size_t write(uint8_t value) override { return write(&value, 1u); }

  size_t write(const uint8_t *buffer, size_t size) override {
    if (!buffer || size == 0u) return 0u;
    if (target_.length() + size > max_bytes_) {
      overflowed_ = true;
      return 0u;
    }
    return target_.concat(reinterpret_cast<const char *>(buffer), size) ? size : 0u;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  bool overflowed() const { return overflowed_; }

 private:
  String &target_;
  size_t max_bytes_;
  bool overflowed_ = false;
};

bool readHttpBodyBounded(HTTPClient &http, size_t max_bytes, String &body) {
  body = "";
  const int content_length = http.getSize();
  if (content_length > static_cast<int>(max_bytes)) return false;
  const size_t reserve_bytes =
      (content_length > 0) ? static_cast<size_t>(content_length) : std::min<size_t>(4096u, max_bytes);
  if (!body.reserve(reserve_bytes)) return false;
  BoundedStringStream sink(body, max_bytes);
  const int written = http.writeToStream(&sink);
  return written >= 0 && !sink.overflowed() && body.length() <= max_bytes;
}

int32_t dayIdFromTm(const struct tm &tm_value) {
  return static_cast<int32_t>((tm_value.tm_year + 1900) * 10000 + (tm_value.tm_mon + 1) * 100 +
                              tm_value.tm_mday);
}

String formatDateYmd(const struct tm &tm_value) {
  char buf[16] = {0};
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm_value.tm_year + 1900, tm_value.tm_mon + 1,
           tm_value.tm_mday);
  return String(buf);
}

String formatTimeHm(const struct tm &tm_value) {
  char buf[8] = {0};
  snprintf(buf, sizeof(buf), "%02d:%02d", tm_value.tm_hour, tm_value.tm_min);
  return String(buf);
}

String formatDateTimeYmdHm(time_t epoch_value) {
  struct tm tm_value {};
  if (localtime_r(&epoch_value, &tm_value) == nullptr) {
    return String("invalid");
  }
  char buf[20] = {0};
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", tm_value.tm_year + 1900,
           tm_value.tm_mon + 1, tm_value.tm_mday, tm_value.tm_hour, tm_value.tm_min);
  return String(buf);
}

String formatMinuteHm(uint16_t minute_of_day) {
  if (minute_of_day >= 1440u) {
    minute_of_day = 0;
  }
  char buf[8] = {0};
  snprintf(buf, sizeof(buf), "%02u:%02u",
           static_cast<unsigned>(minute_of_day / 60u),
           static_cast<unsigned>(minute_of_day % 60u));
  return String(buf);
}

bool parseMinuteHm(const String &raw, uint16_t &minute_out) {
  String normalized;
  if (!normalizeCalendarTimeValue(raw, normalized)) {
    return false;
  }
  minute_out = static_cast<uint16_t>(
      normalized.substring(0, 2).toInt() * 60 + normalized.substring(3, 5).toInt());
  return true;
}

void mixCalendarHashByte(uint32_t &hash, uint8_t value) {
  hash ^= value;
  hash *= 16777619UL;
}

void mixCalendarHashString(uint32_t &hash, const String &value) {
  for (size_t i = 0; i < value.length(); ++i) {
    mixCalendarHashByte(hash, static_cast<uint8_t>(value[i]));
  }
  mixCalendarHashByte(hash, 0xFFu);
}

void mixCalendarHashInt(uint32_t &hash, uint32_t value) {
  for (uint8_t i = 0; i < 4u; ++i) {
    mixCalendarHashByte(hash, static_cast<uint8_t>((value >> (i * 8u)) & 0xFFu));
  }
}

uint8_t bcdToDec(uint8_t value) {
  return static_cast<uint8_t>(((value >> 4) * 10u) + (value & 0x0Fu));
}

uint8_t decToBcd(uint8_t value) {
  return static_cast<uint8_t>(((value / 10u) << 4) | (value % 10u));
}

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

time_t epochFromUtcTm(const struct tm &tm_value) {
  const int year = tm_value.tm_year + 1900;
  const unsigned month = static_cast<unsigned>(tm_value.tm_mon + 1);
  const unsigned day = static_cast<unsigned>(tm_value.tm_mday);
  const int64_t days = daysFromCivil(year, month, day);
  const int64_t seconds = days * 86400LL +
                          static_cast<int64_t>(tm_value.tm_hour) * 3600LL +
                          static_cast<int64_t>(tm_value.tm_min) * 60LL +
                          static_cast<int64_t>(tm_value.tm_sec);
  return static_cast<time_t>(seconds);
}

bool isTrustedEpoch(time_t epoch_value) {
  return epoch_value >= kMinTrustedEpoch;
}

const char *sntpSyncStatusName(sntp_sync_status_t status) {
  switch (status) {
    case SNTP_SYNC_STATUS_RESET:
      return "reset";
    case SNTP_SYNC_STATUS_COMPLETED:
      return "completed";
    case SNTP_SYNC_STATUS_IN_PROGRESS:
      return "in_progress";
    default:
      return "unknown";
  }
}

bool readRx8025Utc(time_t &epoch_value) {
  epoch_value = 0;
  Wire.beginTransmission(kRx8025Address);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const int len = Wire.requestFrom(static_cast<int>(kRx8025Address), 7);
  if (len != 7) {
    return false;
  }
  uint8_t data[7] = {};
  for (uint8_t i = 0; i < 7; ++i) {
    if (!Wire.available()) {
      return false;
    }
    data[i] = static_cast<uint8_t>(Wire.read());
  }

  struct tm tm_utc {};
  tm_utc.tm_sec = bcdToDec(data[0] & 0x7F);
  tm_utc.tm_min = bcdToDec(data[1] & 0x7F);
  tm_utc.tm_hour = bcdToDec(data[2] & 0x3F);
  tm_utc.tm_mday = bcdToDec(data[4] & 0x3F);
  tm_utc.tm_mon = bcdToDec(data[5] & 0x1F) - 1;
  tm_utc.tm_year = bcdToDec(data[6]) + 100;
  if (tm_utc.tm_sec > 59 || tm_utc.tm_min > 59 || tm_utc.tm_hour > 23 ||
      tm_utc.tm_mday < 1 || tm_utc.tm_mday > 31 || tm_utc.tm_mon < 0 ||
      tm_utc.tm_mon > 11) {
    return false;
  }
  epoch_value = epochFromUtcTm(tm_utc);
  return isTrustedEpoch(epoch_value);
}

bool writeRx8025Utc(time_t epoch_value) {
  if (!isTrustedEpoch(epoch_value)) {
    return false;
  }
  struct tm tm_utc {};
  if (gmtime_r(&epoch_value, &tm_utc) == nullptr) {
    return false;
  }

  Wire.beginTransmission(kRx8025Address);
  Wire.write(0x00);
  Wire.write(decToBcd(static_cast<uint8_t>(tm_utc.tm_sec)));
  Wire.write(decToBcd(static_cast<uint8_t>(tm_utc.tm_min)));
  Wire.write(decToBcd(static_cast<uint8_t>(tm_utc.tm_hour)));
  Wire.write(static_cast<uint8_t>(1u << tm_utc.tm_wday));
  Wire.write(decToBcd(static_cast<uint8_t>(tm_utc.tm_mday)));
  Wire.write(decToBcd(static_cast<uint8_t>(tm_utc.tm_mon + 1)));
  Wire.write(decToBcd(static_cast<uint8_t>((tm_utc.tm_year + 1900) % 100)));
  return Wire.endTransmission() == 0;
}

String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == '\\') {
      out += "\\\\";
    } else if (c == '"') {
      out += "\\\"";
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else if (c < 0x20u) {
      char escaped[7] = {0};
      snprintf(escaped, sizeof(escaped), "\\u%04X", static_cast<unsigned>(c));
      out += escaped;
    } else {
      out += static_cast<char>(c);
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

bool parseYmdDate(const String &raw, int &year, int &month, int &day) {
  if (raw.length() != 10 || raw.charAt(4) != '-' || raw.charAt(7) != '-') {
    return false;
  }
  year = raw.substring(0, 4).toInt();
  month = raw.substring(5, 7).toInt();
  day = raw.substring(8, 10).toInt();
  return year >= 2020 && month >= 1 && month <= 12 && day >= 1 && day <= daysInMonth(year, month);
}

bool parseFixedDigits(const String &raw, int start, int count, int &value) {
  if (start < 0 || count <= 0 || start + count > static_cast<int>(raw.length())) {
    return false;
  }
  int parsed = 0;
  for (int i = 0; i < count; ++i) {
    const char c = raw[start + i];
    if (c < '0' || c > '9') {
      return false;
    }
    parsed = parsed * 10 + (c - '0');
  }
  value = parsed;
  return true;
}

bool parseWeatherLocalTimeEpoch(const String &raw, int32_t utc_offset_seconds,
                                time_t &epoch_value) {
  epoch_value = 0;
  if (raw.length() < 16 || raw.charAt(4) != '-' || raw.charAt(7) != '-' ||
      raw.charAt(10) != 'T' || raw.charAt(13) != ':') {
    return false;
  }
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (!parseFixedDigits(raw, 0, 4, year) || !parseFixedDigits(raw, 5, 2, month) ||
      !parseFixedDigits(raw, 8, 2, day) || !parseFixedDigits(raw, 11, 2, hour) ||
      !parseFixedDigits(raw, 14, 2, minute)) {
    return false;
  }
  if (raw.length() >= 19) {
    if (raw.charAt(16) != ':' || !parseFixedDigits(raw, 17, 2, second)) {
      return false;
    }
  }
  if (year < 2024 || month < 1 || month > 12 || day < 1 || day > daysInMonth(year, month) ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
    return false;
  }

  struct tm local_tm {};
  local_tm.tm_year = year - 1900;
  local_tm.tm_mon = month - 1;
  local_tm.tm_mday = day;
  local_tm.tm_hour = hour;
  local_tm.tm_min = minute;
  local_tm.tm_sec = second;
  const time_t local_as_utc = epochFromUtcTm(local_tm);
  epoch_value = local_as_utc - static_cast<time_t>(utc_offset_seconds);
  return isTrustedEpoch(epoch_value);
}

bool setSystemClockFromEpoch(time_t epoch_value, const char *source) {
  if (!isTrustedEpoch(epoch_value)) {
    return false;
  }
  timeval tv {};
  tv.tv_sec = epoch_value;
  tv.tv_usec = 0;
  if (settimeofday(&tv, nullptr) != 0) {
    Serial.printf("[TIME] settimeofday failed source=%s\n", source ? source : "unknown");
    return false;
  }
  Serial.printf("[TIME] system clock set source=%s epoch=%lu\n",
                source ? source : "unknown",
                static_cast<unsigned long>(epoch_value));
  return true;
}

time_t localEpochFromYmdHm(const String &date, const String &time_hhmm) {
  int year = 0;
  int month = 0;
  int day = 0;
  if (!parseYmdDate(date, year, month, day)) {
    return 0;
  }
  uint16_t minute = 0;
  if (!parseMinuteHm(time_hhmm, minute)) {
    return 0;
  }
  struct tm tm_value {};
  tm_value.tm_year = year - 1900;
  tm_value.tm_mon = month - 1;
  tm_value.tm_mday = day;
  tm_value.tm_hour = static_cast<int>(minute / 60u);
  tm_value.tm_min = static_cast<int>(minute % 60u);
  tm_value.tm_sec = 0;
  tm_value.tm_isdst = -1;
  return mktime(&tm_value);
}

time_t startOfLocalDay(time_t epoch_value, struct tm &local_tm) {
  if (localtime_r(&epoch_value, &local_tm) == nullptr) {
    memset(&local_tm, 0, sizeof(local_tm));
    return 0;
  }
  local_tm.tm_hour = 0;
  local_tm.tm_min = 0;
  local_tm.tm_sec = 0;
  local_tm.tm_isdst = -1;
  return mktime(&local_tm);
}

bool nextManualEventEpoch(const CalendarEvent &event, time_t now_epoch, time_t &next_epoch,
                          String &occurrence_date) {
  next_epoch = 0;
  occurrence_date = event.date;
  if (event.source == "ics") {
    return false;
  }
  uint16_t minute = 0;
  if (!parseMinuteHm(event.time_hhmm, minute)) {
    return false;
  }
  if (event.repeat == "once") {
    next_epoch = localEpochFromYmdHm(event.date, event.time_hhmm);
    occurrence_date = event.date;
    return next_epoch > 0 && (now_epoch <= 0 || next_epoch >= now_epoch);
  }

  if (now_epoch <= 0) {
    next_epoch = 1;
    occurrence_date = event.date;
    return true;
  }

  struct tm day_tm {};
  time_t day_start = startOfLocalDay(now_epoch, day_tm);
  if (day_start <= 0) {
    return false;
  }

  uint8_t days_ahead = 0;
  if (event.repeat == "weekly") {
    if (event.weekday < 0 || event.weekday > 6) {
      return false;
    }
    const int today_weekday = (day_tm.tm_wday + 6) % 7;
    days_ahead = static_cast<uint8_t>((event.weekday - today_weekday + 7) % 7);
  } else if (event.repeat != "daily") {
    return false;
  }

  next_epoch = day_start + static_cast<time_t>(days_ahead) * 86400L +
               static_cast<time_t>(minute) * 60L;
  if (next_epoch < now_epoch) {
    next_epoch += (event.repeat == "weekly") ? static_cast<time_t>(7 * 86400L)
                                             : static_cast<time_t>(86400L);
  }
  struct tm next_tm {};
  if (localtime_r(&next_epoch, &next_tm) != nullptr) {
    occurrence_date = formatDateYmd(next_tm);
  }
  return true;
}

struct UpcomingCalendarEvent {
  CalendarEvent event;
  time_t epoch = 0;
  uint16_t order = 0;
};

bool upcomingEventLess(const UpcomingCalendarEvent &a, const UpcomingCalendarEvent &b) {
  if (a.epoch != b.epoch) {
    return a.epoch < b.epoch;
  }
  if (a.event.title != b.event.title) {
    return a.event.title < b.event.title;
  }
  return a.order < b.order;
}

void appendCalendarEventJson(String &item, const CalendarEvent &event) {
  item += "{\"id\":";
  item += String(event.id);
  item += ",\"title\":\"";
  item += jsonEscape(event.title);
  item += "\",\"date\":\"";
  item += jsonEscape(event.date);
  item += "\",\"time\":\"";
  item += jsonEscape(event.time_hhmm);
  item += "\",\"end_time\":\"";
  item += jsonEscape(event.end_time_hhmm);
  item += "\",\"color\":\"";
  item += jsonEscape(event.color);
  item += "\",\"repeat\":\"";
  item += jsonEscape(event.repeat);
  item += "\",\"weekday\":";
  item += String(event.weekday);
  item += ",\"source\":\"";
  item += jsonEscape(event.source);
  item += "\",\"external_id\":\"";
  item += jsonEscape(event.external_id);
  item += "\",\"updated_at\":\"";
  item += jsonEscape(event.updated_at);
  item += "\"}";
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

String extractJsonStringFieldAfter(const String &json, const char *anchor, const char *key) {
  if (anchor == nullptr || key == nullptr || anchor[0] == '\0' || key[0] == '\0') {
    return "";
  }
  const int anchor_pos = json.indexOf(anchor);
  if (anchor_pos < 0) {
    return "";
  }
  const String token = String("\"") + key + "\":\"";
  const int start = json.indexOf(token, anchor_pos + static_cast<int>(strlen(anchor)));
  if (start < 0) {
    return "";
  }

  String out;
  out.reserve(32);
  bool escaping = false;
  for (int i = start + static_cast<int>(token.length()); i < static_cast<int>(json.length()); ++i) {
    const char c = json[i];
    if (escaping) {
      out += c;
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

bool extractJsonIntField(const String &json, const char *key, int32_t &value) {
  value = 0;
  if (key == nullptr || key[0] == '\0') {
    return false;
  }
  const String token = String("\"") + key + "\":";
  int search_start = 0;
  while (search_start < static_cast<int>(json.length())) {
    const int start = json.indexOf(token, search_start);
    if (start < 0) {
      return false;
    }

    int pos = start + static_cast<int>(token.length());
    while (pos < static_cast<int>(json.length()) &&
           (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
      ++pos;
    }
    if (pos >= static_cast<int>(json.length())) {
      return false;
    }

    int end = pos;
    if (json[end] == '-') {
      ++end;
    }
    bool saw_digit = false;
    while (end < static_cast<int>(json.length()) && isDigit(json[end])) {
      saw_digit = true;
      ++end;
    }
    if (saw_digit) {
      value = static_cast<int32_t>(json.substring(pos, end).toInt());
      return true;
    }
    search_start = pos + 1;
  }
  return false;
}

String posixTimezoneFromUtcOffsetSeconds(int32_t utc_offset_seconds) {
  const long posix_offset = static_cast<long>(-utc_offset_seconds);
  const long abs_offset = labs(posix_offset);
  const long hours = abs_offset / 3600L;
  const long minutes = (abs_offset % 3600L) / 60L;
  const long seconds = abs_offset % 60L;

  String tz = "UTC";
  if (posix_offset > 0) {
    tz += "+";
  } else if (posix_offset < 0) {
    tz += "-";
  } else {
    tz += "0";
    return tz;
  }
  tz += String(hours);
  if (minutes != 0 || seconds != 0) {
    if (minutes < 10) tz += ":0";
    else tz += ":";
    tz += String(minutes);
    if (seconds != 0) {
      if (seconds < 10) tz += ":0";
      else tz += ":";
      tz += String(seconds);
    }
  }
  return tz;
}

String timezoneForEsp(const String &timezone_name, bool has_utc_offset, int32_t utc_offset_seconds) {
  if (has_utc_offset) {
    return posixTimezoneFromUtcOffsetSeconds(utc_offset_seconds);
  }

  String tz = timezone_name;
  tz.trim();
  if (tz.length() == 0) {
    return "";
  }
  if (tz == "Asia/Shanghai") return "CST-8";
  if (tz == "UTC" || tz == "Etc/UTC" || tz == "GMT") return "UTC0";
  if (tz.indexOf('/') >= 0) {
    return "";
  }
  return tz;
}

String openMeteoUrlForCoordinates(const char *lat, const char *lon) {
  String url = "http://api.open-meteo.com/v1/forecast?latitude=";
  url += lat;
  url += "&longitude=";
  url += lon;
  url += "&current=temperature_2m,relative_humidity_2m,weather_code&timezone=auto";
  return url;
}

bool isBeijingWeatherCity(const String &city) {
  String normalized = city;
  normalized.trim();
  normalized.toLowerCase();
  return normalized == "beijing" || normalized == "beijing, china" ||
         normalized.indexOf(u8"北京") >= 0;
}

bool applyKnownWeatherLocationDefaults(WifiSettings &settings) {
  if (!isBeijingWeatherCity(settings.weather_city)) {
    return false;
  }
  const String beijing_url = openMeteoUrlForCoordinates("39.9042", "116.4074");
  bool changed = false;
  if (settings.weather_lat != "39.9042") {
    settings.weather_lat = "39.9042";
    changed = true;
  }
  if (settings.weather_lon != "116.4074") {
    settings.weather_lon = "116.4074";
    changed = true;
  }
  if (settings.weather_url != beijing_url) {
    settings.weather_url = beijing_url;
    changed = true;
  }
  if (settings.weather_location_city != settings.weather_city) {
    settings.weather_location_city = settings.weather_city;
    changed = true;
  }
  if (settings.timezone != "Asia/Shanghai") {
    settings.timezone = "Asia/Shanghai";
    changed = true;
  }
  return changed;
}

bool syncClockWithTimezone(const String &timezone, String &local_time, String &error_msg) {
  local_time = "";
  error_msg = "";
  if (timezone.length() == 0) {
    error_msg = "empty_timezone";
    return false;
  }

  const time_t before_sync = time(nullptr);
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  configTzTime(timezone.c_str(), "ntp.aliyun.com", "time.cloudflare.com", "pool.ntp.org");
  constexpr time_t kMinValidEpoch = 1700000000;  // About 2023-11.
  const uint32_t start_ms = millis();
  sntp_sync_status_t last_status = SNTP_SYNC_STATUS_RESET;
  Serial.printf("[TIME] ntp sync start tz=%s before_epoch=%ld\n",
                timezone.c_str(), static_cast<long>(before_sync));
  while ((millis() - start_ms) < 10000) {
    const time_t now_ts = time(nullptr);
    const sntp_sync_status_t status = sntp_get_sync_status();
    if (status != last_status) {
      if (kDebugLogs) {
        Serial.printf("[TIME] ntp status=%s epoch=%ld\n",
                      sntpSyncStatusName(status), static_cast<long>(now_ts));
      }
      last_status = status;
    }
    if (status == SNTP_SYNC_STATUS_COMPLETED && now_ts >= kMinValidEpoch) {
      struct tm tm_local {};
      if (localtime_r(&now_ts, &tm_local) == nullptr) {
        sntp_stop();
        error_msg = "localtime_failed";
        return false;
      }
      char buf[40] = {0};
      if (strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %z", &tm_local) > 0) {
        local_time = String(buf);
      } else {
        local_time = String(static_cast<unsigned long>(now_ts));
      }
      Serial.printf("[TIME] ntp sync completed epoch=%ld local=%s\n",
                    static_cast<long>(now_ts), local_time.c_str());
      sntp_stop();
      return true;
    }
    delay(200);
  }
  error_msg = "ntp_timeout_status_";
  error_msg += sntpSyncStatusName(sntp_get_sync_status());
  error_msg += "_epoch_";
  error_msg += String(static_cast<long>(time(nullptr)));
  sntp_stop();
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
      if (kDebugLogs) {
        Serial.println("[WIFI][EVT] STA_START");
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
      if (kDebugLogs) {
        Serial.println("[WIFI][EVT] STA_STOP");
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      if (kDebugLogs) {
        Serial.printf("[WIFI][EVT] STA_CONNECTED ssid=%s channel=%d auth=%d\n",
                      reinterpret_cast<const char *>(info.wifi_sta_connected.ssid),
                      static_cast<int>(info.wifi_sta_connected.channel),
                      static_cast<int>(info.wifi_sta_connected.authmode));
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (kDebugLogs) {
        Serial.printf("[WIFI][EVT] STA_DISCONNECTED reason=%u (%s) ssid=%s\n",
                      static_cast<unsigned>(info.wifi_sta_disconnected.reason),
                      disconnectReasonName(info.wifi_sta_disconnected.reason),
                      reinterpret_cast<const char *>(info.wifi_sta_disconnected.ssid));
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      if (kDebugLogs) {
        Serial.printf("[WIFI][EVT] STA_GOT_IP ip=%s gw=%s mask=%s\n",
                      IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str(),
                      IPAddress(info.got_ip.ip_info.gw.addr).toString().c_str(),
                      IPAddress(info.got_ip.ip_info.netmask.addr).toString().c_str());
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      if (kDebugLogs) {
        Serial.println("[WIFI][EVT] STA_LOST_IP");
      }
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      ap_client_connected_ = true;
      Serial.println("[WIFI][EVT] AP_CLIENT_CONNECTED");
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      if (kDebugLogs) {
        Serial.println("[WIFI][EVT] AP_CLIENT_DISCONNECTED");
      }
      break;
    default:
      if (kDebugLogs) {
        Serial.printf("[WIFI][EVT] %s (%d)\n", wifiEventName(event), static_cast<int>(event));
      }
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
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      return "WIFI_AP_STACONNECTED";
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      return "WIFI_AP_STADISCONNECTED";
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      return "WIFI_AP_STAIPASSIGNED";
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
  if (!kDebugLogs) {
    return;
  }
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
  initWebFs();
  loadCalendarMonthCache();
  stop("boot");
}

void WifiManager::update(uint32_t now_ms) {
  updateSensors(now_ms);
  pruneExpiredCalendarEvents();

  if (server_ != nullptr && (isApSessionActive() || state_ == State::StaRunning)) {
    server_->handleClient();
  }

  if (state_ == State::StaConnecting) {
    const int status = WiFi.status();
    if (status != last_sta_wifi_status_) {
      Serial.printf("[WIFI] STA status -> %s (%d)\n", wifiStatusName(status), status);
      last_sta_wifi_status_ = status;
    }
    const bool ap_background_terminal_failure =
        sta_session_role_ == StaSessionRole::ApBackground &&
        (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED);
    if (ap_background_terminal_failure) {
      Serial.printf("[WIFI] AP background STA single attempt failed status=%s/%d -> keep AP\n",
                    wifiStatusName(status), status);
      sta_connect_failed_ = true;
      stopStaOnly("ap_background_terminal_failure_keep_ap");
      return;
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
      if (sta_session_role_ == StaSessionRole::ManualConfig) {
        manual_sta_sync_settled_ = true;
        calendar_sync_pending_ = false;
        last_calendar_sync_ms_ = 0;
        return;
      }
      if (sta_session_role_ == StaSessionRole::ApBackground) {
        calendar_sync_pending_ = false;
        last_calendar_sync_ms_ = 0;
        Serial.println("[WIFI] AP background STA connected");
        return;
      }
      if (sta_session_role_ == StaSessionRole::StatusProbe) {
        calendar_sync_pending_ = false;
        last_calendar_sync_ms_ = 0;
        Serial.println("[WIFI] STA status probe connected; network sync deferred");
        return;
      }
      String resolved_timezone;
      String local_time;
      String sync_error;
      String preview;
      String request_error;
      bool timezone_updated = false;
      int http_status = 0;
      const bool request_ntp =
          sta_session_role_ == StaSessionRole::DailyRefresh ||
          sta_session_role_ == StaSessionRole::AutoSync;
      const bool clock_was_trusted = systemClockTrusted();
      const bool weather_location_ready =
          refreshWeatherLocationFromCity("sta_connected_pre_refresh");
      if (!weather_location_ready) {
        request_error = "city_resolve_failed";
      }
      if (weather_location_ready &&
          syncClockFromWeather(resolved_timezone, timezone_updated, local_time, sync_error, preview,
                               http_status, request_error, request_ntp)) {
        Serial.printf("[TIME] synced tz=%s local=%s\n",
                      resolved_timezone.c_str(), local_time.c_str());
        if (request_ntp || !clock_was_trusted) writeClockToRtc("weather_sync");
      } else {
        const String effective_tz = timezoneForEsp(settings_.timezone, false, 0);
        if ((request_ntp || !systemClockTrusted()) &&
            syncClockWithTimezone(effective_tz, local_time, sync_error)) {
          Serial.printf("[TIME] fallback synced tz=%s local=%s\n",
                        effective_tz.c_str(), local_time.c_str());
          writeClockToRtc("fallback_ntp");
        } else {
          Serial.printf("[TIME] sync failed weather_err=%s tz=%s err=%s\n",
                        request_error.c_str(), effective_tz.c_str(), sync_error.c_str());
        }
      }
      calendar_sync_pending_ = true;
      last_calendar_sync_ms_ = 0;
    } else if ((now_ms - sta_connect_start_ms_) >= sta_connect_timeout_ms_) {
      Serial.printf("[WIFI] STA connect timeout -> stop (last_status=%s/%d)\n",
                    wifiStatusName(status), status);
      sta_connect_failed_ = true;
      const bool status_probe = sta_session_role_ == StaSessionRole::StatusProbe;
      if (isApSessionActive()) {
        stopStaOnly("sta_connect_timeout_keep_ap");
      } else {
        stop(status_probe ? "sta_status_probe_timeout" : "sta_connect_timeout");
        if (!status_probe) {
          auto_exit_requested_ = true;
        }
      }
    }
    return;
  }

  if (state_ == State::StaRunning && WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] STA lost connection -> stop");
    sta_connect_failed_ = true;
    const bool status_probe = sta_session_role_ == StaSessionRole::StatusProbe;
    if (sta_session_role_ == StaSessionRole::ManualConfig) {
      cleanupDisconnectedStaSession("manual_sta_lost_connection");
    } else if (isApSessionActive()) {
      stopStaOnly("sta_lost_connection_keep_ap");
    } else {
      stop(status_probe ? "sta_status_probe_lost_connection" : "sta_lost_connection");
      if (!status_probe) {
        auto_exit_requested_ = true;
      }
    }
    return;
  }

  if (state_ == State::StaRunning) {
    maybeSyncCalendarUrl(now_ms);
  }

  // HTTP handlers and network syncs can call markActivity(millis()) after the
  // caller captured now_ms. Use a fresh snapshot so timeout subtraction cannot
  // underflow when the activity timestamp is a few milliseconds newer.
  updateTimeout(millis());
}

void WifiManager::startAP() {
  stop("switch_to_ap");
  digitalWrite(kPeripheralPowerPin, HIGH);
  delay(3);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname(kDefaultHostname);
  const bool ok = WiFi.softAP(kDefaultApSsid, kDefaultApPass);
  if (!ok) {
    Serial.println("[WIFI] AP start failed");
    state_ = State::Idle;
    ap_active_ = false;
    return;
  }

  ap_active_ = true;
  state_ = State::ApRunning;
  markActivity(millis());
  startServer();
  initSD();
  Serial.printf("[WIFI] AP started ssid=%s ip=%s timeout=%lus\n", kDefaultApSsid,
                WiFi.softAPIP().toString().c_str(),
                static_cast<unsigned long>(kApIdleTimeoutMs / 1000));
  Serial.println("[WIFI] portal url: http://192.168.4.1/");
  if (hasStaCredentials()) {
    startSTAWithTimeout(kStaConnectTimeoutManualMs, "ap_config_background");
  } else {
    Serial.println("[WIFI] AP config mode: no STA credentials saved");
  }
}

void WifiManager::startSTA() {
  startSTAWithTimeout(kStaConnectTimeoutManualMs, "manual");
}

void WifiManager::startStaAutoSync() {
  startSTAWithTimeout(kStaConnectTimeoutAutoSyncMs, "auto_sync");
}

void WifiManager::startStaDailySync() {
  Serial.println("[SYNC] daily refresh sync required");
  startSTAWithTimeout(kStaConnectTimeoutAutoSyncMs, "daily_refresh");
}

void WifiManager::startStaStatusProbe() {
  startSTAWithTimeout(kStaConnectTimeoutAutoSyncMs, "calendar_probe");
}

void WifiManager::startStaBackgroundSync() {
  startSTAWithTimeout(kStaConnectTimeoutAutoSyncMs, "calendar_background");
}

String WifiManager::effectiveStaAuthMode() const {
  String mode = settings_.sta_auth_mode;
  mode.trim();
  mode.toLowerCase();
  if (mode != "auto") {
    return mode;
  }
  if (settings_.sta_user.length() > 0) {
    return "enterprise";
  }
  if (settings_.sta_pass.length() > 0) {
    return "personal";
  }
  return "open";
}

bool WifiManager::beginStaConnection() {
  const String mode = effectiveStaAuthMode();
  if (mode == "enterprise") {
    if (settings_.sta_user.length() == 0 || settings_.sta_pass.length() == 0) {
      Serial.println("[WIFI] enterprise auth requires account and password");
      return false;
    }
    const unsigned char *identity =
        reinterpret_cast<const unsigned char *>(settings_.sta_user.c_str());
    const unsigned char *password =
        reinterpret_cast<const unsigned char *>(settings_.sta_pass.c_str());
    esp_wifi_sta_wpa2_ent_clear_identity();
    esp_wifi_sta_wpa2_ent_clear_username();
    esp_wifi_sta_wpa2_ent_clear_password();
    esp_wifi_sta_wpa2_ent_set_identity(identity, settings_.sta_user.length());
    esp_wifi_sta_wpa2_ent_set_username(identity, settings_.sta_user.length());
    esp_wifi_sta_wpa2_ent_set_password(password, settings_.sta_pass.length());
    const bool trusted_clock = systemClockTrusted();
    esp_wifi_sta_wpa2_ent_set_disable_time_check(!trusted_clock);
    Serial.printf("[WIFI] enterprise cert time check=%s\n",
                  trusted_clock ? "enabled" : "disabled_until_time_sync");
    const esp_err_t err = esp_wifi_sta_wpa2_ent_enable();
    if (err != ESP_OK) {
      Serial.printf("[WIFI] enterprise enable failed err=%d\n", static_cast<int>(err));
      return false;
    }
    WiFi.begin(settings_.sta_ssid.c_str());
    Serial.printf("[WIFI] STA enterprise begin user=%s\n", settings_.sta_user.c_str());
    return true;
  }

  esp_wifi_sta_wpa2_ent_disable();
  if (mode == "open") {
    WiFi.begin(settings_.sta_ssid.c_str());
    Serial.println("[WIFI] STA open begin");
    return true;
  }
  WiFi.begin(settings_.sta_ssid.c_str(), settings_.sta_pass.c_str());
  Serial.println("[WIFI] STA personal begin");
  return true;
}

bool WifiManager::systemClockTrusted() const {
  return rtc_time_trusted_ || isTrustedEpoch(time(nullptr));
}

void WifiManager::restoreClockFromRtc() {
  time_t rtc_epoch = 0;
  if (!readRx8025Utc(rtc_epoch)) {
    rtc_time_trusted_ = false;
    Serial.println("[RTC] RX8025 read failed or time not trusted");
    return;
  }
  timeval tv {};
  tv.tv_sec = rtc_epoch;
  tv.tv_usec = 0;
  if (settimeofday(&tv, nullptr) != 0) {
    rtc_time_trusted_ = false;
    Serial.println("[RTC] settimeofday failed");
    return;
  }
  rtc_time_trusted_ = true;
  Serial.printf("[RTC] restored UTC epoch=%lu\n", static_cast<unsigned long>(rtc_epoch));
}

void WifiManager::writeClockToRtc(const char *reason) {
  const time_t now_epoch = time(nullptr);
  if (!isTrustedEpoch(now_epoch)) {
    Serial.printf("[RTC] skip write reason=%s untrusted_epoch=%ld\n",
                  reason ? reason : "unknown", static_cast<long>(now_epoch));
    return;
  }
  if (writeRx8025Utc(now_epoch)) {
    rtc_time_trusted_ = true;
    Serial.printf("[RTC] wrote UTC epoch=%lu reason=%s\n",
                  static_cast<unsigned long>(now_epoch),
                  reason ? reason : "unknown");
  } else {
    Serial.printf("[RTC] write failed reason=%s\n", reason ? reason : "unknown");
  }
}

void WifiManager::cleanupDisconnectedStaSession(const char *reason) {
  Serial.printf("[WIFI] cleanup disconnected STA reason=%s\n", reason ? reason : "none");
  stopServer();
  deinitSD();
  MDNS.end();
  esp_wifi_sta_wpa2_ent_disable();
  esp_wifi_sta_wpa2_ent_clear_identity();
  esp_wifi_sta_wpa2_ent_clear_username();
  esp_wifi_sta_wpa2_ent_clear_password();
  state_ = isApSessionActive() ? State::ApRunning : State::Idle;
  sta_connect_start_ms_ = 0;
  sta_connect_timeout_ms_ = 0;
  sta_session_start_ms_ = 0;
  calendar_sync_pending_ = false;
  last_calendar_sync_ms_ = 0;
  sta_manual_session_ = false;
  sta_session_role_ = StaSessionRole::None;
  last_sta_wifi_status_ = WL_IDLE_STATUS;
  if (!isApSessionActive()) {
    digitalWrite(kPeripheralPowerPin, LOW);
  }
}

void WifiManager::startSTAWithTimeout(uint32_t connect_timeout_ms, const char *reason_tag) {
  const bool keep_ap = isApSessionActive();
  const bool status_probe = reason_tag && strcmp(reason_tag, "calendar_probe") == 0;
  const bool ap_background_attempt =
      keep_ap && reason_tag && strcmp(reason_tag, "ap_config_background") == 0;
  if (!keep_ap) {
    stop("switch_to_sta");
  } else if (isStaActive()) {
    stopStaOnly("restart_sta_keep_ap");
  }
  digitalWrite(kPeripheralPowerPin, HIGH);
  delay(3);
  WiFi.mode(keep_ap ? WIFI_AP_STA : WIFI_STA);
  WiFi.setAutoReconnect(!ap_background_attempt);
  WiFi.setSleep(true);
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  WiFi.setHostname(kDefaultHostname);
  if (kWifiScanDiagLogs && keep_ap) {
    Serial.println("[WIFI] STA scan skipped reason=ap_active");
  } else if (kWifiScanDiagLogs) {
    logStaScanResults();
  } else if (kDebugLogs) {
    Serial.println("[WIFI] STA scan skipped reason=normal_log");
  }
  if (!beginStaConnection()) {
    sta_connect_failed_ = true;
    if (keep_ap) {
      stopStaOnly("sta_auth_config_invalid_keep_ap");
    } else {
      stop(status_probe ? "sta_status_probe_auth_invalid" : "sta_auth_config_invalid");
      if (!status_probe) {
        auto_exit_requested_ = true;
      }
    }
    return;
  }
  sta_connect_start_ms_ = millis();
  sta_connect_timeout_ms_ = connect_timeout_ms;
  sta_manual_session_ = !keep_ap && reason_tag && strcmp(reason_tag, "manual") == 0;
  if (reason_tag && strcmp(reason_tag, "manual") == 0) {
    sta_session_role_ = StaSessionRole::ManualConfig;
  } else if (reason_tag && strcmp(reason_tag, "ap_config_background") == 0) {
    sta_session_role_ = StaSessionRole::ApBackground;
  } else if (reason_tag && strcmp(reason_tag, "daily_refresh") == 0) {
    sta_session_role_ = StaSessionRole::DailyRefresh;
  } else if (reason_tag && strcmp(reason_tag, "calendar_probe") == 0) {
    sta_session_role_ = StaSessionRole::StatusProbe;
  } else if (reason_tag && strcmp(reason_tag, "calendar_background") == 0) {
    sta_session_role_ = StaSessionRole::CalendarBackground;
  } else if (reason_tag && strcmp(reason_tag, "auto_sync") == 0) {
    sta_session_role_ = StaSessionRole::AutoSync;
  } else {
    sta_session_role_ = StaSessionRole::None;
  }
  state_ = State::StaConnecting;
  last_sta_wifi_status_ = WiFi.status();
  Serial.printf("[WIFI] STA connecting reason=%s ssid=%s auth=%s user_len=%u pass_len=%u timeout=%lus initial_status=%s/%d\n",
                reason_tag ? reason_tag : "unknown",
                settings_.sta_ssid.c_str(),
                effectiveStaAuthMode().c_str(),
                static_cast<unsigned>(settings_.sta_user.length()),
                static_cast<unsigned>(settings_.sta_pass.length()),
                static_cast<unsigned long>(sta_connect_timeout_ms_ / 1000),
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
    esp_wifi_sta_wpa2_ent_disable();
    esp_wifi_sta_wpa2_ent_clear_identity();
    esp_wifi_sta_wpa2_ent_clear_username();
    esp_wifi_sta_wpa2_ent_clear_password();
    WiFi.mode(WIFI_OFF);
    delay(60);
  }
  digitalWrite(kPeripheralPowerPin, LOW);
  state_ = State::Idle;
  ap_active_ = false;
  sta_connect_start_ms_ = 0;
  sta_connect_timeout_ms_ = 0;
  sta_session_start_ms_ = 0;
  last_activity_ms_ = 0;
  last_calendar_sync_ms_ = 0;
  calendar_sync_pending_ = false;
  sta_manual_session_ = false;
  sta_session_role_ = StaSessionRole::None;
  last_sta_wifi_status_ = WL_IDLE_STATUS;
}

bool WifiManager::isApSessionActive() const {
  return ap_active_;
}

bool WifiManager::isStaActive() const {
  return state_ == State::StaConnecting || state_ == State::StaRunning;
}

void WifiManager::stopStaOnly(const char *reason) {
  if (!isStaActive()) {
    return;
  }
  Serial.printf("[WIFI] stop STA reason=%s\n", reason ? reason : "none");
  WiFi.disconnect(false, false);
  delay(60);
  esp_wifi_sta_wpa2_ent_disable();
  esp_wifi_sta_wpa2_ent_clear_identity();
  esp_wifi_sta_wpa2_ent_clear_username();
  esp_wifi_sta_wpa2_ent_clear_password();
  state_ = ap_active_ ? State::ApRunning : State::Idle;
  sta_connect_start_ms_ = 0;
  sta_connect_timeout_ms_ = 0;
  sta_session_start_ms_ = 0;
  calendar_sync_pending_ = false;
  last_calendar_sync_ms_ = 0;
  sta_session_role_ = StaSessionRole::None;
  last_sta_wifi_status_ = WL_IDLE_STATUS;
  if (!ap_active_) {
    stop("sta_only_stop_no_ap");
  }
}

bool WifiManager::consumeAutoExitRequested() {
  const bool value = auto_exit_requested_;
  auto_exit_requested_ = false;
  return value;
}

bool WifiManager::consumeSettingsApplyRefreshRequested() {
  const bool value = settings_apply_refresh_pending_;
  settings_apply_refresh_pending_ = false;
  return value;
}

bool WifiManager::consumeStaConnectFailed() {
  const bool value = sta_connect_failed_;
  sta_connect_failed_ = false;
  return value;
}

bool WifiManager::consumeManualStaSyncSettled() {
  const bool value = manual_sta_sync_settled_;
  manual_sta_sync_settled_ = false;
  return value;
}

bool WifiManager::consumeApClientConnected() {
  const bool value = ap_client_connected_;
  ap_client_connected_ = false;
  return value;
}

bool WifiManager::isStaConnecting() const {
  return state_ == State::StaConnecting;
}

bool WifiManager::isStaConnected() const {
  return state_ == State::StaRunning;
}

bool WifiManager::hasStaCredentials() const {
  if (settings_.sta_ssid.length() == 0) {
    return false;
  }
  const String mode = effectiveStaAuthMode();
  if (mode == "open") {
    return true;
  }
  if (mode == "enterprise") {
    return settings_.sta_user.length() > 0 && settings_.sta_pass.length() > 0;
  }
  return settings_.sta_pass.length() > 0;
}

bool WifiManager::isCalendarSyncBusy() const {
  return (state_ == State::StaConnecting) ||
         (state_ == State::StaRunning &&
          (calendar_sync_pending_ || last_calendar_sync_status_ == "running"));
}

bool WifiManager::blocksLightSleep() const {
  return state_ != State::Idle ||
         calendar_sync_pending_ ||
         last_calendar_sync_status_ == "running";
}

void WifiManager::requestCalendarSyncNow() {
  calendar_sync_pending_ = true;
  last_calendar_sync_ms_ = 0;
  if (last_calendar_sync_status_ != "running") {
    last_calendar_sync_status_ = "queued";
  }
  last_calendar_sync_error_ = "";
}

bool WifiManager::ensureLocalCalendarLoaded(time_t now_epoch, const char *reason) {
  if (!settings_.calendar_url.startsWith("/") || now_epoch <= 0) {
    return false;
  }
  if (!settings_.calendar_enabled) {
    settings_.calendar_enabled = true;
  }
  struct tm local_tm {};
  if (localtime_r(&now_epoch, &local_tm) == nullptr) {
    return false;
  }
  const int32_t month_key = static_cast<int32_t>((local_tm.tm_year + 1900) * 100 +
                                                (local_tm.tm_mon + 1));
  if (calendar_month_summary_month_key_ == month_key &&
      calendar_month_summary_source_ == settings_.calendar_url &&
      last_calendar_sync_status_ == "ok") {
    return true;
  }

  Serial.printf("[CALSYNC] local ensure reason=%s month=%ld url=%s\n",
                reason ? reason : "local",
                static_cast<long>(month_key),
                settings_.calendar_url.c_str());
  last_calendar_sync_status_ = "running";
  last_calendar_sync_error_ = "";
  String error_msg;
  if (!syncCalendarFromUrl(error_msg, now_epoch)) {
    last_calendar_sync_status_ = "error";
    last_calendar_sync_error_ = error_msg;
    Serial.printf("[CALSYNC] local ensure failed err=%s url=%s\n", error_msg.c_str(),
                  settings_.calendar_url.c_str());
    return false;
  }
  return true;
}

bool WifiManager::syncCalendarNow(const char *reason) {
  if (state_ != State::StaRunning || WiFi.status() != WL_CONNECTED) {
    Serial.printf("[CALSYNC] skipped reason=%s sta_not_connected\n",
                  reason ? reason : "manual");
    return false;
  }
  if (!settings_.calendar_enabled) {
    return false;
  }
  const bool is_remote_url =
      settings_.calendar_url.startsWith("http://") || settings_.calendar_url.startsWith("https://");
  const bool is_local_path = settings_.calendar_url.startsWith("/");
  if (!is_remote_url && !is_local_path) {
    if (settings_.calendar_url.length() == 0) {
      Serial.println("[CALSYNC] skip reason=empty_url");
    } else {
      Serial.printf("[CALSYNC] skip unsupported source=%s\n", settings_.calendar_url.c_str());
    }
    return false;
  }

  Serial.printf("[CALSYNC] trigger reason=%s url=%s\n",
                reason ? reason : "manual",
                settings_.calendar_url.c_str());
  calendar_sync_pending_ = false;
  last_calendar_sync_ms_ = millis();
  last_calendar_sync_status_ = "running";
  last_calendar_sync_error_ = "";
  String error_msg;
  if (!syncCalendarFromUrl(error_msg)) {
    last_calendar_sync_status_ = "error";
    last_calendar_sync_error_ = error_msg;
    Serial.printf("[CALSYNC] failed err=%s url=%s\n", error_msg.c_str(),
                  settings_.calendar_url.c_str());
    return false;
  }
  return true;
}

bool WifiManager::fetchWeatherCityCoordinates(const String &city_value, String &resolved_name,
                                              String &lat, String &lon, String &weather_url,
                                              String &error_msg) {
  resolved_name = "";
  lat = "";
  lon = "";
  weather_url = "";
  error_msg = "";
  if (state_ != State::StaRunning || WiFi.status() != WL_CONNECTED) {
    error_msg = "sta_required";
    return false;
  }

  String city = city_value;
  city.trim();
  if (city.length() == 0u) {
    error_msg = "empty_city";
    return false;
  }

  String url = "https://geocoding-api.open-meteo.com/v1/search?count=1&language=zh&format=json&name=";
  url += urlEncode(city);
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  if (!http.begin(url)) {
    error_msg = "http_begin_failed";
    return false;
  }
  const int code = http.GET();
  if (code != 200) {
    http.end();
    error_msg = "geocode_http_failed";
    return false;
  }
  String body;
  const bool body_ok = readHttpBodyBounded(http, kGeocodeResponseMaxBytes, body);
  http.end();
  if (!body_ok) {
    error_msg = "geocode_response_too_large";
    return false;
  }

  const int results_idx = body.indexOf("\"results\":[");
  if (results_idx < 0) {
    error_msg = "city_not_found";
    return false;
  }
  const int name_key = body.indexOf("\"name\":\"", results_idx);
  const int lat_key = body.indexOf("\"latitude\":", results_idx);
  const int lon_key = body.indexOf("\"longitude\":", results_idx);
  if (name_key < 0 || lat_key < 0 || lon_key < 0) {
    error_msg = "geocode_parse_failed";
    return false;
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
    error_msg = "geocode_parse_failed";
    return false;
  }

  resolved_name = body.substring(name_start, name_end);
  lat = body.substring(lat_start, lat_end);
  lon = body.substring(lon_start, lon_end);
  lat.trim();
  lon.trim();
  if (lat.length() == 0u || lon.length() == 0u) {
    error_msg = "geocode_parse_failed";
    return false;
  }
  weather_url = "http://api.open-meteo.com/v1/forecast?latitude=" + lat +
                "&longitude=" + lon +
                "&current=temperature_2m,relative_humidity_2m,weather_code&timezone=auto";
  return true;
}

bool WifiManager::refreshWeatherLocationFromCity(const char *reason) {
  String requested_city = settings_.weather_city;
  requested_city.trim();
  String cached_city = settings_.weather_location_city;
  cached_city.trim();
  const bool cached_coordinates_valid =
      settings_.weather_lat.length() > 0u && settings_.weather_lon.length() > 0u &&
      (settings_.weather_url.startsWith("http://") ||
       settings_.weather_url.startsWith("https://"));
  if (cached_coordinates_valid && requested_city.equalsIgnoreCase(cached_city)) {
    Serial.printf("[WEATHER] city coordinates cached reason=%s city=%s lat=%s lon=%s\n",
                  reason ? reason : "unknown", requested_city.c_str(),
                  settings_.weather_lat.c_str(), settings_.weather_lon.c_str());
    return true;
  }

  String resolved_name;
  String lat;
  String lon;
  String weather_url;
  String error_msg;
  if (!fetchWeatherCityCoordinates(settings_.weather_city, resolved_name, lat, lon, weather_url,
                                   error_msg)) {
    Serial.printf("[WEATHER] city resolve failed reason=%s city=%s err=%s\n",
                  reason ? reason : "unknown", settings_.weather_city.c_str(),
                  error_msg.c_str());
    return false;
  }
  const bool changed = settings_.weather_lat != lat || settings_.weather_lon != lon ||
                       settings_.weather_url != weather_url ||
                       settings_.weather_location_city != requested_city;
  settings_.weather_location_city = requested_city;
  settings_.weather_lat = lat;
  settings_.weather_lon = lon;
  settings_.weather_url = weather_url;
  if (changed && !saveSettings()) {
    Serial.printf("[WEATHER] city resolve save failed reason=%s city=%s\n",
                  reason ? reason : "unknown", settings_.weather_city.c_str());
    return false;
  }
  Serial.printf("[WEATHER] city resolved reason=%s city=%s result=%s lat=%s lon=%s saved=%s\n",
                reason ? reason : "unknown", settings_.weather_city.c_str(),
                resolved_name.c_str(), lat.c_str(), lon.c_str(), changed ? "true" : "false");
  return true;
}

bool WifiManager::syncWeatherNow(const char *reason, bool request_ntp) {
  if (state_ != State::StaRunning || WiFi.status() != WL_CONNECTED) {
    Serial.printf("[WEATHER] sync skipped reason=%s sta_not_connected\n",
                  reason ? reason : "manual");
    return false;
  }
  if (!refreshWeatherLocationFromCity(reason ? reason : "weather_sync")) {
    return false;
  }
  String resolved_timezone;
  bool timezone_updated = false;
  String local_time;
  String time_sync_error;
  String preview;
  int http_status = 0;
  String request_error;
  const bool clock_was_trusted = systemClockTrusted();
  const bool ok = syncClockFromWeather(resolved_timezone, timezone_updated, local_time,
                                       time_sync_error, preview, http_status, request_error,
                                       request_ntp);
  if (ok) {
    if (request_ntp || !clock_was_trusted) {
      writeClockToRtc(reason ? reason : "weather_sync");
    }
    Serial.printf("[WEATHER] sync ok reason=%s code=%d city=%s\n",
                  reason ? reason : "manual", weather_code_, settings_.weather_city.c_str());
  } else {
    Serial.printf("[WEATHER] sync failed reason=%s status=%d err=%s code=%d\n",
                  reason ? reason : "manual", http_status, request_error.c_str(), weather_code_);
  }
  return ok;
}

const WifiManager::Settings &WifiManager::settings() const {
  return settings_;
}

bool WifiManager::syncClockFromWeather(String &resolved_timezone, bool &timezone_updated,
                                       String &local_time, String &time_sync_error, String &preview,
                                       int &http_status, String &request_error, bool request_ntp) {
  resolved_timezone = "";
  timezone_updated = false;
  local_time = "";
  time_sync_error = "";
  preview = "";
  http_status = 0;
  request_error = "";

  if (!(settings_.weather_url.startsWith("http://") || settings_.weather_url.startsWith("https://"))) {
    request_error = "bad_weather_url";
    return false;
  }

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  if (!http.begin(settings_.weather_url)) {
    request_error = "http_begin_failed";
    return false;
  }

  http_status = http.GET();
  if (http_status <= 0) {
    http.end();
    request_error = "weather_request_failed";
    return false;
  }

  String body;
  const bool body_ok = readHttpBodyBounded(http, kWeatherResponseMaxBytes, body);
  http.end();
  if (!body_ok) {
    request_error = "weather_response_too_large";
    return false;
  }

  preview = body;
  preview.replace("\r", " ");
  preview.replace("\n", " ");
  if (preview.length() > 240) {
    preview = preview.substring(0, 240);
  }

  resolved_timezone = extractJsonStringField(body, "timezone");
  int32_t utc_offset_seconds = 0;
  const bool has_utc_offset = extractJsonIntField(body, "utc_offset_seconds", utc_offset_seconds);
  String weather_local_time = extractJsonStringFieldAfter(body, "\"current\":{", "time");
  if (weather_local_time.length() == 0) {
    weather_local_time = extractJsonStringFieldAfter(body, "\"current\" : {", "time");
  }
  Serial.printf("[TIME] weather time fields tz=%s esp_tz=%s offset=%ld raw=%s url=%s\n",
                resolved_timezone.c_str(), timezoneForEsp(resolved_timezone, has_utc_offset,
                                                          utc_offset_seconds).c_str(),
                static_cast<long>(utc_offset_seconds), weather_local_time.c_str(),
                settings_.weather_url.c_str());
  int32_t weather_code = -1;
  bool settings_changed = false;
  if (extractJsonIntField(body, "weather_code", weather_code)) {
    weather_code_ = static_cast<int>(weather_code);
    if (settings_.weather_code != weather_code_) {
      settings_.weather_code = weather_code_;
      settings_changed = true;
    }
    Serial.printf("[WEATHER] code=%d\n", weather_code_);
  } else {
    Serial.printf("[WEATHER] code missing, keeping cached=%d\n", weather_code_);
  }
  if (resolved_timezone.length() == 0) {
    resolved_timezone = settings_.timezone;
  }
  const String tz_for_sync = timezoneForEsp(resolved_timezone, has_utc_offset, utc_offset_seconds);

  if (resolved_timezone.length() > 0 && resolved_timezone != settings_.timezone) {
    settings_.timezone = resolved_timezone;
    settings_changed = true;
    timezone_updated = true;
    Serial.printf("[TIME] timezone updated from weather: %s\n", settings_.timezone.c_str());
  }

  if (settings_changed && !saveSettings()) {
    Serial.println("[WEATHER] cache save failed");
  }

  if (!request_ntp && systemClockTrusted()) {
    setenv("TZ", tz_for_sync.c_str(), 1);
    tzset();
    const time_t now_epoch = time(nullptr);
    struct tm tm_local {};
    char buf[40] = {0};
    if (localtime_r(&now_epoch, &tm_local) != nullptr &&
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %z", &tm_local) > 0) {
      local_time = String(buf);
    }
    Serial.printf("[TIME] ntp skipped reason=background trusted=true local=%s\n",
                  local_time.c_str());
    return true;
  }

  if (!syncClockWithTimezone(tz_for_sync, local_time, time_sync_error)) {
    time_t weather_epoch = 0;
    if (has_utc_offset &&
        parseWeatherLocalTimeEpoch(weather_local_time, utc_offset_seconds, weather_epoch) &&
        setSystemClockFromEpoch(weather_epoch, "weather_http")) {
      struct tm tm_local {};
      if (localtime_r(&weather_epoch, &tm_local) != nullptr) {
        char buf[40] = {0};
        if (strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %z", &tm_local) > 0) {
          local_time = String(buf);
        }
      }
      Serial.printf("[TIME] ntp failed, used weather time local=%s raw=%s offset=%ld\n",
                    local_time.c_str(), weather_local_time.c_str(),
                    static_cast<long>(utc_offset_seconds));
      return true;
    }
    Serial.printf("[TIME] weather time fallback failed has_offset=%s raw=%s offset=%ld ntp_err=%s\n",
                  has_utc_offset ? "true" : "false", weather_local_time.c_str(),
                  static_cast<long>(utc_offset_seconds), time_sync_error.c_str());
    return false;
  }
  return true;
}

size_t WifiManager::calendarEventCount() const {
  return calendar_store_.count();
}

bool WifiManager::calendarEventAt(size_t index, CalendarEvent &event) const {
  return calendar_store_.eventAt(index, event);
}

size_t WifiManager::calendarMonthSummaryCount() const {
  return calendar_month_summary_count_;
}

bool WifiManager::calendarMonthSummaryAt(size_t index, CalendarMonthSummaryEvent &event) const {
  if (index >= calendar_month_summary_count_) {
    return false;
  }
  event = calendar_month_summaries_[index];
  return true;
}

uint32_t WifiManager::calendarMonthSummarySignature() const {
  return calendar_month_summary_signature_;
}

bool WifiManager::markDailySyncDay(int32_t day_key) {
  if (day_key <= 0 || settings_.last_daily_sync_day == day_key) {
    return day_key > 0;
  }
  const int32_t previous = settings_.last_daily_sync_day;
  settings_.last_daily_sync_day = day_key;
  if (!saveSettings()) {
    settings_.last_daily_sync_day = previous;
    return false;
  }
  Serial.printf("[SYNC] daily marker saved day=%ld\n", static_cast<long>(day_key));
  return true;
}

void WifiManager::loadSettings() {
  if (prefs_ == nullptr) {
    prefs_ = new Preferences();
  }
  String packed_events;
  uint16_t next_calendar_event_id = calendar_store_.nextId();
  if (!SettingsStore::load(*prefs_, settings_, next_calendar_event_id, packed_events)) {
    applyDefaultSettings();
    return;
  }
  calendar_store_.setNextId(next_calendar_event_id);
  calendar_store_.deserialize(packed_events);
  calendar_store_.setNextId(next_calendar_event_id);
  weather_code_ = settings_.weather_code;
  if (applyKnownWeatherLocationDefaults(settings_)) {
    Serial.printf("[CFG] corrected known weather location city=%s lat=%s lon=%s\n",
                  settings_.weather_city.c_str(),
                  settings_.weather_lat.c_str(),
                  settings_.weather_lon.c_str());
    saveSettings();
  }
  Serial.printf("[CFG] loaded sta_ssid=%s (%s)\n",
                settings_.sta_ssid.c_str(),
                (settings_.sta_ssid == SettingsStore::defaultStaSsid()) ? "default" : "prefs");
}

bool WifiManager::saveSettings() {
  if (prefs_ == nullptr) {
    prefs_ = new Preferences();
  }
  if (!SettingsStore::save(*prefs_, settings_, calendar_store_.nextId(), calendar_store_.serialize())) {
    Serial.println("[CFG] preferences open failed");
    return false;
  }
  Serial.println("[CFG] settings saved");
  return true;
}

void WifiManager::pruneExpiredCalendarEvents() {
  const time_t now_epoch = time(nullptr);
  if (now_epoch <= 0) {
    return;
  }
  const time_t month_start = localMonthWindowStart(now_epoch);
  const time_t month_end = localWindowEndOneMonth(month_start);
  struct tm start_tm {};
  struct tm end_tm {};
  if (localtime_r(&month_start, &start_tm) == nullptr ||
      localtime_r(&month_end, &end_tm) == nullptr) {
    return;
  }
  const String min_date = formatDateYmd(start_tm);
  const String max_date = formatDateYmd(end_tm);
  const size_t removed = calendar_store_.removeOnceOutsideRange(min_date, max_date);
  if (removed > 0) {
    Serial.printf("[CAL] pruned out-of-month manual events removed=%u range=%s..%s\n",
                  static_cast<unsigned>(removed), min_date.c_str(), max_date.c_str());
    saveSettings();
  }
}

void WifiManager::applyDefaultSettings() {
  size_t ignored_count = 0;
  uint16_t next_calendar_event_id = calendar_store_.nextId();
  SettingsStore::applyDefaults(settings_, ignored_count, next_calendar_event_id);
  calendar_store_.clear();
  calendar_store_.setNextId(next_calendar_event_id);
}

void WifiManager::maybeSyncCalendarUrl(uint32_t now_ms) {
  if (state_ != State::StaRunning || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (sta_session_role_ == StaSessionRole::ManualConfig ||
      sta_session_role_ == StaSessionRole::ApBackground) {
    return;
  }
  if (!settings_.calendar_enabled) {
    return;
  }

  const uint32_t requested_interval_ms =
      (settings_.calendar_refresh_sec > 0u) ? settings_.calendar_refresh_sec * 1000u : 0u;
  const uint32_t sync_interval_ms =
      std::max<uint32_t>(requested_interval_ms, kCalendarSyncMinIntervalMs);
  const bool due_by_interval =
      last_calendar_sync_ms_ != 0u && (now_ms - last_calendar_sync_ms_) >= sync_interval_ms;
  if (!calendar_sync_pending_ && !due_by_interval) {
    return;
  }

  const bool is_remote_url =
      settings_.calendar_url.startsWith("http://") || settings_.calendar_url.startsWith("https://");
  const bool is_local_path = settings_.calendar_url.startsWith("/");
  if (!is_remote_url && !is_local_path) {
    if (settings_.calendar_url.length() == 0) {
      Serial.println("[CALSYNC] skip reason=empty_url");
    } else {
      Serial.printf("[CALSYNC] skip unsupported source=%s\n", settings_.calendar_url.c_str());
      last_calendar_sync_ms_ = now_ms;
    }
    calendar_sync_pending_ = false;
    if (last_calendar_sync_status_ == "queued" || last_calendar_sync_status_ == "running") {
      last_calendar_sync_status_ = "idle";
    }
    return;
  }

  Serial.printf("[CALSYNC] trigger pending=%s due=%s interval_s=%lu now_ms=%lu url=%s\n",
                calendar_sync_pending_ ? "true" : "false",
                due_by_interval ? "true" : "false",
                static_cast<unsigned long>(sync_interval_ms / 1000u),
                static_cast<unsigned long>(now_ms),
                settings_.calendar_url.c_str());

  calendar_sync_pending_ = false;
  last_calendar_sync_ms_ = now_ms;
  last_calendar_sync_status_ = "running";
  last_calendar_sync_error_ = "";
  String error_msg;
  if (!syncCalendarFromUrl(error_msg)) {
    last_calendar_sync_status_ = "error";
    last_calendar_sync_error_ = error_msg;
    Serial.printf("[CALSYNC] failed err=%s url=%s\n", error_msg.c_str(),
                  settings_.calendar_url.c_str());
  }
}

bool WifiManager::syncCalendarFromUrl(String &error_msg, time_t now_epoch_override) {
  error_msg = "";
  last_calendar_sync_imported_ = 0;
  last_calendar_sync_total_ = 0;
  last_calendar_sync_vevents_ = 0;

  const uint32_t sync_start_ms = millis();
  if (kDebugLogs) {
    Serial.printf("[CALSYNC] fetch begin url=%s free_heap=%u largest=%u\n",
                  settings_.calendar_url.c_str(),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxAllocHeap()));
  }

  String body;
  String content_type = "text/calendar";
  if (settings_.calendar_url.startsWith("/")) {
    initWebFs();
    if (!web_fs_ready_) {
      error_msg = "spiffs_not_ready";
      Serial.printf("[CALSYNC] local open failed err=%s path=%s\n", error_msg.c_str(),
                    settings_.calendar_url.c_str());
      return false;
    }
    File file = SPIFFS.open(settings_.calendar_url, FILE_READ);
    if (!file) {
      error_msg = "spiffs_open_failed";
      Serial.printf("[CALSYNC] local open failed err=%s path=%s\n", error_msg.c_str(),
                    settings_.calendar_url.c_str());
      return false;
    }
    if (file.size() == 0u || file.size() > kCalendarSourceMaxBytes) {
      error_msg = "calendar_source_too_large";
      file.close();
      return false;
    }
    body = file.readString();
    file.close();
    if (kDebugLogs) {
      Serial.printf("[CALSYNC] local load ok path=%s bytes=%u elapsed=%lums\n",
                    settings_.calendar_url.c_str(),
                    static_cast<unsigned>(body.length()),
                    static_cast<unsigned long>(millis() - sync_start_ms));
    }
  } else {
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(settings_.calendar_url)) {
      error_msg = "http_begin_failed";
      return false;
    }

    const int http_code = http.GET();
    if (http_code != 200) {
      error_msg = "http_" + String(http_code);
      Serial.printf("[CALSYNC] fetch http_failed code=%d elapsed=%lums\n", http_code,
                    static_cast<unsigned long>(millis() - sync_start_ms));
      http.end();
      return false;
    }

    content_type = http.header("Content-Type");
    const bool body_ok = readHttpBodyBounded(http, kCalendarSourceMaxBytes, body);
    http.end();
    if (!body_ok) {
      error_msg = "calendar_source_too_large";
      return false;
    }
    if (kDebugLogs) {
      Serial.printf("[CALSYNC] fetch ok code=%d bytes=%u content_type=%s elapsed=%lums\n",
                    http_code, static_cast<unsigned>(body.length()), content_type.c_str(),
                    static_cast<unsigned long>(millis() - sync_start_ms));
    }
  }
  if (body.length() == 0) {
    error_msg = "empty_body";
    return false;
  }
  if (body.indexOf("BEGIN:VCALENDAR") < 0) {
    error_msg = "not_ics";
    return false;
  }

  std::vector<ParsedIcsEvent> master_events;
  std::vector<ParsedIcsEvent> override_events;
  size_t vevent_count = 0;
  parseIcsBodyIntoEvents(body, master_events, override_events, vevent_count);
  last_calendar_sync_vevents_ = static_cast<uint16_t>(std::min<size_t>(vevent_count, 65535u));
  if (kDebugLogs) {
    Serial.printf("[CALSYNC] parse vevents=%u masters=%u overrides=%u\n",
                  static_cast<unsigned>(vevent_count),
                  static_cast<unsigned>(master_events.size()),
                  static_cast<unsigned>(override_events.size()));
  }
  const time_t now_epoch = (now_epoch_override > 0) ? now_epoch_override : time(nullptr);
  if (now_epoch <= 0) {
    error_msg = "clock_invalid";
    return false;
  }
  const time_t window_start = localMonthWindowStart(now_epoch);
  const time_t window_end = localWindowEndOneMonth(window_start);
  if (kDebugLogs) {
    Serial.printf("[CALSYNC] window start=%s end=%s now=%s\n",
                  formatDateTimeYmdHm(window_start).c_str(),
                  formatDateTimeYmdHm(window_end).c_str(),
                  formatDateTimeYmdHm(now_epoch).c_str());
  }

  std::vector<IcsOverride> override_metadata;
  collectOverrideMetadata(override_events, override_metadata);
  if (kDebugLogs) {
    Serial.printf("[CALSYNC] override metadata entries=%u\n",
                  static_cast<unsigned>(override_metadata.size()));
  }

  std::vector<ImportedCalendarEvent> imported_items;
  imported_items.reserve(master_events.size() + override_events.size());

  for (const ParsedIcsEvent &event : master_events) {
    if (event.rrule.length() == 0) {
      expandSingleEvent(event, window_start, window_end, imported_items);
      continue;
    }

    CalendarRruleCore rrule;
    if (!parseRruleCore(event.rrule, rrule)) {
      expandSingleEvent(event, window_start, window_end, imported_items);
      continue;
    }
    expandRecurringEvent(event, rrule, override_metadata, window_start, window_end, imported_items);
  }

  appendOverrideEvents(override_events, window_start, window_end, imported_items);
  sortImportedEvents(imported_items);
  last_calendar_sync_imported_ =
      static_cast<uint16_t>(std::min<size_t>(imported_items.size(), 65535u));
  Serial.printf("[CALSYNC] expanded imported=%u\n",
                static_cast<unsigned>(imported_items.size()));
  if (kDebugLogs) {
    for (size_t i = 0; i < imported_items.size() && i < 8; ++i) {
      const CalendarEvent &event = imported_items[i].event;
      Serial.printf("[CALSYNC] item[%u] date=%s time=%s end=%s title=%s source=%s external=%s\n",
                    static_cast<unsigned>(i),
                    event.date.c_str(),
                    event.time_hhmm.c_str(),
                    event.end_time_hhmm.c_str(),
                    event.title.c_str(),
                    event.source.c_str(),
                    event.external_id.c_str());
    }
    if (imported_items.size() > 8u) {
      Serial.printf("[CALSYNC] item listing truncated remaining=%u\n",
                    static_cast<unsigned>(imported_items.size() - 8u));
    }
  }

  calendar_month_summary_count_ = 0;
  uint32_t month_signature = 2166136261UL;
  for (const ImportedCalendarEvent &item : imported_items) {
    if (calendar_month_summary_count_ >= static_cast<size_t>(kMaxCalendarMonthSummaries)) {
      break;
    }
    CalendarMonthSummaryEvent &summary = calendar_month_summaries_[calendar_month_summary_count_++];
    summary.title = item.event.title;
    summary.date = item.event.date;
    summary.time_hhmm = item.event.time_hhmm;
    summary.end_time_hhmm = item.event.end_time_hhmm;
    summary.color = item.event.color;
    mixCalendarHashString(month_signature, summary.date);
    mixCalendarHashString(month_signature, summary.time_hhmm);
    mixCalendarHashString(month_signature, summary.end_time_hhmm);
    mixCalendarHashString(month_signature, summary.color);
    mixCalendarHashString(month_signature, summary.title);
  }
  mixCalendarHashInt(month_signature, static_cast<uint32_t>(calendar_month_summary_count_));
  calendar_month_summary_signature_ = month_signature;
  struct tm month_tm {};
  if (localtime_r(&now_epoch, &month_tm) != nullptr) {
    calendar_month_summary_month_key_ =
        static_cast<int32_t>((month_tm.tm_year + 1900) * 100 + (month_tm.tm_mon + 1));
  } else {
    calendar_month_summary_month_key_ = -1;
  }
  calendar_month_summary_source_ = settings_.calendar_url;

  last_calendar_sync_epoch_ = time(nullptr);
  last_calendar_sync_status_ = "ok";
  last_calendar_sync_error_ = "";
  last_calendar_sync_total_ = static_cast<uint16_t>(calendar_month_summary_count_);
  const bool cache_saved = saveCalendarMonthCache();
  Serial.printf("[CALSYNC] ok vevents=%u imported=%u month=%u cache=%s manual=%u elapsed=%lums window=%lu..%lu\n",
                static_cast<unsigned>(vevent_count),
                static_cast<unsigned>(imported_items.size()),
                static_cast<unsigned>(calendar_month_summary_count_),
                cache_saved ? "saved" : "failed",
                static_cast<unsigned>(calendar_store_.count()),
                static_cast<unsigned long>(millis() - sync_start_ms),
                static_cast<unsigned long>(window_start),
                static_cast<unsigned long>(window_end));
  return true;
}

void WifiManager::startServer() {
  stopServer();
  server_ = new WebServer(kHttpPort);

  server_->on("/", HTTP_GET, [this]() { handleRoot(); });
  server_->on("/generate_204", HTTP_GET, [this]() { handleRoot(); });
  server_->on("/hotspot-detect.html", HTTP_GET, [this]() { handleRoot(); });
  server_->on("/connecttest.txt", HTTP_GET, [this]() { handleRoot(); });
  server_->on("/ncsi.txt", HTTP_GET, [this]() { handleRoot(); });
  server_->on("/redirect", HTTP_GET, [this]() { handleRoot(); });
  server_->on("/app.js", HTTP_GET, [this]() {
    markActivity(millis());
    if (serveWebAsset("/app.js", "application/javascript; charset=utf-8")) {
      return;
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
  server_->on("/api/client-log", HTTP_POST, [this]() {
    markActivity(millis());
    auto cleanLogValue = [](String value, size_t max_length) {
      value.replace("\r", " ");
      value.replace("\n", " ");
      if (value.length() > max_length) {
        value.remove(max_length);
      }
      return value;
    };
    const String event = cleanLogValue(server_->arg("event"), 32u);
    const String version = cleanLogValue(server_->arg("version"), 32u);
    const String file = cleanLogValue(server_->arg("file"), 120u);
    const String stage = cleanLogValue(server_->arg("stage"), 32u);
    const String mode = cleanLogValue(server_->arg("mode"), 32u);
    const String algo = cleanLogValue(server_->arg("algo"), 32u);
    const String error = cleanLogValue(server_->arg("error"), 160u);
    Serial.printf("[UPLOAD][CLIENT] event=%s version=%s file=%s stage=%s mode=%s algo=%s "
                  "error=%s remote=%s\n",
                  event.c_str(), version.c_str(), file.c_str(), stage.c_str(), mode.c_str(),
                  algo.c_str(), error.c_str(), server_->client().remoteIP().toString().c_str());
    server_->send(200, "application/json", "{\"ok\":true}");
  });
  server_->on("/api/stop", HTTP_POST, [this]() { handleStopPortal(); });
  server_->on("/api/reboot", HTTP_POST, [this]() { handleReboot(); });
  server_->on(
      "/api/upload", HTTP_POST,
      [this]() {
        markActivity(millis());
        if (upload_ok_ && upload_started_ && upload_received_ > 0 &&
            upload_final_path_.length() > 0) {
          String json = "{\"ok\":true,\"path\":\"";
          json += jsonEscape(upload_final_path_);
          json += "\",\"size\":";
          json += String(upload_received_);
          json += "}";
          server_->send(200, "application/json", json);
          upload_started_ = false;
          upload_received_ = 0;
          upload_final_path_ = "";
          return;
        }
        String json = "{\"ok\":false,\"error\":\"";
        json += jsonEscape(upload_error_.length() > 0 ? upload_error_ : "upload_incomplete");
        json += "\"}";
        server_->send(500, "application/json", json);
        upload_started_ = false;
        upload_received_ = 0;
        upload_final_path_ = "";
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
  if (isApSessionActive()) {
    if (last_activity_ms_ == 0) {
      markActivity(now_ms);
      return;
    }
    // Keep AP alive while at least one station is connected.
    const int sta_num = WiFi.softAPgetStationNum();
    if (sta_num > 0) {
      markActivity(now_ms);
    }
    const int32_t signed_idle_ms = static_cast<int32_t>(now_ms - last_activity_ms_);
    if (signed_idle_ms < 0) {
      return;
    }
    const uint32_t idle_ms = static_cast<uint32_t>(signed_idle_ms);
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
    const int32_t signed_idle_ms = static_cast<int32_t>(now_ms - base_ms);
    if (signed_idle_ms < 0) {
      return;
    }
    const uint32_t idle_ms = static_cast<uint32_t>(signed_idle_ms);
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
  sd_ready_ = mountSdCard("wifi_manager");
}

void WifiManager::deinitSD() {
  if (sd_ready_) {
    unmountSdCard("wifi_manager");
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

void WifiManager::clearCalendarMonthCache(bool remove_files) {
  calendar_month_summary_count_ = 0;
  calendar_month_summary_signature_ = 0;
  calendar_month_summary_month_key_ = -1;
  calendar_month_summary_source_ = "";
  if (!remove_files) {
    return;
  }
  initWebFs();
  if (!web_fs_ready_) {
    return;
  }
  SPIFFS.remove(kCalendarCachePath);
  SPIFFS.remove(kCalendarCacheTempPath);
  SPIFFS.remove(kCalendarCacheBackupPath);
}

bool WifiManager::loadCalendarMonthCacheFile(const char *path) {
  File file = SPIFFS.open(path, FILE_READ);
  if (!file || file.size() == 0u || file.size() > kCalendarCacheMaxBytes) {
    if (file) file.close();
    return false;
  }

  const size_t cache_bytes = file.size();
  String magic = file.readStringUntil('\n');
  magic.trim();
  if (magic != kCalendarCacheMagic) {
    file.close();
    return false;
  }

  String header = file.readStringUntil('\n');
  header.trim();
  int cursor = 0;
  auto nextField = [](const String &line, int &offset, String &value) -> bool {
    if (offset < 0 || offset > static_cast<int>(line.length())) return false;
    const int separator = line.indexOf('\t', offset);
    if (separator < 0) {
      value = line.substring(offset);
      offset = static_cast<int>(line.length()) + 1;
    } else {
      value = line.substring(offset, separator);
      offset = separator + 1;
    }
    return true;
  };

  String month_field;
  String signature_field;
  String count_field;
  String source_field;
  if (!nextField(header, cursor, month_field) ||
      !nextField(header, cursor, signature_field) ||
      !nextField(header, cursor, count_field) ||
      !nextField(header, cursor, source_field)) {
    file.close();
    return false;
  }
  const int32_t month_key = static_cast<int32_t>(month_field.toInt());
  const uint32_t stored_signature = static_cast<uint32_t>(strtoul(signature_field.c_str(), nullptr, 10));
  const size_t expected_count = static_cast<size_t>(count_field.toInt());
  const String source = urlDecode(source_field);
  if (month_key <= 0 || expected_count > kMaxCalendarMonthSummaries ||
      source.length() == 0u || source != settings_.calendar_url) {
    file.close();
    return false;
  }

  clearCalendarMonthCache(false);
  uint32_t computed_signature = 2166136261UL;
  while (file.available() && calendar_month_summary_count_ < expected_count) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0u) continue;
    int field_cursor = 0;
    String fields[5];
    bool valid = true;
    for (uint8_t i = 0; i < 5u; ++i) {
      if (!nextField(line, field_cursor, fields[i])) {
        valid = false;
        break;
      }
    }
    if (!valid) {
      clearCalendarMonthCache(false);
      file.close();
      return false;
    }
    CalendarMonthSummaryEvent &event =
        calendar_month_summaries_[calendar_month_summary_count_++];
    event.date = urlDecode(fields[0]);
    event.time_hhmm = urlDecode(fields[1]);
    event.end_time_hhmm = urlDecode(fields[2]);
    event.color = urlDecode(fields[3]);
    event.title = urlDecode(fields[4]);
    mixCalendarHashString(computed_signature, event.date);
    mixCalendarHashString(computed_signature, event.time_hhmm);
    mixCalendarHashString(computed_signature, event.end_time_hhmm);
    mixCalendarHashString(computed_signature, event.color);
    mixCalendarHashString(computed_signature, event.title);
  }
  file.close();
  mixCalendarHashInt(computed_signature,
                     static_cast<uint32_t>(calendar_month_summary_count_));
  if (calendar_month_summary_count_ != expected_count ||
      computed_signature != stored_signature) {
    clearCalendarMonthCache(false);
    return false;
  }

  calendar_month_summary_signature_ = stored_signature;
  calendar_month_summary_month_key_ = month_key;
  calendar_month_summary_source_ = source;
  last_calendar_sync_status_ = "cached";
  last_calendar_sync_total_ = static_cast<uint16_t>(calendar_month_summary_count_);
  Serial.printf("[CALCACHE] loaded path=%s month=%ld count=%u bytes=%u\n", path,
                static_cast<long>(month_key),
                static_cast<unsigned>(calendar_month_summary_count_),
                static_cast<unsigned>(cache_bytes));
  return true;
}

bool WifiManager::loadCalendarMonthCache() {
  if (settings_.calendar_url.length() == 0u) {
    clearCalendarMonthCache(true);
    return false;
  }
  initWebFs();
  if (!web_fs_ready_) return false;
  if (SPIFFS.exists(kCalendarCachePath) && loadCalendarMonthCacheFile(kCalendarCachePath)) {
    return true;
  }
  if (SPIFFS.exists(kCalendarCacheBackupPath) &&
      loadCalendarMonthCacheFile(kCalendarCacheBackupPath)) {
    SPIFFS.remove(kCalendarCachePath);
    SPIFFS.rename(kCalendarCacheBackupPath, kCalendarCachePath);
    return true;
  }
  clearCalendarMonthCache(true);
  return false;
}

bool WifiManager::saveCalendarMonthCache() {
  if (calendar_month_summary_source_.length() == 0u ||
      calendar_month_summary_month_key_ <= 0) {
    return false;
  }
  initWebFs();
  if (!web_fs_ready_) return false;

  SPIFFS.remove(kCalendarCacheTempPath);
  File file = SPIFFS.open(kCalendarCacheTempPath, FILE_WRITE);
  if (!file) return false;
  file.println(kCalendarCacheMagic);
  file.print(calendar_month_summary_month_key_);
  file.print('\t');
  file.print(calendar_month_summary_signature_);
  file.print('\t');
  file.print(calendar_month_summary_count_);
  file.print('\t');
  file.println(urlEncode(calendar_month_summary_source_));
  for (size_t i = 0; i < calendar_month_summary_count_; ++i) {
    const CalendarMonthSummaryEvent &event = calendar_month_summaries_[i];
    file.print(urlEncode(event.date));
    file.print('\t');
    file.print(urlEncode(event.time_hhmm));
    file.print('\t');
    file.print(urlEncode(event.end_time_hhmm));
    file.print('\t');
    file.print(urlEncode(event.color));
    file.print('\t');
    file.println(urlEncode(event.title));
  }
  file.flush();
  const bool write_ok = file.getWriteError() == 0 && file.size() <= kCalendarCacheMaxBytes;
  const size_t written_bytes = file.size();
  file.close();
  if (!write_ok) {
    SPIFFS.remove(kCalendarCacheTempPath);
    return false;
  }

  SPIFFS.remove(kCalendarCacheBackupPath);
  if (SPIFFS.exists(kCalendarCachePath) &&
      !SPIFFS.rename(kCalendarCachePath, kCalendarCacheBackupPath)) {
    SPIFFS.remove(kCalendarCacheTempPath);
    return false;
  }
  if (!SPIFFS.rename(kCalendarCacheTempPath, kCalendarCachePath)) {
    if (SPIFFS.exists(kCalendarCacheBackupPath)) {
      SPIFFS.rename(kCalendarCacheBackupPath, kCalendarCachePath);
    }
    return false;
  }
  SPIFFS.remove(kCalendarCacheBackupPath);
  Serial.printf("[CALCACHE] saved month=%ld count=%u bytes=%u\n",
                static_cast<long>(calendar_month_summary_month_key_),
                static_cast<unsigned>(calendar_month_summary_count_),
                static_cast<unsigned>(written_bytes));
  return true;
}

bool WifiManager::serveWebAsset(const char *path, const char *content_type) {
  if (server_ == nullptr || path == nullptr || content_type == nullptr) {
    return false;
  }
  initWebFs();
  if (!web_fs_ready_) {
    return false;
  }
  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  server_->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server_->sendHeader("Pragma", "no-cache");
  server_->streamFile(file, content_type);
  file.close();
  return true;
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
  String lower = path;
  lower.toLowerCase();
  if (lower.endsWith(".html")) return "text/html";
  if (lower.endsWith(".css")) return "text/css";
  if (lower.endsWith(".js")) return "application/javascript";
  if (lower.endsWith(".json")) return "application/json";
  if (lower.endsWith(".png")) return "image/png";
  if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
  if (lower.endsWith(".bmp")) return "image/bmp";
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

String WifiManager::joinSdPath(const String &dir, const String &name) const {
  String out = dir;
  if (!out.endsWith("/")) {
    out += "/";
  }
  out += name;
  return out;
}

String WifiManager::leafName(const String &path) const {
  const int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}

String WifiManager::algoSuffix(const String &algo) const {
  String lower = algo;
  lower.toLowerCase();
  if (lower == "fs_serpentine" || lower == "fs") {
    return "fs";
  }
  if (lower == "atkinson" || lower == "atk") {
    return "atk";
  }
  return "unk";
}

String WifiManager::processedImageFilename(const String &original_name,
                                           const String &algo) const {
  String base = leafName(original_name);
  const int extension = base.lastIndexOf('.');
  if (extension > 0) {
    base = base.substring(0, extension);
  }
  if (base.length() == 0) {
    base = "image";
  }
  const String suffix = algoSuffix(algo);
  return base + "_" + suffix + ".png";
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
    const String child_path = joinSdPath(path, leafName(String(child.name())));
    child.close();
    if (!removePathRecursive(child_path)) {
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
  digitalWrite(kPeripheralPowerPin, HIGH);
  delay(3);
  Wire.begin(kI2cSdaPin, kI2cSclPin);
  restoreClockFromRtc();
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
}

void WifiManager::sampleSensorsNow(bool assume_peripheral_powered) {
  const int previous_power_state = digitalRead(kPeripheralPowerPin);
  const bool restore_power =
      !assume_peripheral_powered && previous_power_state != HIGH;
  if (restore_power) {
    pinMode(kPeripheralPowerPin, OUTPUT);
    digitalWrite(kPeripheralPowerPin, HIGH);
    delay(3);
  }
  if (battery_adc_pin_ >= 0) {
    battery_mv_ = readBatteryMilliVolts(battery_adc_pin_);
  }
  float temperature_c = NAN;
  float humidity_pct = NAN;
  if (readAHT20(temperature_c, humidity_pct)) {
    temperature_c_ = temperature_c;
    humidity_pct_ = humidity_pct;
  } else {
    temperature_c_ = NAN;
    humidity_pct_ = NAN;
  }
  if (restore_power) {
    digitalWrite(kPeripheralPowerPin, LOW);
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
  static constexpr uint8_t kSampleCount = 9;
  int samples[kSampleCount] = {};
  uint8_t valid_count = 0;
  for (uint8_t i = 0; i < kSampleCount; ++i) {
    const int raw_mv = analogReadMilliVolts(static_cast<uint8_t>(pin));
    if (raw_mv > 0) {
      samples[valid_count++] = raw_mv;
    }
    delay(2);
  }
  if (valid_count == 0) {
    return -1;
  }
  for (uint8_t i = 1; i < valid_count; ++i) {
    const int value = samples[i];
    uint8_t j = i;
    while (j > 0 && samples[j - 1] > value) {
      samples[j] = samples[j - 1];
      --j;
    }
    samples[j] = value;
  }
  const int raw_mv = samples[valid_count / 2u];
  // Hardware divider is 1:2 at IO39, so the ADC sees half of VBAT.
  return raw_mv * 2;
}

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
  const String mode = (state_ == State::ApRunning)
                          ? "AP"
                          : ((state_ == State::StaRunning) ? "STA" : "IDLE");
  if (kDebugLogs) {
    Serial.printf("[HTTP] GET / from %s mode=%s\n",
                  server_->client().remoteIP().toString().c_str(), mode.c_str());
  }
  if (serveWebAsset(kPortalHtmlPath, "text/html; charset=utf-8")) {
    return;
  }
  if (web_fs_ready_) {
    Serial.println("[WEB] missing /portal.html in SPIFFS, falling back to minimal page");
  } else {
    Serial.println("[WEB] SPIFFS not ready, falling back to minimal page");
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
}

void WifiManager::handleStatus() {
  markActivity(millis());
  if (kDebugLogs) {
    Serial.printf("[HTTP] GET /api/status from %s\n",
                  server_->client().remoteIP().toString().c_str());
  }
  sampleSensorsNow();
  const char *state_str = "idle";
  if (state_ == State::ApRunning) state_str = "ap_running";
  if (state_ == State::StaConnecting) state_str = "sta_connecting";
  if (state_ == State::StaRunning) state_str = "sta_running";
  String json = "{";
  json += "\"state\":\"";
  json += state_str;
  json += "\",\"ap_active\":";
  json += ap_active_ ? "true" : "false";
  json += ",\"sta_connecting\":";
  json += (state_ == State::StaConnecting) ? "true" : "false";
  json += ",\"sta_connected\":";
  json += (state_ == State::StaRunning && WiFi.status() == WL_CONNECTED) ? "true" : "false";
  json += ",\"sta_ip\":\"";
  json += jsonEscape(WiFi.localIP().toString());
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
  json += ",\"clock_trusted\":";
  json += systemClockTrusted() ? "true" : "false";
  json += ",\"rtc_time_trusted\":";
  json += rtc_time_trusted_ ? "true" : "false";
  json += ",\"epoch\":";
  json += String(static_cast<unsigned long>(time(nullptr)));
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
  json += ",\"weather_city\":\"";
  json += jsonEscape(settings_.weather_city);
  json += "\",\"weather_code\":";
  json += String(weather_code_);
  json += ",\"weather_url\":\"";
  json += jsonEscape(settings_.weather_url);
  json += "\"";
  json += ",\"calendar_sync_pending\":";
  json += calendar_sync_pending_ ? "true" : "false";
  json += ",\"calendar_sync_status\":\"";
  json += jsonEscape(last_calendar_sync_status_);
  json += "\",\"calendar_sync_error\":\"";
  json += jsonEscape(last_calendar_sync_error_);
  json += "\",\"calendar_sync_epoch\":";
  json += String(static_cast<unsigned long>(last_calendar_sync_epoch_));
  json += ",\"calendar_sync_vevents\":";
  json += String(last_calendar_sync_vevents_);
  json += ",\"calendar_sync_imported\":";
  json += String(last_calendar_sync_imported_);
  json += ",\"calendar_sync_total\":";
  json += String(last_calendar_sync_total_);
  if (state_ == State::ApRunning) {
    const uint32_t remain =
        (millis() - last_activity_ms_ >= kApIdleTimeoutMs)
            ? 0
            : (kApIdleTimeoutMs - (millis() - last_activity_ms_));
    json += ",\"idle_remaining_ms\":";
    json += String(remain);
  } else if (state_ == State::StaConnecting) {
    const uint32_t remain =
        (millis() - sta_connect_start_ms_ >= sta_connect_timeout_ms_)
            ? 0
            : (sta_connect_timeout_ms_ - (millis() - sta_connect_start_ms_));
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
  if (kDebugLogs) {
    Serial.printf("[HTTP] GET /api/settings from %s\n",
                  server_->client().remoteIP().toString().c_str());
  }
  String json = "{";
  json += "\"sta_ssid\":\"" + jsonEscape(settings_.sta_ssid) + "\",";
  json += "\"sta_user\":\"" + jsonEscape(settings_.sta_user) + "\",";
  json += "\"sta_pass\":\"" + jsonEscape(settings_.sta_pass) + "\",";
  json += "\"sta_auth_mode\":\"" + jsonEscape(settings_.sta_auth_mode) + "\",";
  json += "\"ui_language\":\"" + jsonEscape(settings_.ui_language) + "\",";
  json += "\"timezone\":\"" + jsonEscape(settings_.timezone) + "\",";
  json += "\"photo_interval_sec\":" + String(settings_.photo_interval_sec) + ",";
  json += "\"app_auto_switch_enabled\":";
  json += settings_.app_auto_switch_enabled ? "true," : "false,";
  json += "\"app_switch_interval_sec\":" + String(settings_.app_switch_interval_sec) + ",";
  json += "\"calendar_enabled\":";
  json += settings_.calendar_enabled ? "true," : "false,";
  json += "\"calendar_layout\":\"" + jsonEscape(settings_.calendar_layout) + "\",";
  json += "\"schedule_columns\":\"" + jsonEscape(settings_.schedule_columns) + "\",";
  json += "\"calendar_refresh_sec\":" + String(settings_.calendar_refresh_sec) + ",";
  json += "\"sleep_start\":\"" + formatMinuteHm(settings_.sleep_start_minute) + "\",";
  json += "\"sleep_end\":\"" + formatMinuteHm(settings_.sleep_end_minute) + "\",";
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
  const bool previous_calendar_enabled = settings_.calendar_enabled;
  const String previous_calendar_url = settings_.calendar_url;
  const uint32_t previous_calendar_refresh_sec = settings_.calendar_refresh_sec;
  const String previous_weather_city = settings_.weather_city;
  if (server_->hasArg("sta_ssid")) settings_.sta_ssid = server_->arg("sta_ssid");
  if (server_->hasArg("sta_user")) settings_.sta_user = server_->arg("sta_user");
  if (server_->hasArg("sta_pass")) settings_.sta_pass = server_->arg("sta_pass");
  if (server_->hasArg("sta_auth_mode")) settings_.sta_auth_mode = server_->arg("sta_auth_mode");
  if (server_->hasArg("ui_language")) settings_.ui_language = server_->arg("ui_language");
  if (server_->hasArg("photo_interval_sec")) {
    settings_.photo_interval_sec = static_cast<uint32_t>(server_->arg("photo_interval_sec").toInt());
    if (settings_.photo_interval_sec < 30) settings_.photo_interval_sec = 30;
    if (settings_.photo_interval_sec > 86400) settings_.photo_interval_sec = 86400;
  }
  if (server_->hasArg("app_auto_switch_enabled")) {
    const String enabled = server_->arg("app_auto_switch_enabled");
    settings_.app_auto_switch_enabled = (enabled == "1" || enabled == "true" || enabled == "on");
  } else if (server_->hasArg("app_auto_switch")) {
    const String enabled = server_->arg("app_auto_switch");
    settings_.app_auto_switch_enabled = (enabled == "1" || enabled == "true" || enabled == "on");
  }
  if (server_->hasArg("app_switch_interval_sec")) {
    settings_.app_switch_interval_sec =
        static_cast<uint32_t>(server_->arg("app_switch_interval_sec").toInt());
    if (settings_.app_switch_interval_sec < 60) settings_.app_switch_interval_sec = 60;
    if (settings_.app_switch_interval_sec > 86400) settings_.app_switch_interval_sec = 86400;
  }
  if (server_->hasArg("calendar_enabled")) {
    const String enabled = server_->arg("calendar_enabled");
    settings_.calendar_enabled = (enabled == "1" || enabled == "true" || enabled == "on");
  }
  if (server_->hasArg("calendar_layout")) settings_.calendar_layout = server_->arg("calendar_layout");
  if (server_->hasArg("schedule_columns")) {
    settings_.schedule_columns = server_->arg("schedule_columns");
  }
  if (server_->hasArg("calendar_refresh_sec")) {
    settings_.calendar_refresh_sec =
        static_cast<uint32_t>(server_->arg("calendar_refresh_sec").toInt());
    if (settings_.calendar_refresh_sec < 60) settings_.calendar_refresh_sec = 60;
    if (settings_.calendar_refresh_sec > 86400) settings_.calendar_refresh_sec = 86400;
  }
  if (server_->hasArg("sleep_start")) {
    uint16_t minute = settings_.sleep_start_minute;
    if (parseMinuteHm(server_->arg("sleep_start"), minute)) {
      settings_.sleep_start_minute = minute;
    }
  }
  if (server_->hasArg("sleep_end")) {
    uint16_t minute = settings_.sleep_end_minute;
    if (parseMinuteHm(server_->arg("sleep_end"), minute)) {
      settings_.sleep_end_minute = minute;
    }
  }
  if (server_->hasArg("calendar_url")) settings_.calendar_url = server_->arg("calendar_url");
  if (server_->hasArg("weather_city")) settings_.weather_city = server_->arg("weather_city");
  if (server_->hasArg("weather_lat")) settings_.weather_lat = server_->arg("weather_lat");
  if (server_->hasArg("weather_lon")) settings_.weather_lon = server_->arg("weather_lon");
  if (server_->hasArg("weather_url")) settings_.weather_url = server_->arg("weather_url");

  SettingsStore::normalize(settings_);
  SettingsStore::fillEmptyValues(settings_);
  if (!settings_.weather_city.equalsIgnoreCase(previous_weather_city)) {
    settings_.weather_location_city = "";
  }
  if (applyKnownWeatherLocationDefaults(settings_)) {
    Serial.printf("[CFG] corrected posted weather location city=%s lat=%s lon=%s\n",
                  settings_.weather_city.c_str(),
                  settings_.weather_lat.c_str(),
                  settings_.weather_lon.c_str());
  }

  if (state_ == State::StaRunning &&
      (settings_.calendar_enabled != previous_calendar_enabled ||
       settings_.calendar_url != previous_calendar_url ||
       settings_.calendar_refresh_sec != previous_calendar_refresh_sec)) {
    calendar_sync_pending_ = true;
    last_calendar_sync_ms_ = 0;
  }

  if (!saveSettings()) {
    server_->send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
    return;
  }
  if (settings_.calendar_url != previous_calendar_url) {
    clearCalendarMonthCache(true);
  }
  settings_apply_refresh_pending_ = true;
  server_->send(200, "application/json", "{\"ok\":true}");
}

void WifiManager::handleCalendarEventsGet() {
  markActivity(millis());
  if (kDebugLogs) {
    Serial.printf("[HTTP] GET /api/calendar/events from %s\n",
                  server_->client().remoteIP().toString().c_str());
  }
  constexpr size_t kMaxUpcomingEvents = 5u;
  std::vector<UpcomingCalendarEvent> upcoming;
  upcoming.reserve(kMaxUpcomingEvents + calendar_store_.count());
  const time_t now_epoch = time(nullptr);
  uint16_t order = 0;
  for (size_t i = 0; i < calendar_store_.count(); ++i) {
    CalendarEvent event;
    if (!calendar_store_.eventAt(i, event)) {
      continue;
    }
    time_t next_epoch = 0;
    String occurrence_date;
    if (!nextManualEventEpoch(event, now_epoch, next_epoch, occurrence_date)) {
      continue;
    }
    event.date = occurrence_date;
    UpcomingCalendarEvent item;
    item.event = event;
    item.epoch = next_epoch;
    item.order = order++;
    upcoming.push_back(item);
  }
  std::sort(upcoming.begin(), upcoming.end(), upcomingEventLess);

  server_->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server_->sendHeader("Pragma", "no-cache");
  server_->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_->send(200, "application/json; charset=utf-8", "");
  server_->sendContent("{\"ok\":true,\"items\":[");
  const size_t send_count = std::min(upcoming.size(), kMaxUpcomingEvents);
  for (size_t i = 0; i < send_count; ++i) {
    String item;
    item.reserve(240);
    if (i > 0) {
      item += ",";
    }
    appendCalendarEventJson(item, upcoming[i].event);
    server_->sendContent(item);
  }
  server_->sendContent("],\"total\":");
  server_->sendContent(String(upcoming.size()));
  server_->sendContent(",\"shown\":");
  server_->sendContent(String(send_count));
  server_->sendContent("}");
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
  if (e.title.length() > 96) {
    e.title = truncateCalendarUtf8Value(e.title, 96);
  }
  if (server_->hasArg("location")) {
    e.title = buildImportedTitle(e.title, server_->arg("location"), "");
  }
  if (!server_->hasArg("time")) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_time\"}");
    return;
  }
  String normalized_time;
  String normalized_end_time;
  if (!normalizeCalendarTimeValue(server_->arg("time"), normalized_time)) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_time\"}");
    return;
  }
  e.time_hhmm = normalized_time;
  if (server_->hasArg("end_time")) {
    const String end_time = server_->arg("end_time");
    if (end_time.length() > 0) {
      if (!normalizeCalendarTimeValue(end_time, normalized_end_time)) {
        server_->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_end_time\"}");
        return;
      }
      if (normalized_end_time.compareTo(normalized_time) <= 0) {
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
  e.color =
      normalizeCalendarColorValue(server_->hasArg("color") ? server_->arg("color") : "blue");
  e.repeat =
      normalizeCalendarRepeatValue(server_->hasArg("repeat") ? server_->arg("repeat") : "weekly");
  e.source = "manual";
  e.external_id =
      normalizeCalendarExternalIdValue(server_->hasArg("external_id") ? server_->arg("external_id")
                                                                      : "");
  if (server_->hasArg("updated_at")) {
    e.updated_at = normalizeCalendarUpdatedAtValue(server_->arg("updated_at"));
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
    if (!normalizeCalendarDateValue(server_->arg("date"), normalized_date)) {
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
  const int existing_idx = calendar_store_.findIndexByExternal(e.source, e.external_id);
  CalendarEvent previous_event;
  bool updated_existing = false;
  bool inserted_new = false;
  if (existing_idx >= 0) {
    previous_event = calendar_store_.data()[existing_idx];
    e.id = calendar_store_.data()[existing_idx].id;
    calendar_store_.data()[existing_idx] = e;
    updated_existing = true;
  } else {
    if (calendar_store_.count() >= static_cast<size_t>(kMaxCalendarEvents)) {
      server_->send(409, "application/json",
                    "{\"ok\":false,\"error\":\"manual_calendar_events_full\"}");
      return;
    }
    e.id = calendar_store_.allocateId();
    if (!calendar_store_.push(e)) {
      server_->send(500, "application/json", "{\"ok\":false,\"error\":\"store_push_failed\"}");
      return;
    }
    inserted_new = true;
  }
  if (!saveSettings()) {
    if (updated_existing && existing_idx >= 0) {
      calendar_store_.data()[existing_idx] = previous_event;
    } else if (inserted_new) {
      const int inserted_idx = calendar_store_.findIndexById(e.id);
      if (inserted_idx >= 0) {
        calendar_store_.removeAt(static_cast<size_t>(inserted_idx));
      }
    }
    server_->send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
    return;
  }
  String json = "{\"ok\":true,\"id\":";
  json += String(e.id);
  json += "}";
  server_->send(200, "application/json", json);
}

void WifiManager::handleCalendarEventsDelete() {
  markActivity(millis());
  Serial.printf("[HTTP] DELETE /api/calendar/events from %s\n",
                server_->client().remoteIP().toString().c_str());
  int idx = -1;
  if (server_->hasArg("id")) {
    const uint16_t id = static_cast<uint16_t>(server_->arg("id").toInt());
    idx = calendar_store_.findIndexById(id);
  } else if (server_->hasArg("source") && server_->hasArg("external_id")) {
    const String source = normalizeCalendarSourceValue(server_->arg("source"));
    const String external_id = normalizeCalendarExternalIdValue(server_->arg("external_id"));
    idx = calendar_store_.findIndexByExternal(source, external_id);
  } else {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_id_or_external\"}");
    return;
  }
  if (idx < 0) {
    server_->send(404, "application/json", "{\"ok\":false,\"error\":\"event_not_found\"}");
    return;
  }
  CalendarEvent target;
  if (!calendar_store_.eventAt(static_cast<size_t>(idx), target)) {
    server_->send(404, "application/json", "{\"ok\":false,\"error\":\"event_not_found\"}");
    return;
  }

  const uint16_t deleted_id = target.id;
  calendar_store_.removeAt(static_cast<size_t>(idx));
  if (!saveSettings()) {
    calendar_store_.push(target);
    server_->send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
    return;
  }
  String json = "{\"ok\":true,\"id\":";
  json += String(deleted_id);
  json += "}";
  server_->send(200, "application/json", json);
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

  Serial.printf("[HTTP] GET /api/geocode city=%s\n", city.c_str());
  String resolved_name;
  String lat;
  String lon;
  String weather_url;
  String error_msg;
  if (!fetchWeatherCityCoordinates(city, resolved_name, lat, lon, weather_url, error_msg)) {
    int status = 500;
    if (error_msg == "sta_required" || error_msg == "empty_city") status = 400;
    if (error_msg == "city_not_found") status = 404;
    if (error_msg == "geocode_http_failed") status = 502;
    String json = "{\"ok\":false,\"error\":\"" + jsonEscape(error_msg) + "\"}";
    server_->send(status, "application/json", json);
    return;
  }

  const String previous_city = settings_.weather_city;
  const String previous_location_city = settings_.weather_location_city;
  const String previous_lat = settings_.weather_lat;
  const String previous_lon = settings_.weather_lon;
  const String previous_url = settings_.weather_url;
  settings_.weather_city = city;
  settings_.weather_location_city = city;
  settings_.weather_lat = lat;
  settings_.weather_lon = lon;
  settings_.weather_url = weather_url;
  if (!saveSettings()) {
    settings_.weather_city = previous_city;
    settings_.weather_location_city = previous_location_city;
    settings_.weather_lat = previous_lat;
    settings_.weather_lon = previous_lon;
    settings_.weather_url = previous_url;
    server_->send(500, "application/json",
                  "{\"ok\":false,\"error\":\"save_failed\"}");
    return;
  }
  settings_apply_refresh_pending_ = true;

  String json = "{\"ok\":true,\"city\":\"";
  json += jsonEscape(resolved_name);
  json += "\",\"lat\":\"";
  json += jsonEscape(lat);
  json += "\",\"lon\":\"";
  json += jsonEscape(lon);
  json += "\",\"weather_url\":\"";
  json += jsonEscape(weather_url);
  json += "\"}";
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
  String filename = leafName(path);
  filename.replace("\"", "");
  server_->sendHeader("Content-Disposition",
                      String("attachment; filename=\"") + filename + "\"");
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

  String resolved_timezone;
  bool timezone_updated = false;
  String local_time;
  String time_sync_error;
  String preview;
  int code = 0;
  String request_error;
  const bool time_sync_ok = syncClockFromWeather(resolved_timezone, timezone_updated, local_time,
                                                 time_sync_error, preview, code, request_error);
  if (code <= 0) {
    const int status = (request_error == "bad_weather_url") ? 400 : 502;
    server_->send(status, "application/json",
                  String("{\"ok\":false,\"error\":\"") + request_error + "\"}");
    return;
  }
  if (time_sync_ok) {
    Serial.printf("[TIME] weather sync ok tz=%s local=%s\n",
                  resolved_timezone.c_str(), local_time.c_str());
    writeClockToRtc("weather_test");
  } else {
    Serial.printf("[TIME] weather sync failed tz=%s err=%s\n",
                  resolved_timezone.c_str(), time_sync_error.c_str());
  }
  String json = "{";
  json += "\"ok\":true,";
  json += "\"msg\":\"weather_request_ok\",";
  json += "\"http_status\":";
  json += String(code);
  json += ",\"weather_code\":";
  json += String(weather_code_);
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
    upload_started_ = true;
    upload_received_ = 0;
    upload_error_ = "";
    upload_mode_ = server_->hasArg("mode") ? server_->arg("mode") : "normal";
    upload_mode_.toLowerCase();
    upload_algo = server_->hasArg("algo") ? server_->arg("algo") : "none";
    upload_gamma = server_->hasArg("gamma") ? server_->arg("gamma") : "1.00";
    upload_tmp_path_ = "";
    upload_final_path_ = "";
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

  const bool preprocess_mode = (upload_mode_ == "fit" || upload_mode_ == "crop");
  String filepath = upload_tmp_path_;
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    filename.replace("\\", "");
    filename.replace("/", "");
    const bool image_output = server_->hasArg("kind") && server_->arg("kind") == "image";
    String dir = server_->hasArg("dir") ? server_->arg("dir") : "/";
    if (image_output) {
      dir = "/pic";
      filename = processedImageFilename(filename, upload_algo);
    }
    if (filename.length() == 0) {
      upload_ok_ = false;
      upload_error_ = "bad_filename";
      return;
    }
    if (!isSafePath(dir)) {
      upload_ok_ = false;
      upload_error_ = "bad_dir";
      Serial.printf("[SD] upload rejected bad dir=%s\n", dir.c_str());
      return;
    }

    filepath = dir;
    if (!filepath.endsWith("/")) {
      filepath += "/";
    }
    filepath += filename;
    upload_tmp_path_ = filepath;
  }
  if (filepath.length() == 0) {
    upload_ok_ = false;
    upload_error_ = "write_target_missing";
    return;
  }

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
      const size_t written = upload_file.write(upload.buf, upload.currentSize);
      upload_received_ += static_cast<uint32_t>(written);
      if (written != upload.currentSize) {
        upload_ok_ = false;
        upload_error_ = "write_failed";
      }
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

    File verify = SD.open(filepath, FILE_READ);
    if (!verify) {
      upload_ok_ = false;
      upload_error_ = "verify_open_failed";
      Serial.printf("[UPLOAD] verify open failed %s\n", filepath.c_str());
      return;
    }
    const uint32_t actual_size = static_cast<uint32_t>(verify.size());
    verify.close();
    if (actual_size == 0 || actual_size != upload_received_) {
      upload_ok_ = false;
      upload_error_ = "verify_size_failed";
      SD.remove(filepath);
      Serial.printf("[UPLOAD] verify size failed %s actual=%lu received=%lu\n",
                    filepath.c_str(), static_cast<unsigned long>(actual_size),
                    static_cast<unsigned long>(upload_received_));
      return;
    }

    upload_final_path_ = filepath;
    Serial.printf("[UPLOAD] done ok mode=%s algo=%s gamma=%s final=%s bytes=%lu\n",
                  upload_mode_.c_str(),
                  upload_algo.c_str(),
                  upload_gamma.c_str(),
                  filepath.c_str(),
                  static_cast<unsigned long>(upload_received_));
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
