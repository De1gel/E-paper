#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <math.h>

class WebServer;
class Preferences;

namespace appfw {

class WifiManager {
 public:
  struct CalendarEvent {
    uint16_t id = 0;
    String title;
    String date;       // YYYY-MM-DD for one-time event.
    String time_hhmm;  // HH:MM
    String end_time_hhmm;  // Optional HH:MM, reserved for future sync.
    String color;      // black/white/yellow/red/blue/green
    String repeat;     // once/daily/weekly
    int8_t weekday = -1;  // 0-6 (Mon-Sun), used for weekly.
    String source;  // manual/outlook/google...
    String external_id;  // Provider-side stable id.
    String updated_at;   // Optional ISO-8601 or epoch string.
  };

  static constexpr size_t kMaxCalendarEvents = 24;

  struct Settings {
    String sta_ssid;
    String sta_pass;
    String ui_language;
    String timezone;
    uint32_t photo_interval_sec = 3600;
    bool calendar_enabled = false;
    String calendar_layout;
    uint32_t calendar_refresh_sec = 900;
    String calendar_url;
    String weather_city;
    String weather_lat;
    String weather_lon;
    String weather_url;
  };

  void begin();
  void update(uint32_t now_ms);
  void startAP();
  void startSTA();
  void stop(const char *reason);

  bool consumeAutoExitRequested();
  bool consumeStaConnectFailed();
  bool isStaConnected() const;
  const Settings &settings() const;
  size_t calendarEventCount() const;
  bool calendarEventAt(size_t index, CalendarEvent &event) const;

 private:
  enum class State : uint8_t {
    Idle = 0,
    ApRunning,
    StaConnecting,
    StaRunning,
  };

  void loadSettings();
  void saveSettings();
  void applyDefaultSettings();
  String serializeCalendarEvents() const;
  void deserializeCalendarEvents(const String &packed);
  String calendarEventsJson() const;
  bool normalizeCalendarTime(const String &raw, String &normalized) const;
  bool normalizeCalendarDate(const String &raw, String &normalized) const;
  String normalizeCalendarColor(const String &raw) const;
  String normalizeCalendarRepeat(const String &raw) const;
  String normalizeCalendarSource(const String &raw) const;
  String normalizeCalendarExternalId(const String &raw) const;
  String normalizeCalendarUpdatedAt(const String &raw) const;
  int findCalendarEventIndexById(uint16_t id) const;
  int findCalendarEventIndexByExternal(const String &source, const String &external_id) const;
  void registerWifiEvents();
  void handleWifiEvent(arduino_event_id_t event, arduino_event_info_t info);
  const char *wifiStatusName(int status) const;
  const char *wifiEventName(arduino_event_id_t event) const;
  const char *disconnectReasonName(uint8_t reason) const;
  void logStaScanResults();

  void startServer();
  void stopServer();
  void updateTimeout(uint32_t now_ms);
  void markActivity(uint32_t now_ms);

  void initSD();
  void deinitSD();
  void initWebFs();
  String listDirectoryJson(const char *path);
  String contentTypeForPath(const String &path) const;
  bool isSafePath(const String &path) const;
  bool isEpd4Path(const String &path) const;
  bool removePathRecursive(const String &path) const;
  String currentIp() const;
  void initSensors();
  void updateSensors(uint32_t now_ms);
  bool readAHT20(float &temperature_c, float &humidity_pct);
  int readBatteryMilliVolts(int pin) const;
  void detectBatteryPin();
  float estimateBatteryPercent(int battery_mv) const;

  void handleRoot();
  void handleStatus();
  void handleSettingsGet();
  void handleSettingsPost();
  void handleCalendarEventsGet();
  void handleCalendarEventsPost();
  void handleCalendarEventsDelete();
  void handleFilesList();
  void handleDirCreate();
  void handleGeocode();
  void handleFileDownload();
  void handleFileDelete();
  void handleWeatherTest();
  void handleStopPortal();
  void handleReboot();
  void handleFileUpload();
  void handleNotFound();

  State state_ = State::Idle;
  Settings settings_{};
  CalendarEvent calendar_events_[kMaxCalendarEvents];
  size_t calendar_event_count_ = 0;
  uint16_t next_calendar_event_id_ = 1;
  bool auto_exit_requested_ = false;
  bool sta_connect_failed_ = false;
  bool upload_ok_ = true;
  String upload_error_{};
  String upload_mode_{"normal"};
  String upload_tmp_path_{};
  bool aht_ready_ = false;
  float temperature_c_ = NAN;
  float humidity_pct_ = NAN;
  int battery_adc_pin_ = -1;
  int battery_mv_ = -1;

  uint32_t sta_connect_start_ms_ = 0;
  uint32_t sta_session_start_ms_ = 0;
  uint32_t last_activity_ms_ = 0;
  uint32_t last_sensor_poll_ms_ = 0;
  int last_sta_wifi_status_ = -1;

  WebServer *server_ = nullptr;
  Preferences *prefs_ = nullptr;
  SPIClass sd_spi_{HSPI};
  bool sd_ready_ = false;
  bool sd_spi_started_ = false;
  bool web_fs_ready_ = false;
  bool wifi_events_registered_ = false;

  static constexpr uint8_t kSdCsPin = 5;
  static constexpr uint8_t kSdSckPin = 18;
  static constexpr uint8_t kSdMisoPin = 19;
  static constexpr uint8_t kSdMosiPin = 23;
  static constexpr uint8_t kPeripheralPowerPin = 32;

  static constexpr uint32_t kStaConnectTimeoutMs = 30000;
  static constexpr uint32_t kApIdleTimeoutMs = 300000;
  static constexpr uint32_t kStaSessionTimeoutMs = 0;
  static constexpr uint32_t kSensorPollIntervalMs = 10000;
};

}  // namespace appfw

#endif
