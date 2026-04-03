#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <SPI.h>
#include <math.h>

class WebServer;
class Preferences;

namespace appfw {

class WifiManager {
 public:
  struct Settings {
    String sta_ssid;
    String sta_pass;
    String timezone;
    uint32_t photo_interval_sec = 3600;
    String weather_url;
  };

  void begin();
  void update(uint32_t now_ms);
  void startAP();
  void startSTA();
  void stop(const char *reason);

  bool consumeAutoExitRequested();
  const Settings &settings() const;

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
  void handleFilesList();
  void handleDirCreate();
  void handleFileDownload();
  void handleFileDelete();
  void handleWeatherTest();
  void handleStopPortal();
  void handleFileUpload();
  void handleNotFound();

  State state_ = State::Idle;
  Settings settings_{};
  bool auto_exit_requested_ = false;
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
  uint32_t last_activity_ms_ = 0;
  uint32_t last_sensor_poll_ms_ = 0;

  WebServer *server_ = nullptr;
  Preferences *prefs_ = nullptr;
  SPIClass sd_spi_{HSPI};
  bool sd_ready_ = false;
  bool sd_spi_started_ = false;
  bool web_fs_ready_ = false;

  static constexpr uint8_t kSdCsPin = 5;
  static constexpr uint8_t kSdSckPin = 18;
  static constexpr uint8_t kSdMisoPin = 19;
  static constexpr uint8_t kSdMosiPin = 23;
  static constexpr uint8_t kPeripheralPowerPin = 32;

  static constexpr uint32_t kStaConnectTimeoutMs = 30000;
  static constexpr uint32_t kApIdleTimeoutMs = 300000;
  static constexpr uint32_t kStaSessionTimeoutMs = 120000;
  static constexpr uint32_t kSensorPollIntervalMs = 10000;
};

}  // namespace appfw

#endif
