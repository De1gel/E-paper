#include "system/SettingsStore.h"

#include "system/CalendarSettings.h"

namespace appfw {
namespace {

constexpr const char *kDefaultStaSsid = "";
constexpr const char *kDefaultStaUser = "";
constexpr const char *kDefaultStaPass = "";
constexpr const char *kDefaultStaAuthMode = "auto";
constexpr const char *kDefaultUiLanguage = "zh";
constexpr const char *kDefaultTimezone = "Asia/Shanghai";
constexpr const char *kDefaultCalendarUrl = "/team-sync-meeting.ics";
constexpr const char *kDefaultWeatherCity = "北京";
constexpr const char *kDefaultWeatherLat = "39.9042";
constexpr const char *kDefaultWeatherLon = "116.4074";
constexpr const char *kDefaultWeatherUrl =
    "http://api.open-meteo.com/v1/forecast?latitude=39.9042&longitude=116.4074&current=temperature_2m,relative_humidity_2m,weather_code&timezone=auto";
constexpr uint32_t kSettingsRevision = 2;
constexpr uint32_t kOldOneHourDefaultSec = 3600;
constexpr uint32_t kTwoHourDefaultSec = 7200;

}  // namespace

void SettingsStore::applyDefaults(WifiSettings &settings, size_t &calendar_event_count,
                                  uint16_t &next_calendar_event_id) {
  settings.sta_ssid = kDefaultStaSsid;
  settings.sta_user = kDefaultStaUser;
  settings.sta_pass = kDefaultStaPass;
  settings.sta_auth_mode = kDefaultStaAuthMode;
  settings.ui_language = kDefaultUiLanguage;
  settings.timezone = kDefaultTimezone;
  settings.photo_interval_sec = kTwoHourDefaultSec;
  settings.app_auto_switch_enabled = false;
  settings.app_switch_interval_sec = 3600;
  settings.calendar_enabled = true;
  settings.calendar_layout = "landscape_split";
  settings.calendar_refresh_sec = kTwoHourDefaultSec;
  settings.sleep_start_minute = 22 * 60;
  settings.sleep_end_minute = 8 * 60;
  settings.calendar_url = kDefaultCalendarUrl;
  settings.weather_city = kDefaultWeatherCity;
  settings.weather_lat = kDefaultWeatherLat;
  settings.weather_lon = kDefaultWeatherLon;
  settings.weather_url = kDefaultWeatherUrl;
  calendar_event_count = 0;
  next_calendar_event_id = 1;
}

void SettingsStore::normalize(WifiSettings &settings) {
  settings.sta_ssid.trim();
  settings.sta_user.trim();
  settings.sta_auth_mode.trim();
  settings.sta_auth_mode.toLowerCase();
  if (!(settings.sta_auth_mode == "auto" || settings.sta_auth_mode == "open" ||
        settings.sta_auth_mode == "personal" || settings.sta_auth_mode == "enterprise")) {
    settings.sta_auth_mode = kDefaultStaAuthMode;
  }
  settings.calendar_url.trim();
  settings.weather_city.trim();
  settings.weather_lat.trim();
  settings.weather_lon.trim();
  settings.weather_url.trim();
  settings.calendar_layout.trim();
  settings.calendar_layout.toLowerCase();
  if (!(settings.calendar_layout == "landscape_split" ||
        settings.calendar_layout == "portrait_split")) {
    settings.calendar_layout = "landscape_split";
  }

  settings.ui_language.trim();
  settings.ui_language.toLowerCase();
  if (!(settings.ui_language == "zh" || settings.ui_language == "en" ||
        settings.ui_language == "fr")) {
    settings.ui_language = kDefaultUiLanguage;
  }

  if (settings.app_switch_interval_sec < 60) settings.app_switch_interval_sec = 60;
  if (settings.app_switch_interval_sec > 86400) settings.app_switch_interval_sec = 86400;
  settings.sleep_start_minute = normalizeSleepWindowMinute(settings.sleep_start_minute, 22 * 60);
  settings.sleep_end_minute = normalizeSleepWindowMinute(settings.sleep_end_minute, 8 * 60);
  if (settings.sleep_start_minute == settings.sleep_end_minute) {
    settings.sleep_start_minute = 22 * 60;
    settings.sleep_end_minute = 8 * 60;
  }
}

void SettingsStore::fillEmptyValues(WifiSettings &settings) {
  if (settings.calendar_url.length() == 0) {
    settings.calendar_url = kDefaultCalendarUrl;
  }
  if (settings.weather_url.length() == 0) {
    settings.weather_url = kDefaultWeatherUrl;
  }
  if (settings.weather_city.length() == 0) {
    settings.weather_city = kDefaultWeatherCity;
  }
  if (settings.weather_lat.length() == 0) {
    settings.weather_lat = kDefaultWeatherLat;
  }
  if (settings.weather_lon.length() == 0) {
    settings.weather_lon = kDefaultWeatherLon;
  }
}

bool SettingsStore::load(Preferences &prefs, WifiSettings &settings,
                         uint16_t &next_calendar_event_id, String &packed_events) {
  size_t empty_count = 0;
  applyDefaults(settings, empty_count, next_calendar_event_id);

  if (!prefs.begin("config", false)) {
    packed_events = "";
    return false;
  }

  if (prefs.isKey("sta_ssid")) settings.sta_ssid = prefs.getString("sta_ssid", kDefaultStaSsid);
  if (prefs.isKey("sta_user")) settings.sta_user = prefs.getString("sta_user", kDefaultStaUser);
  if (prefs.isKey("sta_pass")) settings.sta_pass = prefs.getString("sta_pass", kDefaultStaPass);
  if (prefs.isKey("sta_auth")) settings.sta_auth_mode = prefs.getString("sta_auth", kDefaultStaAuthMode);
  if (prefs.isKey("ui_lang")) settings.ui_language = prefs.getString("ui_lang", kDefaultUiLanguage);
  if (prefs.isKey("timezone")) settings.timezone = prefs.getString("timezone", kDefaultTimezone);
  const uint32_t stored_revision = prefs.getUInt("cfg_rev", 0);
  const bool migrate_one_hour_defaults = stored_revision < kSettingsRevision;

  if (prefs.isKey("photo_sec")) {
    settings.photo_interval_sec = prefs.getUInt("photo_sec", kTwoHourDefaultSec);
    if (migrate_one_hour_defaults && settings.photo_interval_sec == kOldOneHourDefaultSec) {
      settings.photo_interval_sec = kTwoHourDefaultSec;
      prefs.putUInt("photo_sec", settings.photo_interval_sec);
    }
  }
  if (prefs.isKey("auto_switch")) {
    settings.app_auto_switch_enabled = prefs.getBool("auto_switch", false);
  }
  if (prefs.isKey("switch_sec")) {
    settings.app_switch_interval_sec = prefs.getUInt("switch_sec", 3600);
  }
  if (prefs.isKey("cal_en")) settings.calendar_enabled = prefs.getBool("cal_en", true);
  if (prefs.isKey("cal_layout")) settings.calendar_layout = prefs.getString("cal_layout", "landscape_split");
  if (prefs.isKey("cal_sec")) {
    settings.calendar_refresh_sec = prefs.getUInt("cal_sec", kTwoHourDefaultSec);
    if (migrate_one_hour_defaults && settings.calendar_refresh_sec == kOldOneHourDefaultSec) {
      settings.calendar_refresh_sec = kTwoHourDefaultSec;
      prefs.putUInt("cal_sec", settings.calendar_refresh_sec);
    }
  }
  if (prefs.isKey("sleep_start")) {
    settings.sleep_start_minute =
        static_cast<uint16_t>(prefs.getUInt("sleep_start", settings.sleep_start_minute));
  } else if (prefs.isKey("active_end")) {
    settings.sleep_start_minute =
        static_cast<uint16_t>(prefs.getUInt("active_end", settings.sleep_start_minute));
  }
  if (prefs.isKey("sleep_end")) {
    settings.sleep_end_minute =
        static_cast<uint16_t>(prefs.getUInt("sleep_end", settings.sleep_end_minute));
  } else if (prefs.isKey("active_start")) {
    settings.sleep_end_minute =
        static_cast<uint16_t>(prefs.getUInt("active_start", settings.sleep_end_minute));
  }
  if (prefs.isKey("cal_url")) settings.calendar_url = prefs.getString("cal_url", kDefaultCalendarUrl);
  if (prefs.isKey("weather_city")) settings.weather_city = prefs.getString("weather_city", kDefaultWeatherCity);
  if (prefs.isKey("weather_lat")) settings.weather_lat = prefs.getString("weather_lat", kDefaultWeatherLat);
  if (prefs.isKey("weather_lon")) settings.weather_lon = prefs.getString("weather_lon", kDefaultWeatherLon);
  if (prefs.isKey("weather_url")) settings.weather_url = prefs.getString("weather_url", kDefaultWeatherUrl);

  normalize(settings);
  fillEmptyValues(settings);

  next_calendar_event_id =
      static_cast<uint16_t>(prefs.getUInt("cal_next_id", static_cast<uint32_t>(next_calendar_event_id)));
  if (next_calendar_event_id == 0) {
    next_calendar_event_id = 1;
  }
  packed_events = prefs.getString("cal_events", "");
  if (stored_revision != kSettingsRevision) {
    prefs.putUInt("cfg_rev", kSettingsRevision);
  }
  prefs.end();
  return true;
}

bool SettingsStore::save(Preferences &prefs, const WifiSettings &settings,
                         uint16_t next_calendar_event_id, const String &packed_events) {
  if (!prefs.begin("config", false)) {
    return false;
  }
  prefs.putUInt("cfg_rev", kSettingsRevision);
  prefs.putString("sta_ssid", settings.sta_ssid);
  prefs.putString("sta_user", settings.sta_user);
  prefs.putString("sta_pass", settings.sta_pass);
  prefs.putString("sta_auth", settings.sta_auth_mode);
  prefs.putString("ui_lang", settings.ui_language);
  prefs.putString("timezone", settings.timezone);
  prefs.putUInt("photo_sec", settings.photo_interval_sec);
  prefs.putBool("auto_switch", settings.app_auto_switch_enabled);
  prefs.putUInt("switch_sec", settings.app_switch_interval_sec);
  prefs.putBool("cal_en", settings.calendar_enabled);
  prefs.putString("cal_layout", settings.calendar_layout);
  prefs.putUInt("cal_sec", settings.calendar_refresh_sec);
  prefs.putUInt("sleep_start", settings.sleep_start_minute);
  prefs.putUInt("sleep_end", settings.sleep_end_minute);
  prefs.putString("cal_url", settings.calendar_url);
  prefs.putString("weather_city", settings.weather_city);
  prefs.putString("weather_lat", settings.weather_lat);
  prefs.putString("weather_lon", settings.weather_lon);
  prefs.putString("weather_url", settings.weather_url);
  prefs.putUInt("cal_next_id", next_calendar_event_id);
  prefs.putString("cal_events", packed_events);
  prefs.end();
  return true;
}

const char *SettingsStore::defaultStaSsid() {
  return kDefaultStaSsid;
}

}  // namespace appfw
