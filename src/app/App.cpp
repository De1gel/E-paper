#include "app/App.h"

#include <FS.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <vector>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Display_EPD_W21.h"
#include "Display_EPD_W21_spi.h"
#include "app/RefreshPolicy.h"
#include "calendar/CalendarLayout.h"
#include "calendar/CalendarModel.h"
#include "calendar/CalendarScene.h"
#include "calendar/CalendarText.h"
#include "render/SceneRasterizer.h"
#include "system/LogConfig.h"
#include "system/SdCard.h"

namespace {
constexpr uint8_t kKeyUpPin = 0;
constexpr uint8_t kKeyMidPin = 35;
constexpr uint8_t kKeyDownPin = 34;
constexpr uint8_t kIndicatorLedPin = 2;
constexpr uint8_t kPowerCtrlPin = 32;
constexpr size_t kEpd4Bytes = (800 * 480) / 2;
constexpr uint16_t kPhotoCount = 1;
constexpr uint16_t kScreenWidth = 800;
constexpr uint16_t kScreenHeight = 480;
constexpr uint32_t kClockMinValidEpoch = 1700000000UL;
constexpr uint32_t kCalendarCheckIntervalMs = 60000UL;
constexpr AppState kDebugBootState = AppState::Calendar;
constexpr uint8_t kDebugForcedCalendarRows = 0;

String twoDigits(int value) {
  if (value < 10) {
    return "0" + String(value);
  }
  return String(value);
}

const char *eventName(appfw::InputEvent event) {
  switch (event) {
    case appfw::InputEvent::UpShort:
      return "UpShort";
    case appfw::InputEvent::DownShort:
      return "DownShort";
    case appfw::InputEvent::MidShort:
      return "MidShort";
    case appfw::InputEvent::MidLong:
      return "MidLong";
    default:
      return "None";
  }
}

const char *opModeName(appfw::OperationMode mode) {
  switch (mode) {
    case appfw::OperationMode::Normal:
      return "Normal";
    case appfw::OperationMode::ConfigWait:
      return "ConfigWait";
    case appfw::OperationMode::ConfigAP:
      return "ConfigAP";
    case appfw::OperationMode::ConfigSTA:
      return "ConfigSTA";
    default:
      return "Unknown";
  }
}

const char *appStateName(AppState state) {
  return state == AppState::Photo ? "Photo" : "Calendar";
}

const char *appStateLogTag(AppState state) {
  return state == AppState::Photo ? "[PHOTO]" : "[CAL]";
}

uint32_t largest8BitHeap() {
  return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

uint16_t minuteOfDay(const struct tm &local_tm) {
  return static_cast<uint16_t>(local_tm.tm_hour * 60 + local_tm.tm_min);
}

bool isInSleepWindow(const struct tm &local_tm, const appfw::WifiSettings &settings) {
  const uint16_t now_minute = minuteOfDay(local_tm);
  if (settings.sleep_start_minute < settings.sleep_end_minute) {
    return now_minute >= settings.sleep_start_minute && now_minute < settings.sleep_end_minute;
  }
  return now_minute >= settings.sleep_start_minute || now_minute < settings.sleep_end_minute;
}

time_t windowBoundaryEpoch(const struct tm &local_tm, time_t local_epoch, uint16_t minute_of_day,
                           bool next_day) {
  struct tm boundary_tm = local_tm;
  boundary_tm.tm_sec = 0;
  boundary_tm.tm_min = minute_of_day % 60u;
  boundary_tm.tm_hour = minute_of_day / 60u;
  if (next_day) {
    boundary_tm.tm_mday += 1;
  }
  const time_t boundary_epoch = mktime(&boundary_tm);
  return (boundary_epoch > local_epoch) ? boundary_epoch : local_epoch;
}

time_t nextSleepEndEpoch(const struct tm &local_tm, time_t local_epoch,
                         const appfw::WifiSettings &settings) {
  const uint16_t now_minute = minuteOfDay(local_tm);
  const bool next_day = settings.sleep_start_minute > settings.sleep_end_minute &&
                        now_minute >= settings.sleep_start_minute;
  return windowBoundaryEpoch(local_tm, local_epoch, settings.sleep_end_minute, next_day);
}

time_t nextSleepStartEpoch(const struct tm &local_tm, time_t local_epoch,
                           const appfw::WifiSettings &settings) {
  const uint16_t now_minute = minuteOfDay(local_tm);
  bool next_day = false;
  if (settings.sleep_start_minute < settings.sleep_end_minute) {
    next_day = now_minute >= settings.sleep_start_minute;
  } else {
    next_day = now_minute >= settings.sleep_start_minute || now_minute < settings.sleep_end_minute;
  }
  return windowBoundaryEpoch(local_tm, local_epoch, settings.sleep_start_minute, next_day);
}

uint32_t deadlineFromEpoch(uint32_t now_ms, time_t local_epoch, time_t deadline_epoch) {
  if (deadline_epoch <= local_epoch) {
    return now_ms;
  }
  const uint64_t delta_ms = static_cast<uint64_t>(deadline_epoch - local_epoch) * 1000ULL;
  const uint64_t max_delta = 0xFFFFFFFFULL - now_ms;
  return now_ms + static_cast<uint32_t>((delta_ms > max_delta) ? max_delta : delta_ms);
}

}  // namespace

class CalendarFrameSink : public calendar::SceneSink {
 public:
  explicit CalendarFrameSink(App &app) : app_(app) {}

  void fillRect(const calendar::Rect &rect, uint8_t color_nibble) override {
    app_.fillCalendarRect(rect.x, rect.y, rect.w, rect.h, color_nibble);
  }

  void strokeRect(const calendar::Rect &rect, uint8_t color_nibble) override {
    app_.drawCalendarRect(rect.x, rect.y, rect.w, rect.h, color_nibble);
  }

  void text(uint16_t x, uint16_t y, const String &text, uint8_t pixel_height, uint8_t color_nibble,
            calendar::TextFont font, calendar::TextAAMode aa_mode) override {
    app_.drawCalendarText3x5(x, y, text, pixel_height, color_nibble, font, aa_mode);
  }

 private:
  App &app_;
};

namespace {

uint32_t saturatingAddMs(uint32_t base_ms, uint32_t delta_ms) {
  const uint64_t sum = static_cast<uint64_t>(base_ms) + delta_ms;
  return (sum > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : static_cast<uint32_t>(sum);
}

}  // namespace

void App::begin() {
  state_ = kDebugBootState;
  photo_index_ = 0;
  last_photo_switch_ms_ = millis();
  last_app_switch_ms_ = last_photo_switch_ms_;
  // Always redraw the default page after boot/reset so the panel state matches app state.
  needs_render_ = true;
  calendar_layout_ = CalendarLayout::LandscapeSplit;
  force_calendar_full_refresh_ = true;
  last_calendar_check_ms_ = 0;
  last_calendar_day_key_ = -1;
  last_calendar_render_minute_key_ = -1;
  clock_valid_ = false;
  clock_anchor_epoch_ = 0;
  clock_anchor_ms_ = 0;
  sleep_inhibit_until_ms_ = saturatingAddMs(last_photo_switch_ms_, kLightSleepWakeInhibitMs);
  input_.begin(kKeyUpPin, kKeyMidPin, kKeyDownPin);
  mode_manager_.begin(last_photo_switch_ms_);
  led_manager_.begin(kIndicatorLedPin);
  wifi_manager_.begin();
  applyCalendarLayoutFromConfig(true);
  photo_interval_ms_ = wifi_manager_.settings().photo_interval_sec * 1000UL;
  if (photo_interval_ms_ < 30000UL) {
    photo_interval_ms_ = 30000UL;
  }
  pinMode(kPowerCtrlPin, OUTPUT);
  setPeripheralPower(false);
  initPhotoStorage();
  refreshPhotoFileCount();

  Serial.println("[SYSTEM] begin");
  Serial.printf("[INPUT] key pins up=%u mid=%u down=%u\n", kKeyUpPin, kKeyMidPin,
                kKeyDownPin);
  Serial.printf("[POWER] epd rail pin=%u default=OFF\n", kPowerCtrlPin);
  Serial.printf("[PHOTO] interval=%lus\n", static_cast<unsigned long>(photo_interval_ms_ / 1000UL));
  Serial.printf("[PHOTO] /pic epd4 count=%u\n", photo_file_count_);
  Serial.println("[SYSTEM] boot render queued");
}

void App::startOperationTrace(const char *source, const char *action, uint32_t now_ms) {
  if (operation_trace_active_) {
    finishOperationTrace("preempted", now_ms);
  }
  operation_trace_active_ = true;
  ++operation_trace_id_;
  operation_trace_start_ms_ = now_ms;
  operation_trace_source_ = source ? source : "unknown";
  operation_trace_action_ = action ? action : "unknown";
  Serial.printf("\n\n\n[OP][START] id=%lu source=%s action=%s state=%s mode=%s t=%lums\n",
                static_cast<unsigned long>(operation_trace_id_),
                operation_trace_source_.c_str(),
                operation_trace_action_.c_str(),
                appStateName(state_),
                opModeName(mode_manager_.mode()),
                static_cast<unsigned long>(now_ms));
  if (pending_wake_log_) {
    if ((now_ms - pending_wake_ms_) <= 5000u) {
      Serial.printf("[SLEEP] wake source=%s slept=%lums keys up=%d mid=%d down=%d led=%s\n",
                    pending_wake_gpio_ ? "gpio" : "timer_or_other",
                    static_cast<unsigned long>(pending_wake_slept_ms_),
                    pending_wake_up_pressed_ ? 1 : 0,
                    pending_wake_mid_pressed_ ? 1 : 0,
                    pending_wake_down_pressed_ ? 1 : 0,
                    pending_wake_led_.c_str());
    }
    pending_wake_log_ = false;
  }
}

void App::finishOperationTrace(const char *result, uint32_t now_ms, const char *reason) {
  if (!operation_trace_active_) {
    return;
  }
  if (reason && reason[0] != '\0') {
    Serial.printf("[OP][END] id=%lu result=%s reason=%s elapsed=%lums\n\n\n",
                  static_cast<unsigned long>(operation_trace_id_),
                  result ? result : "done",
                  reason,
                  static_cast<unsigned long>(now_ms - operation_trace_start_ms_));
  } else {
    Serial.printf("[OP][END] id=%lu result=%s elapsed=%lums\n\n\n",
                  static_cast<unsigned long>(operation_trace_id_),
                  result ? result : "done",
                  static_cast<unsigned long>(now_ms - operation_trace_start_ms_));
  }
  operation_trace_active_ = false;
}

void App::logCalendarHeap(const char *tag) const {
  if (!appfw::kDebugLogs) {
    return;
  }
  Serial.printf("[CAL] heap %s free=%lu largest=%lu\n", tag ? tag : "-",
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(largest8BitHeap()));
}

bool App::ensureCalendarFrameBuffer(const char *reason) {
  if (calendar_frame_ != nullptr) {
    return true;
  }

  const char *why = reason ? reason : "unknown";
  const size_t largest = largest8BitHeap();
  if (largest < kCalendarFrameBytes) {
    if (!calendar_frame_unavailable_logged_) {
      calendar_frame_unavailable_logged_ = true;
      Serial.printf("[CAL] framebuffer unavailable bytes=%u largest=%lu mode=striped\n",
                    static_cast<unsigned>(kCalendarFrameBytes),
                    static_cast<unsigned long>(largest));
    }
    return false;
  }

  logCalendarHeap("before_alloc");
  for (uint8_t attempt = 1; attempt <= 2; ++attempt) {
    calendar_frame_ = static_cast<uint8_t *>(
        heap_caps_malloc(kCalendarFrameBytes, MALLOC_CAP_8BIT));
    if (calendar_frame_ != nullptr) {
      Serial.printf("[CAL] framebuffer alloc ok bytes=%u attempt=%u reason=%s\n",
                    static_cast<unsigned>(kCalendarFrameBytes),
                    static_cast<unsigned>(attempt), why);
      clearCalendarFrame(white);
      logCalendarHeap("after_alloc");
      return true;
    }
    delay(2);
    yield();
  }

  Serial.printf("[CAL] framebuffer alloc failed bytes=%u reason=%s\n",
                static_cast<unsigned>(kCalendarFrameBytes), why);
  logCalendarHeap("alloc_failed");
  return false;
}

bool App::ensureCalendarStripeBuffer() {
  if (calendar_stripe_.ready()) {
    return true;
  }
  logCalendarHeap("before_stripe_alloc");
  const bool ok = calendar_stripe_.ensure(kScreenWidth, kCalendarStripeRows);
  if (ok) {
    Serial.printf("[CAL] stripe buffer alloc ok bytes=%u rows=%u\n",
                  static_cast<unsigned>(calendar_stripe_.sizeBytes()),
                  static_cast<unsigned>(kCalendarStripeRows));
    logCalendarHeap("after_stripe_alloc");
    return true;
  }
  Serial.printf("[CAL] stripe buffer alloc failed bytes=%u rows=%u\n",
                static_cast<unsigned>((kScreenWidth / 2u) * kCalendarStripeRows),
                static_cast<unsigned>(kCalendarStripeRows));
  logCalendarHeap("stripe_alloc_failed");
  return false;
}

void App::setPeripheralPower(bool enabled) {
  if (peripheral_power_on_ == enabled) {
    return;
  }
  if (!enabled && appfw::isSdCardMounted()) {
    if (wifi_manager_.blocksLightSleep()) {
      Serial.println("[POWER] peripheral rail kept ON for active WiFi/SD");
      return;
    }
    appfw::unmountSdCard("peripheral_power_off");
  }
  digitalWrite(kPowerCtrlPin, enabled ? HIGH : LOW);
  peripheral_power_on_ = enabled;
  delay(3);
  Serial.printf("[POWER] peripheral rail=%s\n", enabled ? "ON" : "OFF");
}

void App::updateClockAnchor(uint32_t now_ms) {
  const time_t sys_now = time(nullptr);
  if (sys_now < static_cast<time_t>(kClockMinValidEpoch)) {
    return;
  }

  if (!clock_valid_) {
    clock_valid_ = true;
    clock_anchor_epoch_ = sys_now;
    clock_anchor_ms_ = now_ms;
    Serial.printf("[TIME] clock anchor set epoch=%lu\n", static_cast<unsigned long>(sys_now));
    if (state_ == AppState::Calendar) {
      force_calendar_full_refresh_ = true;
      needs_render_ = true;
      Serial.println("[TIME] clock became valid -> calendar render");
    }
    return;
  }

  const uint32_t delta_ms = now_ms - clock_anchor_ms_;
  const time_t est_now = clock_anchor_epoch_ + static_cast<time_t>(delta_ms / 1000UL);
  const long drift = static_cast<long>(sys_now - est_now);
  if (labs(drift) > 2L) {
    bool display_changed = true;
    struct tm est_tm {};
    struct tm sys_tm {};
    if (localtime_r(&est_now, &est_tm) != nullptr && localtime_r(&sys_now, &sys_tm) != nullptr) {
      display_changed = !appfw::sameCalendarMinute(est_tm, sys_tm);
    }
    clock_anchor_epoch_ = sys_now;
    clock_anchor_ms_ = now_ms;
    Serial.printf("[TIME] clock anchor corrected drift=%lds epoch=%lu\n", drift,
                  static_cast<unsigned long>(sys_now));
    if (display_changed && state_ == AppState::Calendar) {
      force_calendar_full_refresh_ = true;
      needs_render_ = true;
      Serial.println("[TIME] clock corrected -> calendar render");
    } else if (!display_changed) {
      Serial.println("[TIME] clock corrected without visible calendar change");
    }
  }
}

bool App::getLocalTimeSnapshot(uint32_t now_ms, struct tm &local_tm, time_t &local_epoch) const {
  if (!clock_valid_) {
    const time_t sys_now = time(nullptr);
    if (sys_now >= static_cast<time_t>(kClockMinValidEpoch)) {
      local_epoch = sys_now;
      return localtime_r(&local_epoch, &local_tm) != nullptr;
    }

    static bool fallback_logged = false;
    const time_t fallback_base = appfw::fallbackClockBaseEpoch();
    local_epoch =
        ((fallback_base >= 0) ? fallback_base : 0) + static_cast<time_t>(now_ms / 1000UL);
    if (!fallback_logged) {
      fallback_logged = true;
      Serial.println("[TIME] using fallback local clock starting at 2026-01-01 12:00:00");
    }
    return localtime_r(&local_epoch, &local_tm) != nullptr;
  }
  const uint32_t delta_ms = now_ms - clock_anchor_ms_;
  local_epoch = clock_anchor_epoch_ + static_cast<time_t>(delta_ms / 1000UL);
  return localtime_r(&local_epoch, &local_tm) != nullptr;
}

void App::updateCalendarAutoRefresh(uint32_t now_ms) {
  if (state_ != AppState::Calendar || mode_manager_.mode() != appfw::OperationMode::Normal) {
    return;
  }
  if (needs_render_ || calendar_pre_refresh_sync_waiting_) {
    return;
  }

  struct tm local_tm {};
  time_t local_epoch = 0;
  if (!getLocalTimeSnapshot(now_ms, local_tm, local_epoch)) {
    return;
  }
  if (isInSleepWindow(local_tm, wifi_manager_.settings())) {
    return;
  }
  const int32_t key = appfw::dayKeyFromTm(local_tm);
  if (key != last_calendar_day_key_) {
    last_calendar_day_key_ = key;
    force_calendar_full_refresh_ = true;
    needs_render_ = true;
    startOperationTrace("auto", "CalendarDay", now_ms);
    Serial.printf("[AUTO] trigger=calendar_day key=%ld\n", static_cast<long>(key));
    return;
  }

  uint32_t check_interval_ms = wifi_manager_.settings().calendar_refresh_sec * 1000UL;
  if (check_interval_ms < 60000UL) {
    check_interval_ms = 60000UL;
  }
  if (last_calendar_check_ms_ == 0) {
    last_calendar_check_ms_ = now_ms;
    return;
  }
  if (last_calendar_check_ms_ != 0 && (now_ms - last_calendar_check_ms_) < check_interval_ms) {
    return;
  }
  last_calendar_check_ms_ = now_ms;
  needs_render_ = true;
  startOperationTrace("auto", "CalendarPeriodic", now_ms);
  Serial.printf("[AUTO] trigger=calendar_periodic epoch=%lu\n",
                static_cast<unsigned long>(local_epoch));
}

void App::updateAppAutoSwitch(uint32_t now_ms) {
  const appfw::WifiSettings &settings = wifi_manager_.settings();
  if (!settings.app_auto_switch_enabled ||
      mode_manager_.mode() != appfw::OperationMode::Normal ||
      needs_render_ ||
      calendar_pre_refresh_sync_waiting_) {
    return;
  }
  struct tm local_tm {};
  time_t local_epoch = 0;
  if (getLocalTimeSnapshot(now_ms, local_tm, local_epoch) &&
      isInSleepWindow(local_tm, settings)) {
    return;
  }
  uint32_t interval_ms = settings.app_switch_interval_sec * 1000UL;
  if (interval_ms < 60000UL) {
    interval_ms = 60000UL;
  }
  if (last_app_switch_ms_ == 0u) {
    last_app_switch_ms_ = now_ms;
    return;
  }
  if ((now_ms - last_app_switch_ms_) < interval_ms) {
    return;
  }
  last_app_switch_ms_ = now_ms;
  startOperationTrace("auto", "AppAutoSwitch", now_ms);
  setState((state_ == AppState::Photo) ? AppState::Calendar : AppState::Photo);
  Serial.printf("[AUTO] trigger=app_auto_switch state=%s interval=%lus\n",
                appStateName(state_),
                static_cast<unsigned long>(interval_ms / 1000UL));
}

uint32_t App::calendarSyncSignature() const {
  uint32_t hash = 2166136261UL;
  auto mixByte = [&hash](uint8_t value) {
    hash ^= value;
    hash *= 16777619UL;
  };
  auto mixString = [&mixByte](const String &value) {
    for (size_t i = 0; i < value.length(); ++i) {
      mixByte(static_cast<uint8_t>(value[i]));
    }
    mixByte(0xFFu);
  };
  auto mixInt = [&mixByte](int32_t value) {
    for (uint8_t i = 0; i < 4u; ++i) {
      mixByte(static_cast<uint8_t>((static_cast<uint32_t>(value) >> (i * 8u)) & 0xFFu));
    }
  };
  mixInt(wifi_manager_.weatherCode());
  mixInt(static_cast<int32_t>(wifi_manager_.calendarEventCount()));
  mixInt(static_cast<int32_t>(wifi_manager_.calendarMonthSummarySignature()));
  for (size_t i = 0; i < wifi_manager_.calendarEventCount(); ++i) {
    appfw::CalendarEvent event;
    if (!wifi_manager_.calendarEventAt(i, event)) {
      continue;
    }
    mixInt(event.id);
    mixString(event.title);
    mixString(event.date);
    mixString(event.time_hhmm);
    mixString(event.end_time_hhmm);
    mixString(event.color);
    mixString(event.repeat);
    mixInt(event.weekday);
    mixString(event.source);
    mixString(event.external_id);
    mixString(event.updated_at);
  }
  return hash;
}

void App::startCalendarBackgroundSync(const char *reason) {
  if (calendar_background_sync_active_ || !wifi_manager_.hasStaCredentials()) {
    return;
  }
  calendar_background_sync_signature_ = calendarSyncSignature();
  calendar_background_sync_active_ = true;
  calendar_background_sync_started_session_ = false;
  if (wifi_manager_.isStaConnected()) {
    wifi_manager_.syncWeatherNow(reason ? reason : "calendar_background_sync");
    wifi_manager_.requestCalendarSyncNow();
    Serial.printf("[CAL] background sync requested on active STA reason=%s sig=%lu\n",
                  reason ? reason : "unknown",
                  static_cast<unsigned long>(calendar_background_sync_signature_));
  } else {
    wifi_manager_.startStaPreRefreshSync();
    calendar_background_sync_started_session_ = true;
    Serial.printf("[CAL] background sync starting STA reason=%s sig=%lu\n",
                  reason ? reason : "unknown",
                  static_cast<unsigned long>(calendar_background_sync_signature_));
  }
}

void App::updateCalendarBackgroundSync(uint32_t now_ms) {
  (void)now_ms;
  if (!calendar_background_sync_active_) {
    return;
  }
  if (wifi_manager_.isStaConnecting() || wifi_manager_.isCalendarSyncBusy()) {
    return;
  }

  const uint32_t next_signature = calendarSyncSignature();
  const bool changed = next_signature != calendar_background_sync_signature_;
  const bool should_stop_sta = calendar_background_sync_started_session_;
  calendar_background_sync_active_ = false;
  calendar_background_sync_started_session_ = false;
  calendar_background_sync_signature_ = next_signature;

  if (changed && state_ == AppState::Calendar) {
    force_calendar_full_refresh_ = true;
    needs_render_ = true;
    calendar_skip_presync_once_ = true;
    calendar_start_background_sync_after_render_ = false;
    calendar_stop_sta_after_render_ = should_stop_sta;
    Serial.printf("[CAL] background sync changed -> full refresh sig=%lu\n",
                  static_cast<unsigned long>(next_signature));
    return;
  }

  if (should_stop_sta) {
    wifi_manager_.stop(changed ? "calendar_background_sync_changed_not_visible"
                               : "calendar_background_sync_no_change");
  }
  Serial.printf("[CAL] background sync done changed=%s sig=%lu\n",
                changed ? "true" : "false",
                static_cast<unsigned long>(next_signature));
}

void App::applyCalendarLayoutFromConfig(bool force_apply) {
  String layout = wifi_manager_.settings().calendar_layout;
  layout.trim();
  layout.toLowerCase();
  if (!(layout == "landscape_split" || layout == "portrait_split")) {
    layout = "landscape_split";
  }
  if (!force_apply && layout == calendar_layout_cfg_cache_) {
    return;
  }
  calendar_layout_cfg_cache_ = layout;
  const CalendarLayout next =
      (layout == "portrait_split") ? CalendarLayout::PortraitSplit : CalendarLayout::LandscapeSplit;
  if (force_apply || next != calendar_layout_) {
    calendar_layout_ = next;
    force_calendar_full_refresh_ = true;
    if (state_ == AppState::Calendar) {
      needs_render_ = true;
    }
    Serial.printf("[CAL] layout config -> %s\n",
                  (calendar_layout_ == CalendarLayout::LandscapeSplit) ? "landscape_split"
                                                                        : "portrait_split");
  }
}

void App::queueSettingsApplyFullRefresh(uint32_t now_ms, const char *reason) {
  if (!wifi_manager_.consumeSettingsApplyRefreshRequested()) {
    Serial.printf("[CONFIG] WiFi exited without saved settings reason=%s\n",
                  reason ? reason : "unknown");
    finishOperationTrace("ok", now_ms, reason);
    return;
  }
  force_calendar_full_refresh_ = true;
  needs_render_ = true;
  last_app_switch_ms_ = now_ms;
  Serial.printf("[CONFIG] WiFi exited with saved settings -> full refresh queued reason=%s\n",
                reason ? reason : "unknown");
}

void App::update(uint32_t now_ms) {
  input_.update(now_ms);

  appfw::InputEvent event = appfw::InputEvent::None;
  while (input_.pollEvent(event)) {
    handleInputEvent(event, now_ms);
  }

  mode_manager_.update(now_ms);
  wifi_manager_.update(now_ms);
  now_ms = millis();
  updateCalendarBackgroundSync(now_ms);
  updateClockAnchor(now_ms);
  applyCalendarLayoutFromConfig(false);
  const bool sta_connect_failed = wifi_manager_.consumeStaConnectFailed();
  bool handled_manual_sta_failure = false;
  if (sta_connect_failed && calendar_pre_refresh_sync_waiting_) {
    calendar_pre_refresh_failed_ = true;
  }
  if (sta_connect_failed && mode_manager_.mode() == appfw::OperationMode::ConfigSTA) {
    mode_manager_.forceConfigWait(now_ms, "sta_connect_failed");
    led_manager_.stopEffects("sta_connect_failed");
    finishOperationTrace("failed", millis(), "sta_connect_failed");
    handled_manual_sta_failure = true;
  }
  const uint32_t latest_interval_ms = wifi_manager_.settings().photo_interval_sec * 1000UL;
  if (latest_interval_ms >= 30000UL && latest_interval_ms != photo_interval_ms_) {
    photo_interval_ms_ = latest_interval_ms;
    Serial.printf("[PHOTO] interval updated=%lus\n",
                  static_cast<unsigned long>(photo_interval_ms_ / 1000UL));
  }

  const bool wifi_auto_exit = wifi_manager_.consumeAutoExitRequested();
  if (wifi_auto_exit && !handled_manual_sta_failure) {
    mode_manager_.forceNormal(now_ms, "wifi_session_timeout");
    queueSettingsApplyFullRefresh(now_ms, "auto_exit");
  }

  if (wifi_manager_.consumeManualStaSyncSettled()) {
    last_app_switch_ms_ = now_ms;
    led_manager_.showConfigSessionOn("manual_sta_ready");
    finishOperationTrace("ok", millis(), "manual_sta_ready");
  }

  if (wifi_manager_.consumeApClientConnected()) {
    led_manager_.showConfigSessionOn("ap_client_connected");
    finishOperationTrace("ok", millis(), "ap_client_connected");
  }

  if (mode_manager_.consumeApRequest()) {
    led_manager_.startDoubleBlink("ap_wait_client");
    wifi_manager_.startAP();
    if (operation_trace_active_) {
      finishOperationTrace("ok", millis(), "ap_started");
    }
  }
  if (mode_manager_.consumeStaRequest()) {
    led_manager_.startDoubleBlink("sta_connecting");
    wifi_manager_.startSTA();
  }
  if (mode_manager_.consumeStopWifiRequest()) {
    wifi_manager_.stop("manual_key_exit_config");
    queueSettingsApplyFullRefresh(now_ms, "manual_key_exit_config");
    if (operation_trace_active_) {
      finishOperationTrace("ok", millis(), "wifi_stopped");
    }
  }
  if (mode_manager_.consumeWhiteScreenRequest()) {
    renderWhiteScreen();
    if (operation_trace_active_) {
      finishOperationTrace("ok", millis(), "white_screen_done");
    }
  }

  if (mode_manager_.mode() == appfw::OperationMode::Normal) {
    updateAppAutoSwitch(now_ms);
    if (state_ == AppState::Photo) {
      updatePhotoCarousel(now_ms);
    } else if (state_ == AppState::Calendar) {
      updateCalendarAutoRefresh(now_ms);
    }
  }

  led_manager_.update(mode_manager_.mode(), now_ms, wifi_manager_.isStaConnected());
}

void App::handleInputEvent(appfw::InputEvent event, uint32_t now_ms) {
  startOperationTrace("user", eventName(event), now_ms);
  Serial.printf("[INPUT] event=%s\n", eventName(event));
  sleep_inhibit_until_ms_ = saturatingAddMs(now_ms, kLightSleepWakeInhibitMs);
  const appfw::OperationMode previous_mode = mode_manager_.mode();
  mode_manager_.onInputEvent(event, now_ms);
  const appfw::OperationMode current_mode = mode_manager_.mode();

  if (previous_mode == appfw::OperationMode::Normal &&
      current_mode == appfw::OperationMode::Normal) {
    if (event == appfw::InputEvent::MidShort) {
      led_manager_.triggerDoubleBlink("page_switch");
      const AppState previous_state = state_;
      const AppState next_state =
          (state_ == AppState::Photo) ? AppState::Calendar : AppState::Photo;
      setState(next_state);
      Serial.printf("%s page switch %s->%s\n",
                    appStateLogTag(next_state),
                    appStateName(previous_state),
                    appStateName(next_state));
    } else if (state_ == AppState::Photo && event == appfw::InputEvent::UpShort) {
      prevPhoto("key_up", now_ms);
    } else if (state_ == AppState::Photo && event == appfw::InputEvent::DownShort) {
      nextPhoto("key_down", now_ms);
    }
  }
  if (!needs_render_ &&
      current_mode != appfw::OperationMode::ConfigAP &&
      current_mode != appfw::OperationMode::ConfigSTA) {
    finishOperationTrace("ok", millis());
  }
}

void App::render() {
  if (!needs_render_) {
    return;
  }
  const appfw::OperationMode mode = mode_manager_.mode();
  if (mode == appfw::OperationMode::ConfigWait ||
      mode == appfw::OperationMode::ConfigAP ||
      mode == appfw::OperationMode::ConfigSTA) {
    return;
  }
  const uint32_t now_ms = millis();
  if (state_ == AppState::Calendar && !ensureCalendarSyncBeforeFullRefresh(now_ms)) {
    return;
  }
  const uint32_t render_begin_ms = millis();
  led_manager_.startBreath(state_ == AppState::Photo ? "photo_render" : "calendar_render");

  Serial.printf("%s render begin\n", appStateLogTag(state_));
  beginDisplaySession();
  wifi_manager_.sampleSensorsNow(true);

  if (state_ == AppState::Photo) {
    renderPhotoPage();
  } else if (state_ == AppState::Calendar) {
    renderCalendarPage(now_ms);
  } else {
    renderCalendarPage(now_ms);
  }

  endDisplaySession();
  if (calendar_start_background_sync_after_render_ && state_ == AppState::Calendar) {
    calendar_start_background_sync_after_render_ = false;
    startCalendarBackgroundSync("calendar_fast_render");
  }
  if (calendar_stop_sta_after_render_) {
    wifi_manager_.stop("calendar_post_refresh_sync_done");
    peripheral_power_on_ = false;
    calendar_stop_sta_after_render_ = false;
  }
  led_manager_.stopEffects("render_done");
  Serial.printf("%s render done elapsed=%lums led=%s\n",
                appStateLogTag(state_),
                static_cast<unsigned long>(millis() - render_begin_ms),
                led_manager_.currentStateName());
  needs_render_ = false;
  led_manager_.update(mode_manager_.mode(), millis(), wifi_manager_.isStaConnected());
}

bool App::isAnyWakeKeyPressed() const {
  return digitalRead(kKeyUpPin) == LOW ||
         digitalRead(kKeyMidPin) == LOW ||
         digitalRead(kKeyDownPin) == LOW;
}

bool App::shouldWakeFromSideKeys() const {
  return state_ == AppState::Photo || mode_manager_.mode() != appfw::OperationMode::Normal;
}

bool App::canEnterLightSleep(uint32_t now_ms) const {
  if (mode_manager_.mode() != appfw::OperationMode::Normal) {
    return false;
  }
  if (needs_render_ || peripheral_power_on_) {
    return false;
  }
  if (calendar_pre_refresh_sync_waiting_ || calendar_pre_refresh_sync_started_session_) {
    return false;
  }
  if (calendar_background_sync_active_) {
    return false;
  }
  if (wifi_manager_.blocksLightSleep()) {
    return false;
  }
  if (now_ms < sleep_inhibit_until_ms_) {
    return false;
  }
  if (isAnyWakeKeyPressed()) {
    return false;
  }
  return state_ == AppState::Photo || state_ == AppState::Calendar;
}

uint32_t App::nextWakeDeadlineMs(uint32_t now_ms) const {
  uint32_t deadline_ms = saturatingAddMs(now_ms, 1000u);
  if (state_ != AppState::Photo && state_ != AppState::Calendar) {
    return deadline_ms;
  }

  struct tm local_tm {};
  time_t local_epoch = 0;
  const bool time_valid = getLocalTimeSnapshot(now_ms, local_tm, local_epoch);
  if (time_valid && isInSleepWindow(local_tm, wifi_manager_.settings())) {
    return deadlineFromEpoch(now_ms, local_epoch,
                             nextSleepEndEpoch(local_tm, local_epoch, wifi_manager_.settings()));
  }

  if (state_ == AppState::Photo) {
    deadline_ms = saturatingAddMs(last_photo_switch_ms_, photo_interval_ms_);
  } else {
    uint32_t check_interval_ms = wifi_manager_.settings().calendar_refresh_sec * 1000UL;
    if (check_interval_ms < 60000UL) {
      check_interval_ms = 60000UL;
    }
    deadline_ms =
        (last_calendar_check_ms_ == 0u) ? saturatingAddMs(now_ms, check_interval_ms)
                                        : saturatingAddMs(last_calendar_check_ms_, check_interval_ms);

  }

  if (wifi_manager_.settings().app_auto_switch_enabled) {
    uint32_t switch_interval_ms = wifi_manager_.settings().app_switch_interval_sec * 1000UL;
    if (switch_interval_ms < 60000UL) {
      switch_interval_ms = 60000UL;
    }
    const uint32_t switch_deadline = saturatingAddMs(last_app_switch_ms_, switch_interval_ms);
    if (switch_deadline < deadline_ms) {
      deadline_ms = switch_deadline;
    }
  }

  if (time_valid) {
    struct tm next_midnight_tm = local_tm;
    next_midnight_tm.tm_sec = 0;
    next_midnight_tm.tm_min = 0;
    next_midnight_tm.tm_hour = 0;
    next_midnight_tm.tm_mday += 1;
    const time_t next_midnight_epoch = mktime(&next_midnight_tm);
    if (next_midnight_epoch > local_epoch) {
      const uint32_t midnight_deadline = deadlineFromEpoch(now_ms, local_epoch, next_midnight_epoch);
      if (midnight_deadline < deadline_ms) {
        deadline_ms = midnight_deadline;
      }
    }

    const time_t sleep_start = nextSleepStartEpoch(local_tm, local_epoch, wifi_manager_.settings());
    if (sleep_start > local_epoch) {
      const uint32_t sleep_start_deadline = deadlineFromEpoch(now_ms, local_epoch, sleep_start);
      if (sleep_start_deadline < deadline_ms) {
        deadline_ms = sleep_start_deadline;
      }
    }
  }

  return deadline_ms;
}

void App::onWakeFromLightSleep(uint64_t slept_us, bool woke_from_gpio, bool wake_up_pressed,
                               bool wake_mid_pressed, bool wake_down_pressed) {
  const uint32_t now_ms = millis();
  led_manager_.setTraceMuted(true);
  led_manager_.setSleeping(false, "wake");
  led_manager_.update(mode_manager_.mode(), now_ms, wifi_manager_.isStaConnected());
  led_manager_.setTraceMuted(false);
  if (woke_from_gpio) {
    sleep_inhibit_until_ms_ = saturatingAddMs(now_ms, kLightSleepWakeInhibitMs);
    input_.recoverWakePress(wake_up_pressed, wake_mid_pressed, wake_down_pressed, now_ms);
  }
  pending_wake_log_ = !appfw::kSleepQuietLogs;
  pending_wake_gpio_ = woke_from_gpio;
  pending_wake_up_pressed_ = wake_up_pressed;
  pending_wake_mid_pressed_ = wake_mid_pressed;
  pending_wake_down_pressed_ = wake_down_pressed;
  pending_wake_ms_ = now_ms;
  pending_wake_slept_ms_ = static_cast<uint32_t>(slept_us / 1000ULL);
  pending_wake_led_ = led_manager_.currentStateName();
}

void App::onEnterLightSleep(uint32_t deadline_ms) {
  const uint32_t now_ms = millis();
  input_.prepareForSleep(now_ms);
  led_manager_.setSleeping(true, "light_sleep_enter");
  if (!appfw::kSleepQuietLogs) {
    Serial.printf("[SLEEP] enter deadline_in=%lums\n",
                  static_cast<unsigned long>(deadline_ms - now_ms));
  }
  finishOperationTrace("ok", now_ms);
  Serial.flush();
  delay(200);
}

void App::cancelLightSleepEntry(const char *reason) {
  led_manager_.setSleeping(false, reason ? reason : "sleep_cancel");
  led_manager_.update(mode_manager_.mode(), millis(), wifi_manager_.isStaConnected());
  if (!appfw::kSleepQuietLogs) {
    Serial.printf("[SLEEP] cancel reason=%s led=%s\n",
                  reason ? reason : "sleep_cancel",
                  led_manager_.currentStateName());
  }
  finishOperationTrace("degraded", millis(), reason ? reason : "sleep_cancel");
}

void App::updatePhotoCarousel(uint32_t now_ms) {
  struct tm local_tm {};
  time_t local_epoch = 0;
  if (getLocalTimeSnapshot(now_ms, local_tm, local_epoch) &&
      isInSleepWindow(local_tm, wifi_manager_.settings())) {
    return;
  }
  if ((now_ms - last_photo_switch_ms_) < photo_interval_ms_) {
    return;
  }
  startOperationTrace("auto", "PhotoInterval", now_ms);
  Serial.printf("[AUTO] trigger=photo_interval interval=%lus\n",
                static_cast<unsigned long>(photo_interval_ms_ / 1000UL));
  nextPhoto("auto", now_ms);
}

void App::nextPhoto(const char *reason, uint32_t now_ms) {
  refreshPhotoFileCount();
  const uint16_t total = (photo_file_count_ > 0) ? photo_file_count_ : kPhotoCount;
  if (total == 0) {
    return;
  }
  photo_index_ = static_cast<uint16_t>((photo_index_ + 1) % total);
  last_photo_switch_ms_ = now_ms;
  needs_render_ = true;
  Serial.printf("[PHOTO] next -> index=%u/%u reason=%s source=%s\n", photo_index_ + 1, total,
                reason, (photo_file_count_ > 0) ? "epd4" : "clear");
}

void App::prevPhoto(const char *reason, uint32_t now_ms) {
  refreshPhotoFileCount();
  const uint16_t total = (photo_file_count_ > 0) ? photo_file_count_ : kPhotoCount;
  if (total == 0) {
    return;
  }
  if (photo_index_ == 0) {
    photo_index_ = static_cast<uint16_t>(total - 1);
  } else {
    photo_index_ = static_cast<uint16_t>(photo_index_ - 1);
  }
  last_photo_switch_ms_ = now_ms;
  needs_render_ = true;
  Serial.printf("[PHOTO] prev -> index=%u/%u reason=%s source=%s\n", photo_index_ + 1, total,
                reason, (photo_file_count_ > 0) ? "epd4" : "clear");
}

bool App::ensureCalendarSyncBeforeFullRefresh(uint32_t now_ms) {
  (void)now_ms;
  if (state_ != AppState::Calendar || !force_calendar_full_refresh_) {
    return true;
  }
  if (calendar_skip_presync_once_) {
    calendar_skip_presync_once_ = false;
    calendar_pre_refresh_sync_waiting_ = false;
    calendar_pre_refresh_sync_started_session_ = false;
    calendar_pre_refresh_led_active_ = false;
    calendar_pre_refresh_wifi_connected_ = false;
    calendar_pre_refresh_failed_ = false;
    Serial.println("[CAL] pre-refresh sync skipped: fast calendar render");
    return true;
  }
  if (!wifi_manager_.hasStaCredentials()) {
    calendar_pre_refresh_sync_waiting_ = false;
    calendar_pre_refresh_sync_started_session_ = false;
    calendar_start_background_sync_after_render_ = false;
    calendar_background_sync_active_ = false;
    calendar_background_sync_started_session_ = false;
    calendar_stop_sta_after_render_ = false;
    calendar_pre_refresh_led_active_ = false;
    calendar_pre_refresh_wifi_connected_ = false;
    calendar_pre_refresh_failed_ = false;
    static bool logged_missing_credentials = false;
    if (!logged_missing_credentials) {
      logged_missing_credentials = true;
      Serial.println("[CAL] pre-refresh sync skipped: missing STA credentials");
    }
    return true;
  }

  if (!calendar_pre_refresh_sync_waiting_) {
    calendar_pre_refresh_sync_waiting_ = true;
    calendar_pre_refresh_wifi_connected_ = wifi_manager_.isStaConnected();
    calendar_pre_refresh_failed_ = false;
    if (!calendar_pre_refresh_led_active_) {
      led_manager_.startBreath("calendar_render");
      calendar_pre_refresh_led_active_ = true;
    }
    if (wifi_manager_.isStaConnected()) {
      wifi_manager_.requestCalendarSyncNow();
      calendar_pre_refresh_sync_started_session_ = false;
      Serial.println("[CAL] pre-refresh sync requested on active STA");
    } else {
      wifi_manager_.startStaPreRefreshSync();
      calendar_pre_refresh_sync_started_session_ = true;
      Serial.println("[CAL] pre-refresh sync: starting STA before full refresh");
    }
    return false;
  }

  if (wifi_manager_.isStaConnected()) {
    calendar_pre_refresh_wifi_connected_ = true;
  }

  if (wifi_manager_.isStaConnecting() || wifi_manager_.isCalendarSyncBusy()) {
    led_manager_.update(mode_manager_.mode(), millis(), wifi_manager_.isStaConnected());
    return false;
  }

  if (wifi_manager_.isStaConnected()) {
    calendar_pre_refresh_wifi_connected_ = true;
  }
  calendar_stop_sta_after_render_ = calendar_pre_refresh_sync_started_session_;
  calendar_pre_refresh_sync_waiting_ = false;
  calendar_pre_refresh_sync_started_session_ = false;
  calendar_pre_refresh_led_active_ = false;
  Serial.printf("[CAL] pre-refresh sync %s -> proceed render wifi_seen=%s\n",
                calendar_pre_refresh_failed_ ? "failed" : "settled",
                calendar_pre_refresh_wifi_connected_ ? "true" : "false");
  calendar_pre_refresh_failed_ = false;
  return true;
}

void App::beginDisplaySession() {
  setPeripheralPower(true);
  EPD_init_fast();
}

void App::endDisplaySession() {
  EPD_sleep();
  delay(2);
  setPeripheralPower(false);
}

void App::setState(AppState next) {
  if (next == state_) {
    return;
  }
  if (calendar_pre_refresh_sync_started_session_ || calendar_stop_sta_after_render_ ||
      (calendar_background_sync_active_ && calendar_background_sync_started_session_)) {
    wifi_manager_.stop("state_change_cancel_pre_refresh_sync");
  }
  calendar_pre_refresh_sync_waiting_ = false;
  calendar_pre_refresh_sync_started_session_ = false;
  calendar_skip_presync_once_ = false;
  calendar_start_background_sync_after_render_ = false;
  calendar_background_sync_active_ = false;
  calendar_background_sync_started_session_ = false;
  calendar_stop_sta_after_render_ = false;
  calendar_pre_refresh_led_active_ = false;
  calendar_pre_refresh_wifi_connected_ = false;
  calendar_pre_refresh_failed_ = false;
  state_ = next;
  last_app_switch_ms_ = millis();
  if (state_ == AppState::Calendar) {
    force_calendar_full_refresh_ = true;
    calendar_skip_presync_once_ = false;
    calendar_start_background_sync_after_render_ = false;
    last_calendar_day_key_ = -1;
    last_calendar_render_minute_key_ = -1;
    last_calendar_check_ms_ = 0;
  }
  sleep_inhibit_until_ms_ = saturatingAddMs(millis(), kLightSleepWakeInhibitMs);
  needs_render_ = true;
}

void App::renderPhotoPage() {
  refreshPhotoFileCount();
  if (photo_file_count_ > 0) {
    const uint16_t safe_index = static_cast<uint16_t>(photo_index_ % photo_file_count_);
    if (renderEpd4PhotoAtIndex(safe_index)) {
      return;
    }
    Serial.println("[PHOTO] epd4 render failed, fallback clear");
  }

  Serial.println("[PHOTO] no epd4 file available, fallback clear");
  PIC_display_Clear();
}

void App::initPhotoStorage() {
  const bool auto_power_cycle = !peripheral_power_on_;
  if (auto_power_cycle) {
    setPeripheralPower(true);
  }
  ensurePhotoStorageMounted();
  if (auto_power_cycle) {
    setPeripheralPower(false);
  }
}

bool App::ensurePhotoStorageMounted() {
  if (!appfw::mountSdCard("photo")) {
    Serial.println("[PHOTO] SD not_ready");
    return false;
  }
  if (!SD.exists("/pic")) {
    SD.mkdir("/pic");
  }
  return true;
}

bool App::isEpd4Name(const String &name) const {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".epd4");
}

void App::refreshPhotoFileCount() {
  const bool auto_power_cycle = !peripheral_power_on_;
  if (auto_power_cycle) {
    setPeripheralPower(true);
  }
  if (!ensurePhotoStorageMounted()) {
    photo_file_count_ = 0;
    if (auto_power_cycle) {
      setPeripheralPower(false);
    }
    return;
  }
  File dir = SD.open("/pic");
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    photo_file_count_ = 0;
    if (auto_power_cycle) {
      setPeripheralPower(false);
    }
    return;
  }
  uint16_t count = 0;
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory() && isEpd4Name(String(entry.name())) && entry.size() == kEpd4Bytes) {
      ++count;
    }
    entry = dir.openNextFile();
  }
  dir.close();
  photo_file_count_ = count;
  if (photo_file_count_ != last_logged_photo_file_count_) {
    if (appfw::kDebugLogs) {
      Serial.printf("[PHOTO] epd4 scan count=%u\n", photo_file_count_);
    }
    last_logged_photo_file_count_ = photo_file_count_;
  }
  if (auto_power_cycle) {
    setPeripheralPower(false);
  }
}

bool App::renderEpd4PhotoAtIndex(uint16_t index) {
  const bool auto_power_cycle = !peripheral_power_on_;
  if (auto_power_cycle) {
    setPeripheralPower(true);
  }
  if (!ensurePhotoStorageMounted()) {
    Serial.println("[PHOTO] epd4 render skip: sd_not_ready");
    if (auto_power_cycle) {
      setPeripheralPower(false);
    }
    return false;
  }
  File dir = SD.open("/pic");
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    Serial.println("[PHOTO] epd4 render skip: /pic unavailable");
    if (auto_power_cycle) {
      setPeripheralPower(false);
    }
    return false;
  }

  File selected;
  uint16_t current = 0;
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory() && isEpd4Name(String(entry.name())) && entry.size() == kEpd4Bytes) {
      if (current == index) {
        selected = entry;
        break;
      }
      ++current;
    }
    entry = dir.openNextFile();
  }
  if (!selected) {
    dir.close();
    Serial.printf("[PHOTO] epd4 render skip: index=%u not found\n", index + 1);
    if (auto_power_cycle) {
      setPeripheralPower(false);
    }
    return false;
  }

  if (appfw::kDebugLogs) {
    Serial.printf("[PHOTO] render epd4 file=%s index=%u\n", selected.name(), index + 1);
  }
  uint8_t buf[256];
  EPD_W21_WriteCMD(0x10);
  size_t total = 0;
  while (selected.available()) {
    const size_t n = selected.read(buf, sizeof(buf));
    if (n == 0) {
      break;
    }
    total += n;
    for (size_t i = 0; i < n; ++i) {
      EPD_W21_WriteDATA(buf[i]);
    }
    if ((total & 0x1FFFu) == 0) {
      led_manager_.update(mode_manager_.mode(), millis());
    }
  }

  if (total != kEpd4Bytes) {
    selected.close();
    dir.close();
    Serial.printf("[PHOTO] epd4 size mismatch got=%u expected=%u\n",
                  static_cast<unsigned>(total), static_cast<unsigned>(kEpd4Bytes));
    if (auto_power_cycle) {
      setPeripheralPower(false);
    }
    return false;
  }

  selected.close();
  dir.close();
  EPD_W21_WriteCMD(0x12);
  EPD_W21_WriteDATA(0x00);
  delay(1);
  waitEpdReadyWithLed();
  if (auto_power_cycle) {
    setPeripheralPower(false);
  }
  return true;
}

void App::clearCalendarFrame(uint8_t color_nibble) {
  if (calendar_frame_ == nullptr) {
    return;
  }
  const uint8_t packed =
      static_cast<uint8_t>(((color_nibble & 0x0Fu) << 4) | (color_nibble & 0x0Fu));
  memset(calendar_frame_, packed, kCalendarFrameBytes);
}

bool App::calendarUsesPortraitRotation() const {
  return calendar_layout_ == CalendarLayout::PortraitSplit;
}

uint16_t App::calendarLogicalWidth() const {
  return calendarUsesPortraitRotation() ? kScreenHeight : kScreenWidth;
}

uint16_t App::calendarLogicalHeight() const {
  return calendarUsesPortraitRotation() ? kScreenWidth : kScreenHeight;
}

bool App::calendarLogicalToPhysical(uint16_t x, uint16_t y, uint16_t &physical_x,
                                    uint16_t &physical_y) const {
  if (calendarUsesPortraitRotation()) {
    if (x >= kScreenHeight || y >= kScreenWidth) {
      return false;
    }
    physical_x = y;
    physical_y = static_cast<uint16_t>(kScreenHeight - 1u - x);
    return true;
  }
  if (x >= kScreenWidth || y >= kScreenHeight) {
    return false;
  }
  physical_x = x;
  physical_y = y;
  return true;
}

calendar::Rect App::calendarLogicalRectToPhysical(const calendar::Rect &rect) const {
  if (rect.w == 0 || rect.h == 0) {
    return calendar::makeRect(0, 0, 0, 0);
  }
  if (calendarUsesPortraitRotation()) {
    const uint16_t physical_x = rect.y;
    const uint16_t physical_y =
        static_cast<uint16_t>(kScreenHeight - static_cast<uint16_t>(rect.x + rect.w));
    return calendar::makeRect(physical_x, physical_y, rect.h, rect.w);
  }
  return rect;
}

void App::setCalendarPixel(uint16_t x, uint16_t y, uint8_t color_nibble) {
  if (calendar_frame_ == nullptr) {
    return;
  }
  uint16_t physical_x = 0;
  uint16_t physical_y = 0;
  if (!calendarLogicalToPhysical(x, y, physical_x, physical_y)) {
    return;
  }
  const uint32_t pixel_index = static_cast<uint32_t>(physical_y) * kScreenWidth + physical_x;
  const uint32_t byte_index = pixel_index >> 1;
  const uint8_t nib = static_cast<uint8_t>(color_nibble & 0x0Fu);
  if ((pixel_index & 0x01u) == 0u) {
    calendar_frame_[byte_index] = static_cast<uint8_t>((calendar_frame_[byte_index] & 0x0Fu) |
                                                        (nib << 4));
  } else {
    calendar_frame_[byte_index] = static_cast<uint8_t>((calendar_frame_[byte_index] & 0xF0u) | nib);
  }
}

void App::fillCalendarRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color_nibble) {
  const uint16_t logical_width = calendarLogicalWidth();
  const uint16_t logical_height = calendarLogicalHeight();
  if (w == 0 || h == 0 || x >= logical_width || y >= logical_height) {
    return;
  }
  uint16_t x_end = static_cast<uint16_t>(x + w);
  uint16_t y_end = static_cast<uint16_t>(y + h);
  if (x_end > logical_width || x_end < x) {
    x_end = logical_width;
  }
  if (y_end > logical_height || y_end < y) {
    y_end = logical_height;
  }
  for (uint16_t yy = y; yy < y_end; ++yy) {
    for (uint16_t xx = x; xx < x_end; ++xx) {
      setCalendarPixel(xx, yy, color_nibble);
    }
  }
}

void App::drawCalendarRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color_nibble) {
  if (w < 2 || h < 2) {
    return;
  }
  fillCalendarRect(x, y, w, 1, color_nibble);
  fillCalendarRect(x, static_cast<uint16_t>(y + h - 1), w, 1, color_nibble);
  fillCalendarRect(x, y, 1, h, color_nibble);
  fillCalendarRect(static_cast<uint16_t>(x + w - 1), y, 1, h, color_nibble);
}

void App::drawCalendarText3x5(uint16_t x, uint16_t y, const String &text, uint8_t pixel_height,
                              uint8_t color_nibble, calendar::TextFont font,
                              calendar::TextAAMode aa_mode) {
  if (pixel_height == 0 || text.length() == 0) {
    return;
  }
  if (aa_mode != calendar::TextAAMode::Threshold) {
    calendar::TextCoverageMap map;
    if (!calendar::buildTextCoverageMap(text, pixel_height, font, map)) {
      return;
    }

    const int width = static_cast<int>(map.width);
    std::vector<int16_t> err0(width + 6, 0);
    std::vector<int16_t> err1(width + 6, 0);
    std::vector<int16_t> err2(width + 6, 0);
    for (uint16_t row = 0; row < map.height; ++row) {
      for (uint16_t col = 0; col < map.width; ++col) {
        const uint32_t idx = static_cast<uint32_t>(row) * map.width + col;
        int value = static_cast<int>(map.alpha[idx]) + err0[col + 2];
        if (value < 0) value = 0;
        if (value > 255) value = 255;
        const bool on = (value >= 128);
        if (on) {
          fillCalendarRect(static_cast<uint16_t>(x + col), static_cast<uint16_t>(y + row), 1, 1,
                           color_nibble);
        }
        const int error = value - (on ? 255 : 0);
        err0[col + 3] += static_cast<int16_t>((error * 8) / 32);
        err0[col + 4] += static_cast<int16_t>((error * 4) / 32);
        err1[col + 0] += static_cast<int16_t>((error * 2) / 32);
        err1[col + 1] += static_cast<int16_t>((error * 4) / 32);
        err1[col + 2] += static_cast<int16_t>((error * 8) / 32);
        err1[col + 3] += static_cast<int16_t>((error * 4) / 32);
        err1[col + 4] += static_cast<int16_t>((error * 2) / 32);
        err2[col + 1] += static_cast<int16_t>((error * 1) / 32);
        err2[col + 2] += static_cast<int16_t>((error * 2) / 32);
        err2[col + 3] += static_cast<int16_t>((error * 4) / 32);
        err2[col + 4] += static_cast<int16_t>((error * 2) / 32);
        err2[col + 5] += static_cast<int16_t>((error * 1) / 32);
      }
      std::fill(err0.begin(), err0.end(), 0);
      err0.swap(err1);
      err1.swap(err2);
    }

    calendar::freeTextCoverageMap(map);
    return;
  }
  const calendar::TextStyle style = calendar::resolveTextStyle(pixel_height, font);
  if (style.pixel_height == 0 || style.base_height == 0) {
    return;
  }
  const uint8_t coverage_threshold =
      (style.font == calendar::TextFont::AsciiSmooth) ? static_cast<uint8_t>(6u)
                                                      : static_cast<uint8_t>(8u);
  uint16_t pen_x = x;
  size_t byte_index = 0;
  calendar::GlyphBitmap glyph;
  while (calendar::nextTextGlyph(text, byte_index, glyph, style.font)) {
    if (glyph.rows == nullptr || glyph.width == 0 || glyph.height == 0) {
      continue;
    }
    const uint16_t draw_w = calendar::glyphWidthPx(glyph, style);
    const uint16_t draw_h = calendar::glyphHeightPx(glyph, style);
    const uint8_t src_top = (glyph.bits_per_pixel > 1u) ? style.box_top : 0u;
    const uint8_t src_left = (glyph.bits_per_pixel > 1u) ? style.box_left : 0u;
    const uint8_t src_h = (glyph.bits_per_pixel > 1u && style.box_height > 0u) ? style.box_height
                                                                                 : glyph.height;
    const uint8_t src_w = (glyph.bits_per_pixel > 1u && style.box_width > 0u) ? style.box_width
                                                                               : glyph.width;
    for (uint16_t dy = 0; dy < draw_h; ++dy) {
      const uint8_t src_row =
          static_cast<uint8_t>(src_top + ((static_cast<uint32_t>(dy) * src_h) / draw_h));
      for (uint16_t dx = 0; dx < draw_w; ++dx) {
        const uint8_t src_col =
            static_cast<uint8_t>(src_left + ((static_cast<uint32_t>(dx) * src_w) / draw_w));
        const uint8_t coverage = calendar::glyphCoverage(glyph, src_row, src_col);
        if (coverage == 0u) {
          continue;
        }
        if (glyph.bits_per_pixel > 1u && coverage < coverage_threshold) {
          continue;
        }
        fillCalendarRect(static_cast<uint16_t>(pen_x + dx), static_cast<uint16_t>(y + dy), 1, 1,
                         color_nibble);
      }
    }
    pen_x = static_cast<uint16_t>(pen_x + draw_w + calendar::glyphLetterSpacingPx(glyph, style));
  }
}

void App::drawCalendarNumberInCell(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int day_number,
                                    uint8_t scale, uint8_t color_nibble) {
  if (w < 6 || h < 6) {
    return;
  }
  const String label = String(day_number);
  const uint8_t pixel_height = static_cast<uint8_t>(7 * scale);
  const uint16_t text_w = calendar::textWidthPx(label, pixel_height);
  const uint16_t text_h = calendar::textHeightPx(label, pixel_height);
  const uint16_t text_x = static_cast<uint16_t>(x + ((w > text_w) ? ((w - text_w) / 2u) : 0u));
  const uint16_t text_y = static_cast<uint16_t>(y + ((h > text_h) ? ((h - text_h) / 2u) : 0u));
  drawCalendarText3x5(text_x, text_y, label, pixel_height, color_nibble);
}

void App::drawCalendarScene(const struct tm &local_tm, bool time_valid) {
  clearCalendarFrame(white);
  rebuildCalendarSceneCache(local_tm, time_valid);
  CalendarFrameSink sink(*this);
  calendar::emitCalendarScene(calendar_model_cache_, calendar_layout_cache_, sink);
}

void App::rebuildCalendarSceneCache(const struct tm &local_tm, bool time_valid) {
  const calendar::LayoutMode layout_mode =
      (calendar_layout_ == CalendarLayout::LandscapeSplit)
          ? calendar::LayoutMode::LandscapeSplit
          : calendar::LayoutMode::PortraitSplit;
  const bool force_header_wifi_connected =
      force_calendar_full_refresh_ && calendar_pre_refresh_wifi_connected_;
  calendar::buildCalendarModel(calendar_model_cache_, local_tm, time_valid, layout_mode,
                               wifi_manager_.settings().ui_language, wifi_manager_,
                               force_header_wifi_connected);
  if (kDebugForcedCalendarRows >= 4 && kDebugForcedCalendarRows <= 6) {
    calendar_model_cache_.month_row_count = kDebugForcedCalendarRows;
  }
  calendar::buildCalendarLayout(calendar_layout_cache_, layout_mode, kScreenWidth, kScreenHeight,
                                calendar_model_cache_.month_row_count);
}

void App::pushCalendarFullRefresh() {
  if (calendar_frame_ == nullptr) {
    PIC_display_Clear();
    return;
  }
  EPD_W21_WriteCMD(0x10);
  const uint16_t row_bytes = static_cast<uint16_t>(kScreenWidth / 2u);
  for (uint16_t y = 0; y < kScreenHeight; ++y) {
    const uint8_t *row = calendar_frame_ + static_cast<uint32_t>(y) * row_bytes;
    for (uint16_t i = 0; i < row_bytes; ++i) {
      EPD_W21_WriteDATA(row[i]);
    }
    if ((y & 0x0Fu) == 0u) {
      led_manager_.update(mode_manager_.mode(), millis(), wifi_manager_.isStaConnected());
    }
  }
  EPD_W21_WriteCMD(0x12);
  EPD_W21_WriteDATA(0x00);
  delay(1);
  waitEpdReadyWithLed();
}

void App::pushCalendarFullRefreshStriped(const calendar::CalendarModel &model,
                                         const calendar::CalendarLayout &layout) {
  if (!ensureCalendarStripeBuffer()) {
    PIC_display_Clear();
    return;
  }

  EPD_W21_WriteCMD(0x10);
  for (uint16_t stripe_y = 0; stripe_y < kScreenHeight; stripe_y += kCalendarStripeRows) {
    const uint16_t stripe_rows =
        static_cast<uint16_t>((stripe_y + kCalendarStripeRows <= kScreenHeight)
                                  ? kCalendarStripeRows
                                  : (kScreenHeight - stripe_y));
    if (!render::rasterizeCalendarSceneStripe(model, layout, stripe_y, stripe_rows,
                                              calendar_stripe_)) {
      Serial.printf("[CAL] stripe rasterize failed y=%u rows=%u\n", stripe_y, stripe_rows);
      PIC_display_Clear();
      return;
    }

    const uint16_t row_bytes = calendar_stripe_.rowBytes();
    const uint8_t *data = calendar_stripe_.data();
    for (uint16_t row = 0; row < stripe_rows; ++row) {
      const uint8_t *src = data + static_cast<uint32_t>(row) * row_bytes;
      for (uint16_t i = 0; i < row_bytes; ++i) {
        EPD_W21_WriteDATA(src[i]);
      }
    }
    led_manager_.update(mode_manager_.mode(), millis(), wifi_manager_.isStaConnected());
  }
  EPD_W21_WriteCMD(0x12);
  EPD_W21_WriteDATA(0x00);
  delay(1);
  waitEpdReadyWithLed();
}

void App::renderCalendarPage(uint32_t now_ms) {
  struct tm local_tm {};
  time_t local_epoch = 0;
  const bool time_valid = getLocalTimeSnapshot(now_ms, local_tm, local_epoch);
  const int32_t minute_key = time_valid ? appfw::minuteKeyFromTm(local_tm) : -1;
  if (time_valid) {
    last_calendar_day_key_ = appfw::dayKeyFromTm(local_tm);
  }
  rebuildCalendarSceneCache(local_tm, time_valid);
  pushCalendarFullRefreshStriped(calendar_model_cache_, calendar_layout_cache_);
  force_calendar_full_refresh_ = false;
  calendar_pre_refresh_wifi_connected_ = false;
  last_calendar_check_ms_ = millis();
  Serial.printf("[CAL] full refresh layout=%s\n",
                (calendar_layout_ == CalendarLayout::LandscapeSplit) ? "landscape_split"
                                                                      : "portrait_split");
  last_calendar_render_minute_key_ = minute_key;
}

void App::renderWhiteScreen() {
  Serial.println("[CONFIG] white screen action begin");
  led_manager_.startBreath("white_screen");
  beginDisplaySession();
  EPD_W21_WriteCMD(0x10);
  for (uint16_t y = 0; y < 480; ++y) {
    if ((y & 0x0FU) == 0) {
      led_manager_.update(mode_manager_.mode(), millis());
    }
    const uint8_t packed = static_cast<uint8_t>((white << 4) | white);
    for (uint16_t x_pair = 0; x_pair < 400; ++x_pair) {
      EPD_W21_WriteDATA(packed);
    }
  }
  EPD_W21_WriteCMD(0x12);
  EPD_W21_WriteDATA(0x00);
  delay(1);
  waitEpdReadyWithLed();
  endDisplaySession();
  led_manager_.stopEffects("white_screen_done");
  led_manager_.update(mode_manager_.mode(), millis(), wifi_manager_.isStaConnected());
  Serial.println("[CONFIG] white screen action done");
}

void App::waitEpdReadyWithLed() {
  const uint32_t start_ms = millis();
  while (!isEPD_W21_BUSY) {
    led_manager_.update(mode_manager_.mode(), millis());
    delay(2);
  }
  led_manager_.update(mode_manager_.mode(), millis());
  const uint32_t elapsed_ms = millis() - start_ms;
  if (appfw::kDebugLogs && elapsed_ms >= 200) {
    Serial.printf("[EPD] busy wait=%lums (led updated)\n", static_cast<unsigned long>(elapsed_ms));
  }
}
