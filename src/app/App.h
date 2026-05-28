#ifndef APP_H
#define APP_H

#include <Arduino.h>
#include <time.h>

#include "calendar/CalendarLayout.h"
#include "calendar/CalendarModel.h"
#include "calendar/CalendarText.h"
#include "render/StripeBuffer.h"
#include "system/InputManager.h"
#include "system/LedManager.h"
#include "system/ModeManager.h"
#include "system/WifiManager.h"

class CalendarFrameSink;

enum class AppState : uint8_t {
  Photo = 0,
  Calendar = 1,
};

class App {
 public:
  void begin();
  void update(uint32_t now_ms);
  void render();
  bool canEnterLightSleep(uint32_t now_ms) const;
  uint32_t nextWakeDeadlineMs(uint32_t now_ms) const;
  bool shouldWakeFromSideKeys() const;
  void onEnterLightSleep(uint32_t deadline_ms);
  void cancelLightSleepEntry(const char *reason);
  void onWakeFromLightSleep(uint64_t slept_us, bool woke_from_gpio, bool wake_up_pressed,
                            bool wake_mid_pressed, bool wake_down_pressed);

 private:
  enum class CalendarLayout : uint8_t {
    LandscapeSplit = 0,  // Left half: calendar, right half: schedule
    PortraitSplit = 1,   // Top half: calendar, bottom half: schedule
  };

  void handleInputEvent(appfw::InputEvent event, uint32_t now_ms);
  void startOperationTrace(const char *source, const char *action, uint32_t now_ms);
  void finishOperationTrace(const char *result, uint32_t now_ms, const char *reason = nullptr);
  void setPeripheralPower(bool enabled);
  void updateClockAnchor(uint32_t now_ms);
  bool getLocalTimeSnapshot(uint32_t now_ms, struct tm &local_tm, time_t &local_epoch) const;
  void updateCalendarAutoRefresh(uint32_t now_ms);
  void updateAppAutoSwitch(uint32_t now_ms);
  void updateCalendarBackgroundSync(uint32_t now_ms);
  void applyCalendarLayoutFromConfig(bool force_apply);
  void queueSettingsApplyFullRefresh(uint32_t now_ms, const char *reason);
  void updatePhotoCarousel(uint32_t now_ms);
  void nextPhoto(const char *reason, uint32_t now_ms);
  void prevPhoto(const char *reason, uint32_t now_ms);
  bool ensureCalendarSyncBeforeFullRefresh(uint32_t now_ms);
  void beginDisplaySession();
  void endDisplaySession();
  void setState(AppState next);
  bool ensureCalendarFrameBuffer(const char *reason);
  bool ensureCalendarStripeBuffer();
  void rebuildCalendarSceneCache(const struct tm &local_tm, bool time_valid);
  void logCalendarHeap(const char *tag) const;
  void renderPhotoPage();
  void initPhotoStorage();
  bool ensurePhotoStorageMounted();
  void refreshPhotoFileCount();
  bool renderEpd4PhotoAtIndex(uint16_t index);
  bool isEpd4Name(const String &name) const;
  void renderCalendarPage(uint32_t now_ms);
  void clearCalendarFrame(uint8_t color_nibble);
  bool calendarUsesPortraitRotation() const;
  uint16_t calendarLogicalWidth() const;
  uint16_t calendarLogicalHeight() const;
  bool calendarLogicalToPhysical(uint16_t x, uint16_t y, uint16_t &physical_x,
                                 uint16_t &physical_y) const;
  calendar::Rect calendarLogicalRectToPhysical(const calendar::Rect &rect) const;
  void setCalendarPixel(uint16_t x, uint16_t y, uint8_t color_nibble);
  void fillCalendarRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color_nibble);
  void drawCalendarRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color_nibble);
  void drawCalendarText3x5(uint16_t x, uint16_t y, const String &text, uint8_t pixel_height,
                            uint8_t color_nibble,
                            calendar::TextFont font = calendar::TextFont::Auto,
                            calendar::TextAAMode aa_mode = calendar::TextAAMode::Threshold);
  void drawCalendarNumberInCell(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int day_number,
                                uint8_t scale, uint8_t color_nibble);
  void drawCalendarScene(const struct tm &local_tm, bool time_valid);
  void pushCalendarFullRefresh();
  void pushCalendarFullRefreshStriped(const calendar::CalendarModel &model,
                                      const calendar::CalendarLayout &layout);
  void renderWhiteScreen();
  void waitEpdReadyWithLed();
  bool isAnyWakeKeyPressed() const;
  uint32_t calendarSyncSignature() const;
  void startCalendarBackgroundSync(const char *reason);

  AppState state_ = AppState::Photo;
  uint32_t last_photo_switch_ms_ = 0;
  uint32_t last_app_switch_ms_ = 0;
  uint32_t photo_interval_ms_ = 3600000;
  uint16_t photo_index_ = 0;
  uint16_t photo_file_count_ = 0;
  uint16_t last_logged_photo_file_count_ = 0xFFFF;
  bool needs_render_ = true;
  bool peripheral_power_on_ = false;
  static constexpr size_t kCalendarFrameBytes = (800u * 480u) / 2u;
  static constexpr uint16_t kCalendarStripeRows = 32u;
  uint8_t *calendar_frame_ = nullptr;
  render::StripeBuffer calendar_stripe_;
  CalendarLayout calendar_layout_ = CalendarLayout::LandscapeSplit;
  calendar::CalendarModel calendar_model_cache_{};
  calendar::CalendarLayout calendar_layout_cache_{};
  bool force_calendar_full_refresh_ = true;
  bool calendar_pre_refresh_sync_waiting_ = false;
  bool calendar_pre_refresh_sync_started_session_ = false;
  bool calendar_skip_presync_once_ = false;
  bool calendar_start_background_sync_after_render_ = false;
  bool calendar_background_sync_active_ = false;
  bool calendar_background_sync_started_session_ = false;
  bool calendar_stop_sta_after_render_ = false;
  bool calendar_pre_refresh_led_active_ = false;
  bool calendar_pre_refresh_wifi_connected_ = false;
  bool calendar_pre_refresh_failed_ = false;
  uint32_t calendar_background_sync_signature_ = 0;
  uint32_t last_calendar_check_ms_ = 0;
  int32_t last_calendar_day_key_ = -1;
  int32_t last_calendar_render_minute_key_ = -1;
  bool clock_valid_ = false;
  time_t clock_anchor_epoch_ = 0;
  uint32_t clock_anchor_ms_ = 0;
  uint32_t sleep_inhibit_until_ms_ = 0;
  bool operation_trace_active_ = false;
  uint32_t operation_trace_id_ = 0;
  uint32_t operation_trace_start_ms_ = 0;
  String operation_trace_source_;
  String operation_trace_action_;
  bool pending_wake_log_ = false;
  bool pending_wake_gpio_ = false;
  bool pending_wake_up_pressed_ = false;
  bool pending_wake_mid_pressed_ = false;
  bool pending_wake_down_pressed_ = false;
  uint32_t pending_wake_ms_ = 0;
  uint32_t pending_wake_slept_ms_ = 0;
  String pending_wake_led_;
  String calendar_layout_cfg_cache_;
  bool calendar_frame_unavailable_logged_ = false;

  appfw::InputManager input_;
  appfw::ModeManager mode_manager_;
  appfw::LedManager led_manager_;
  appfw::WifiManager wifi_manager_;

  static constexpr uint32_t kLightSleepWakeInhibitMs = 1200u;

  friend class CalendarFrameSink;
};

#endif
