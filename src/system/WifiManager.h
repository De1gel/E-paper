#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <math.h>

#include "system/CalendarData.h"
#include "system/CalendarStore.h"
#include "system/SettingsStore.h"

class WebServer;
class Preferences;

namespace appfw {

class WifiManager {
 public:
  using Settings = WifiSettings;

  void begin();
  void update(uint32_t now_ms);
  void startAP();
  void startSTA();
  void stop(const char *reason);

  bool consumeAutoExitRequested();
  bool consumeStaConnectFailed();
  bool isStaConnecting() const;
  bool isStaConnected() const;
  bool hasStaCredentials() const;
  bool isCalendarSyncBusy() const;
  bool blocksLightSleep() const;
  void requestCalendarSyncNow();
  const Settings &settings() const;
  size_t calendarEventCount() const;
  bool calendarEventAt(size_t index, CalendarEvent &event) const;
  void sampleSensorsNow(bool assume_peripheral_powered = false);
  float temperatureC() const { return temperature_c_; }
  float humidityPct() const { return humidity_pct_; }
  String weatherCity() const { return settings_.weather_city; }

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
  void maybeSyncCalendarUrl(uint32_t now_ms);
  bool syncCalendarFromUrl(String &error_msg);
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
  bool serveWebAsset(const char *path, const char *content_type);
  String listDirectoryJson(const char *path);
  String contentTypeForPath(const String &path) const;
  bool isSafePath(const String &path) const;
  bool isEpd4Path(const String &path) const;
  bool removePathRecursive(const String &path) const;
  String currentIp() const;
  bool syncClockFromWeather(String &resolved_timezone, bool &timezone_updated, String &local_time,
                            String &time_sync_error, String &preview, int &http_status,
                            String &request_error);
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
  void handleCalendarSyncNow();
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
  CalendarStore calendar_store_{};
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
  uint32_t last_calendar_sync_ms_ = 0;
  bool calendar_sync_pending_ = false;
  time_t last_calendar_sync_epoch_ = 0;
  String last_calendar_sync_status_{"idle"};
  String last_calendar_sync_error_{};
  uint16_t last_calendar_sync_imported_ = 0;
  uint16_t last_calendar_sync_total_ = 0;
  uint16_t last_calendar_sync_vevents_ = 0;
  int last_sta_wifi_status_ = -1;

  WebServer *server_ = nullptr;
  Preferences *prefs_ = nullptr;
  SPIClass sd_spi_{HSPI};
  bool sd_ready_ = false;
  bool sd_spi_started_ = false;
  bool web_fs_ready_ = false;
  bool wifi_events_registered_ = false;
  static constexpr uint8_t kPeripheralPowerPin = 32;

  static constexpr uint8_t kSdCsPin = 5;
  static constexpr uint8_t kSdSckPin = 18;
  static constexpr uint8_t kSdMisoPin = 19;
  static constexpr uint8_t kSdMosiPin = 23;

  static constexpr uint32_t kStaConnectTimeoutMs = 30000;
  static constexpr uint32_t kApIdleTimeoutMs = 300000;
  static constexpr uint32_t kStaSessionTimeoutMs = 0;
  static constexpr uint32_t kSensorPollIntervalMs = 10000;
  static constexpr uint32_t kCalendarSyncMinIntervalMs = 900000;
};

}  // namespace appfw

#endif
