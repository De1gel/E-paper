#include "system/SettingsStore.h"

#include "system/CalendarSettings.h"

namespace appfw {
namespace {

constexpr const char *kDefaultStaSsid = "DESKTOP-09PTMRM 4607";
constexpr const char *kDefaultStaPass = "67O9b1-2";
constexpr const char *kDefaultUiLanguage = "zh";
constexpr const char *kDefaultTimezone = "Asia/Shanghai";
constexpr const char *kDefaultCalendarUrl = "";
constexpr const char *kDefaultWeatherCity = "Shanghai";
constexpr const char *kDefaultWeatherLat = "31.2304";
constexpr const char *kDefaultWeatherLon = "121.4737";
constexpr const char *kDefaultWeatherUrl =
    "https://api.open-meteo.com/v1/forecast?latitude=31.2304&longitude=121.4737&current=temperature_2m,relative_humidity_2m,weather_code&timezone=auto";

}  // namespace

void SettingsStore::applyDefaults(WifiSettings &settings, size_t &calendar_event_count,
                                  uint16_t &next_calendar_event_id) {
  settings.sta_ssid = kDefaultStaSsid;
  settings.sta_pass = kDefaultStaPass;
  settings.ui_language = kDefaultUiLanguage;
  settings.timezone = kDefaultTimezone;
  settings.photo_interval_sec = 3600;
  settings.calendar_enabled = false;
  settings.calendar_layout = "landscape_split";
  settings.calendar_refresh_sec = 900;
  settings.calendar_time_refresh_sec = 600;
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

  settings.calendar_time_refresh_sec =
      normalizeCalendarTimeRefreshSec(settings.calendar_time_refresh_sec);
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
  if (prefs.isKey("sta_pass")) settings.sta_pass = prefs.getString("sta_pass", kDefaultStaPass);
  if (prefs.isKey("ui_lang")) settings.ui_language = prefs.getString("ui_lang", kDefaultUiLanguage);
  if (prefs.isKey("timezone")) settings.timezone = prefs.getString("timezone", kDefaultTimezone);
  if (prefs.isKey("photo_sec")) settings.photo_interval_sec = prefs.getUInt("photo_sec", 3600);
  if (prefs.isKey("cal_en")) settings.calendar_enabled = prefs.getBool("cal_en", false);
  if (prefs.isKey("cal_layout")) settings.calendar_layout = prefs.getString("cal_layout", "landscape_split");
  if (prefs.isKey("cal_sec")) settings.calendar_refresh_sec = prefs.getUInt("cal_sec", 900);
  if (prefs.isKey("cal_time_sec")) {
    settings.calendar_time_refresh_sec =
        prefs.getUInt("cal_time_sec", settings.calendar_time_refresh_sec);
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
  prefs.end();
  return true;
}

bool SettingsStore::save(Preferences &prefs, const WifiSettings &settings,
                         uint16_t next_calendar_event_id, const String &packed_events) {
  if (!prefs.begin("config", false)) {
    return false;
  }
  prefs.putString("sta_ssid", settings.sta_ssid);
  prefs.putString("sta_pass", settings.sta_pass);
  prefs.putString("ui_lang", settings.ui_language);
  prefs.putString("timezone", settings.timezone);
  prefs.putUInt("photo_sec", settings.photo_interval_sec);
  prefs.putBool("cal_en", settings.calendar_enabled);
  prefs.putString("cal_layout", settings.calendar_layout);
  prefs.putUInt("cal_sec", settings.calendar_refresh_sec);
  prefs.putUInt("cal_time_sec", settings.calendar_time_refresh_sec);
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
