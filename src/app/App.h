#ifndef APP_H
#define APP_H

#include <Arduino.h>

#include "system/InputManager.h"
#include "system/LedManager.h"
#include "system/ModeManager.h"
#include "system/WifiManager.h"

enum class AppState : uint8_t {
  Photo = 0,
  Calendar = 1,
  Debug = 2,
};

class App {
 public:
  void begin();
  void update(uint32_t now_ms);
  void render();

 private:
  void handleInputEvent(appfw::InputEvent event, uint32_t now_ms);
  void updatePhotoCarousel(uint32_t now_ms);
  void nextPhoto(const char *reason);
  void prevPhoto(const char *reason);
  void beginDisplaySession();
  void endDisplaySession();
  void displayImage(const unsigned char *pic_data);
  void setState(AppState next);
  void renderPhotoPage();
  void renderCalendarPage();
  void drawPatternPaletteBands();
  void drawPatternHorizontalBars();
  void drawPatternChecker();
  void renderWhiteScreen();

  AppState state_ = AppState::Photo;
  uint32_t last_photo_switch_ms_ = 0;
  uint16_t photo_index_ = 0;
  bool needs_render_ = true;

  appfw::InputManager input_;
  appfw::ModeManager mode_manager_;
  appfw::LedManager led_manager_;
  appfw::WifiManager wifi_manager_;
};

#endif
