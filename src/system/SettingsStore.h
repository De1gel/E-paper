#ifndef SYSTEM_SETTINGS_STORE_H
#define SYSTEM_SETTINGS_STORE_H

#include <Arduino.h>
#include <Preferences.h>

#include "system/CalendarData.h"

namespace appfw {

struct WifiSettings {
  String sta_ssid;
  String sta_pass;
  String ui_language;
  String timezone;
  uint32_t photo_interval_sec = 3600;
  bool calendar_enabled = false;
  String calendar_layout;
  uint32_t calendar_refresh_sec = 900;
  uint32_t calendar_time_refresh_sec = 600;
  String calendar_url;
  String weather_city;
  String weather_lat;
  String weather_lon;
  String weather_url;
};

class SettingsStore {
 public:
  static void applyDefaults(WifiSettings &settings, size_t &calendar_event_count,
                            uint16_t &next_calendar_event_id);
  static void normalize(WifiSettings &settings);
  static void fillEmptyValues(WifiSettings &settings);
  static bool load(Preferences &prefs, WifiSettings &settings,
                   uint16_t &next_calendar_event_id, String &packed_events);
  static bool save(Preferences &prefs, const WifiSettings &settings,
                   uint16_t next_calendar_event_id, const String &packed_events);

  static const char *defaultStaSsid();
};

}  // namespace appfw

#endif
