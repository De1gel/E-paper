#ifndef APP_H
#define APP_H

#include <Arduino.h>
#include <SPI.h>

#include "system/InputManager.h"
#include "system/LedManager.h"
#include "system/ModeManager.h"
#include "system/WifiManager.h"

enum class AppState : uint8_t {
  Photo = 0,
  Calendar = 1,
};

class App {
 public:
  void begin();
  void update(uint32_t now_ms);
  void render();

 private:
  void handleInputEvent(appfw::InputEvent event, uint32_t now_ms);
  void updatePhotoCarousel(uint32_t now_ms);
  void nextPhoto(const char *reason, uint32_t now_ms);
  void prevPhoto(const char *reason, uint32_t now_ms);
  void beginDisplaySession();
  void endDisplaySession();
  void setState(AppState next);
  void renderPhotoPage();
  void initPhotoStorage();
  void refreshPhotoFileCount();
  bool renderEpd4PhotoAtIndex(uint16_t index);
  bool isEpd4Name(const String &name) const;
  void renderCalendarPage();
  void renderWhiteScreen();
  void waitEpdReadyWithLed();

  AppState state_ = AppState::Photo;
  uint32_t last_photo_switch_ms_ = 0;
  uint32_t photo_interval_ms_ = 3600000;
  uint16_t photo_index_ = 0;
  uint16_t photo_file_count_ = 0;
  uint16_t last_logged_photo_file_count_ = 0xFFFF;
  bool needs_render_ = true;
  SPIClass photo_sd_spi_{HSPI};
  bool photo_sd_ready_ = false;
  bool photo_sd_spi_started_ = false;

  appfw::InputManager input_;
  appfw::ModeManager mode_manager_;
  appfw::LedManager led_manager_;
  appfw::WifiManager wifi_manager_;
};

#endif
