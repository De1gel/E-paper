#include "app/App.h"

#include <FS.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <vector>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Display_EPD_W21.h"
#include "Display_EPD_W21_spi.h"
#include "calendar/CalendarLayout.h"
#include "calendar/CalendarModel.h"
#include "calendar/CalendarScene.h"
#include "calendar/CalendarText.h"
#include "display/PartialRefresh.h"
#include "render/SceneRasterizer.h"

namespace {
constexpr uint8_t kKeyUpPin = 0;
constexpr uint8_t kKeyMidPin = 35;
constexpr uint8_t kKeyDownPin = 34;
constexpr uint8_t kIndicatorLedPin = 2;
constexpr uint8_t kPowerCtrlPin = 32;
constexpr uint8_t kSdCsPin = 5;
constexpr uint8_t kSdSckPin = 18;
constexpr uint8_t kSdMisoPin = 19;
constexpr uint8_t kSdMosiPin = 23;
constexpr size_t kEpd4Bytes = (800 * 480) / 2;
constexpr uint16_t kPhotoCount = 1;
constexpr uint16_t kScreenWidth = 800;
constexpr uint16_t kScreenHeight = 480;
constexpr uint32_t kClockMinValidEpoch = 1700000000UL;
constexpr uint32_t kCalendarCheckIntervalMs = 60000UL;
constexpr uint8_t kCalendarPartialBeforeFull = 7;
constexpr AppState kDebugBootState = AppState::Calendar;
constexpr uint8_t kDebugForcedCalendarRows = 0;
constexpr bool kDebugCalendarHeaderPartialPattern = false;
constexpr uint16_t kPartialAlignPx = 4u;

String twoDigits(int value) {
  if (value < 10) {
    return "0" + String(value);
  }
  return String(value);
}

int32_t dayKeyFromTm(const struct tm &t) {
  return static_cast<int32_t>((t.tm_year + 1900) * 1000 + t.tm_yday);
}

int32_t minuteKeyFromTm(const struct tm &t) {
  return static_cast<int32_t>(((t.tm_year + 1900) * 400 + t.tm_yday) * 1440 +
                              t.tm_hour * 60 + t.tm_min);
}

int32_t refreshBucketKey(int32_t minute_key, uint32_t interval_sec) {
  if (minute_key < 0 || interval_sec == 0u) {
    return -1;
  }
  const uint32_t interval_min = interval_sec / 60u;
  if (interval_min == 0u) {
    return -1;
  }
  return static_cast<int32_t>(minute_key / static_cast<int32_t>(interval_min));
}

bool sameCalendarMinute(const struct tm &a, const struct tm &b) {
  return a.tm_year == b.tm_year &&
         a.tm_yday == b.tm_yday &&
         a.tm_hour == b.tm_hour &&
         a.tm_min == b.tm_min;
}

time_t fallbackClockBaseEpoch() {
  struct tm fallback_tm {};
  fallback_tm.tm_year = 2026 - 1900;
  fallback_tm.tm_mon = 0;
  fallback_tm.tm_mday = 1;
  fallback_tm.tm_hour = 12;
  fallback_tm.tm_min = 0;
  fallback_tm.tm_sec = 0;
  fallback_tm.tm_isdst = -1;
  return mktime(&fallback_tm);
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

uint32_t largest8BitHeap() {
  return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

void setPackedBufferPixel(uint8_t *buffer, uint16_t buffer_width_px, uint16_t buffer_rows,
                          uint16_t x, uint16_t y, uint8_t color_nibble) {
  if (buffer == nullptr || x >= buffer_width_px || y >= buffer_rows) {
    return;
  }
  const uint32_t pixel_index = static_cast<uint32_t>(y) * buffer_width_px + x;
  const uint32_t byte_index = pixel_index >> 1;
  const uint8_t nib = static_cast<uint8_t>(color_nibble & 0x0Fu);
  if ((pixel_index & 0x01u) == 0u) {
    buffer[byte_index] = static_cast<uint8_t>((buffer[byte_index] & 0x0Fu) | (nib << 4));
  } else {
    buffer[byte_index] = static_cast<uint8_t>((buffer[byte_index] & 0xF0u) | nib);
  }
}

void fillPackedBufferRect(uint8_t *buffer, uint16_t buffer_width_px, uint16_t buffer_rows,
                          uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color_nibble) {
  if (buffer == nullptr || w == 0 || h == 0 || x >= buffer_width_px || y >= buffer_rows) {
    return;
  }
  const uint16_t x_end =
      static_cast<uint16_t>(((x + w) > buffer_width_px) ? buffer_width_px : (x + w));
  const uint16_t y_end =
      static_cast<uint16_t>(((y + h) > buffer_rows) ? buffer_rows : (y + h));
  for (uint16_t yy = y; yy < y_end; ++yy) {
    for (uint16_t xx = x; xx < x_end; ++xx) {
      setPackedBufferPixel(buffer, buffer_width_px, buffer_rows, xx, yy, color_nibble);
    }
  }
}

void drawPackedBufferText(uint8_t *buffer, uint16_t buffer_width_px, uint16_t buffer_rows,
                          uint16_t x, uint16_t y, const String &text, uint8_t pixel_height,
                          uint8_t color_nibble, calendar::TextFont font,
                          calendar::TextAAMode aa_mode) {
  if (buffer == nullptr || pixel_height == 0 || text.length() == 0) {
    return;
  }
  if (aa_mode != calendar::TextAAMode::Threshold) {
    calendar::TextCoverageMap map;
    if (!calendar::buildTextCoverageMap(text, pixel_height, font, map)) {
      return;
    }
    for (uint16_t row = 0; row < map.height; ++row) {
      for (uint16_t col = 0; col < map.width; ++col) {
        const uint32_t idx = static_cast<uint32_t>(row) * map.width + col;
        if (map.alpha[idx] >= 128u) {
          fillPackedBufferRect(buffer, buffer_width_px, buffer_rows,
                               static_cast<uint16_t>(x + col), static_cast<uint16_t>(y + row),
                               1, 1, color_nibble);
        }
      }
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
        fillPackedBufferRect(buffer, buffer_width_px, buffer_rows,
                             static_cast<uint16_t>(pen_x + dx), static_cast<uint16_t>(y + dy),
                             1, 1, color_nibble);
      }
    }
    pen_x = static_cast<uint16_t>(pen_x + draw_w + calendar::glyphLetterSpacingPx(glyph, style));
  }
}

void drawPackedBufferLine(uint8_t *buffer, uint16_t buffer_width_px, uint16_t buffer_rows,
                          uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                          uint8_t color_nibble) {
  int x = static_cast<int>(x0);
  int y = static_cast<int>(y0);
  const int tx = static_cast<int>(x1);
  const int ty = static_cast<int>(y1);
  const int dx = abs(tx - x);
  const int sx = (x < tx) ? 1 : -1;
  const int dy = -abs(ty - y);
  const int sy = (y < ty) ? 1 : -1;
  int err = dx + dy;
  while (true) {
    fillPackedBufferRect(buffer, buffer_width_px, buffer_rows, static_cast<uint16_t>(x),
                         static_cast<uint16_t>(y), 1, 1, color_nibble);
    if (x == tx && y == ty) {
      break;
    }
    const int e2 = err * 2;
    if (e2 >= dy) {
      err += dy;
      x += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y += sy;
    }
  }
}

calendar::Rect alignRectToPartialGrid(const calendar::Rect &rect,
                                      const calendar::Rect &bounds) {
  if (rect.w == 0 || rect.h == 0 || bounds.w == 0 || bounds.h == 0) {
    return calendar::makeRect(0, 0, 0, 0);
  }
  const uint16_t bounds_x1 = static_cast<uint16_t>(bounds.x + bounds.w);
  const uint16_t bounds_y1 = static_cast<uint16_t>(bounds.y + bounds.h);
  uint16_t x0 = rect.x;
  uint16_t y0 = rect.y;
  uint16_t x1 = static_cast<uint16_t>(rect.x + rect.w);
  uint16_t y1 = static_cast<uint16_t>(rect.y + rect.h);
  if (x0 < bounds.x) x0 = bounds.x;
  if (y0 < bounds.y) y0 = bounds.y;
  if (x1 > bounds_x1) x1 = bounds_x1;
  if (y1 > bounds_y1) y1 = bounds_y1;
  x0 = static_cast<uint16_t>((x0 / kPartialAlignPx) * kPartialAlignPx);
  y0 = static_cast<uint16_t>((y0 / kPartialAlignPx) * kPartialAlignPx);
  x1 = static_cast<uint16_t>(((x1 + kPartialAlignPx - 1u) / kPartialAlignPx) * kPartialAlignPx);
  y1 = static_cast<uint16_t>(((y1 + kPartialAlignPx - 1u) / kPartialAlignPx) * kPartialAlignPx);
  if (x1 > bounds_x1) x1 = bounds_x1;
  if (y1 > bounds_y1) y1 = bounds_y1;
  if (x1 <= x0 || y1 <= y0) {
    return calendar::makeRect(0, 0, 0, 0);
  }
  return calendar::makeRect(x0, y0, static_cast<uint16_t>(x1 - x0),
                            static_cast<uint16_t>(y1 - y0));
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

void App::begin() {
  state_ = kDebugBootState;
  photo_index_ = 0;
  last_photo_switch_ms_ = millis();
  // Always redraw the default page after boot/reset so the panel state matches app state.
  needs_render_ = true;
  calendar_layout_ = CalendarLayout::LandscapeSplit;
  force_calendar_full_refresh_ = true;
  calendar_partial_refresh_count_ = 0;
  last_calendar_check_ms_ = 0;
  last_calendar_day_key_ = -1;
  last_calendar_render_minute_key_ = -1;
  clock_valid_ = false;
  clock_anchor_epoch_ = 0;
  clock_anchor_ms_ = 0;
  ensureCalendarFrameBuffer("boot");
  clearCalendarFrame(white);

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

  Serial.println("[APP] begin");
  Serial.printf("[INPUT] key pins up=%u mid=%u down=%u\n", kKeyUpPin, kKeyMidPin,
                kKeyDownPin);
  Serial.printf("[POWER] epd rail pin=%u default=OFF\n", kPowerCtrlPin);
  Serial.printf("[PHOTO] interval=%lus\n", static_cast<unsigned long>(photo_interval_ms_ / 1000UL));
  Serial.printf("[PHOTO] /pic epd4 count=%u\n", photo_file_count_);
  Serial.println("[APP] boot render queued");
}

void App::logCalendarHeap(const char *tag) const {
  Serial.printf("[CAL] heap %s free=%lu largest=%lu\n", tag ? tag : "-",
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(largest8BitHeap()));
}

bool App::ensureCalendarFrameBuffer(const char *reason) {
  if (calendar_frame_ != nullptr) {
    return true;
  }

  const char *why = reason ? reason : "unknown";
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
  if (!enabled && photo_sd_ready_) {
    SD.end();
    photo_sd_ready_ = false;
    Serial.println("[PHOTO] SD unmounted (power off)");
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
      display_changed = !sameCalendarMinute(est_tm, sys_tm);
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
    const time_t fallback_base = fallbackClockBaseEpoch();
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

  struct tm local_tm {};
  time_t local_epoch = 0;
  if (!getLocalTimeSnapshot(now_ms, local_tm, local_epoch)) {
    return;
  }
  const int32_t key = dayKeyFromTm(local_tm);
  const int32_t minute_key = minuteKeyFromTm(local_tm);
  const uint32_t time_refresh_sec = wifi_manager_.settings().calendar_time_refresh_sec;
  const bool time_slot_changed =
      (time_refresh_sec != 0u) &&
      (refreshBucketKey(minute_key, time_refresh_sec) !=
       refreshBucketKey(last_calendar_render_minute_key_, time_refresh_sec));
  if (key != last_calendar_day_key_) {
    last_calendar_day_key_ = key;
    force_calendar_full_refresh_ = true;
    needs_render_ = true;
    Serial.printf("[CAL] day changed key=%ld -> render\n", static_cast<long>(key));
    return;
  }
  if (minute_key != last_calendar_render_minute_key_ && time_slot_changed) {
    needs_render_ = true;
    Serial.printf("[CAL] time slot changed key=%ld interval=%lus -> header refresh\n",
                  static_cast<long>(minute_key),
                  static_cast<unsigned long>(time_refresh_sec));
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
  Serial.printf("[CAL] periodic refresh epoch=%lu\n", static_cast<unsigned long>(local_epoch));
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
    calendar_partial_refresh_count_ = 0;
    if (state_ == AppState::Calendar) {
      needs_render_ = true;
    }
    Serial.printf("[CAL] layout config -> %s\n",
                  (calendar_layout_ == CalendarLayout::LandscapeSplit) ? "landscape_split"
                                                                        : "portrait_split");
  }
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
  updateClockAnchor(now_ms);
  applyCalendarLayoutFromConfig(false);
  if (wifi_manager_.consumeStaConnectFailed()) {
    led_manager_.triggerDoubleBlink();
  }
  const uint32_t latest_interval_ms = wifi_manager_.settings().photo_interval_sec * 1000UL;
  if (latest_interval_ms >= 30000UL && latest_interval_ms != photo_interval_ms_) {
    photo_interval_ms_ = latest_interval_ms;
    Serial.printf("[PHOTO] interval updated=%lus\n",
                  static_cast<unsigned long>(photo_interval_ms_ / 1000UL));
  }

  if (wifi_manager_.consumeAutoExitRequested()) {
    mode_manager_.forceNormal(now_ms, "wifi_session_timeout");
  }

  if (mode_manager_.consumeApRequest()) {
    wifi_manager_.startAP();
  }
  if (mode_manager_.consumeStaRequest()) {
    wifi_manager_.startSTA();
  }
  if (mode_manager_.consumeStopWifiRequest()) {
    wifi_manager_.stop("manual_key_exit_config");
  }
  if (mode_manager_.consumeWhiteScreenRequest()) {
    renderWhiteScreen();
  }

  if (mode_manager_.mode() == appfw::OperationMode::Normal) {
    if (state_ == AppState::Photo) {
      updatePhotoCarousel(now_ms);
    } else if (state_ == AppState::Calendar) {
      updateCalendarAutoRefresh(now_ms);
    }
  }

  led_manager_.update(mode_manager_.mode(), now_ms, wifi_manager_.isStaConnected());
}

void App::handleInputEvent(appfw::InputEvent event, uint32_t now_ms) {
  Serial.printf("[INPUT] event=%s\n", eventName(event));
  mode_manager_.onInputEvent(event, now_ms);

  if (mode_manager_.mode() == appfw::OperationMode::Normal) {
    if (event == appfw::InputEvent::MidShort) {
      led_manager_.triggerDoubleBlink();
      const AppState next_state =
          (state_ == AppState::Photo) ? AppState::Calendar : AppState::Photo;
      setState(next_state);
      Serial.printf("[APP] page switch -> %s\n",
                    next_state == AppState::Photo ? "Photo" : "Calendar");
    } else if (state_ == AppState::Photo && event == appfw::InputEvent::UpShort) {
      prevPhoto("key_up", now_ms);
    } else if (state_ == AppState::Photo && event == appfw::InputEvent::DownShort) {
      nextPhoto("key_down", now_ms);
    }
  }
}

void App::render() {
  if (!needs_render_) {
    return;
  }
  const uint32_t now_ms = millis();
  const bool partial_refresh =
      (state_ == AppState::Calendar) && willUseCalendarPartialRefresh(now_ms);

  Serial.println("[APP] render begin");
  beginDisplaySession(partial_refresh);

  if (state_ == AppState::Photo) {
    renderPhotoPage();
  } else if (state_ == AppState::Calendar) {
    renderCalendarPage(now_ms);
  } else {
    renderCalendarPage(now_ms);
  }

  endDisplaySession();
  Serial.println("[APP] render done, epd sleep");
  needs_render_ = false;
  if (state_ == AppState::Photo) {
    led_manager_.stopEffects();
    led_manager_.update(mode_manager_.mode(), millis(), wifi_manager_.isStaConnected());
  }
}

void App::updatePhotoCarousel(uint32_t now_ms) {
  if ((now_ms - last_photo_switch_ms_) < photo_interval_ms_) {
    return;
  }
  nextPhoto("auto", now_ms);
}

void App::nextPhoto(const char *reason, uint32_t now_ms) {
  refreshPhotoFileCount();
  const uint16_t total = (photo_file_count_ > 0) ? photo_file_count_ : kPhotoCount;
  if (total == 0) {
    return;
  }
  led_manager_.startBreath();
  led_manager_.update(mode_manager_.mode(), millis(), wifi_manager_.isStaConnected());
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
  led_manager_.startBreath();
  led_manager_.update(mode_manager_.mode(), millis(), wifi_manager_.isStaConnected());
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

bool App::willUseCalendarPartialRefresh(uint32_t now_ms) const {
  if (state_ != AppState::Calendar) {
    return false;
  }

  struct tm local_tm {};
  time_t local_epoch = 0;
  const bool time_valid = getLocalTimeSnapshot(now_ms, local_tm, local_epoch);
  (void)local_epoch;
  const int32_t minute_key = time_valid ? minuteKeyFromTm(local_tm) : -1;
  const uint32_t time_refresh_sec = wifi_manager_.settings().calendar_time_refresh_sec;
  const bool time_only_refresh =
      time_valid &&
      !force_calendar_full_refresh_ &&
      (minute_key != -1) &&
      (minute_key != last_calendar_render_minute_key_) &&
      (time_refresh_sec != 0u) &&
      (refreshBucketKey(minute_key, time_refresh_sec) !=
       refreshBucketKey(last_calendar_render_minute_key_, time_refresh_sec));
  bool use_full = force_calendar_full_refresh_;
  if (!use_full && calendar_partial_refresh_count_ >= kCalendarPartialBeforeFull) {
    use_full = true;
  }
  return !use_full || time_only_refresh;
}

void App::beginDisplaySession(bool partial_refresh) {
  (void)partial_refresh;
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
  state_ = next;
  if (state_ == AppState::Calendar) {
    force_calendar_full_refresh_ = true;
    last_calendar_day_key_ = -1;
    last_calendar_render_minute_key_ = -1;
    last_calendar_check_ms_ = 0;
    ensureCalendarFrameBuffer("switch_to_calendar");
  }
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
  if (!photo_sd_spi_started_) {
    photo_sd_spi_.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
    photo_sd_spi_started_ = true;
  }
  if (!photo_sd_ready_) {
    photo_sd_ready_ = SD.begin(kSdCsPin, photo_sd_spi_);
    if (photo_sd_ready_ && !SD.exists("/pic")) {
      SD.mkdir("/pic");
    }
    Serial.printf("[PHOTO] SD %s\n", photo_sd_ready_ ? "ready" : "not_ready");
  }
  return photo_sd_ready_;
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
    photo_sd_ready_ = false;
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
    Serial.printf("[PHOTO] epd4 scan count=%u\n", photo_file_count_);
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
    photo_sd_ready_ = false;
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

  Serial.printf("[PHOTO] render epd4 file=%s index=%u\n", selected.name(), index + 1);
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

void App::setCalendarPixel(uint16_t x, uint16_t y, uint8_t color_nibble) {
  if (calendar_frame_ == nullptr) {
    return;
  }
  if (x >= kScreenWidth || y >= kScreenHeight) {
    return;
  }
  const uint32_t pixel_index = static_cast<uint32_t>(y) * kScreenWidth + x;
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
  if (w == 0 || h == 0 || x >= kScreenWidth || y >= kScreenHeight) {
    return;
  }
  uint16_t x_end = static_cast<uint16_t>(x + w);
  uint16_t y_end = static_cast<uint16_t>(y + h);
  if (x_end > kScreenWidth || x_end < x) {
    x_end = kScreenWidth;
  }
  if (y_end > kScreenHeight || y_end < y) {
    y_end = kScreenHeight;
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

calendar::Rect App::calendarHeaderTimeRect(const calendar::CalendarModel &model,
                                           const calendar::CalendarLayout &layout) const {
  constexpr uint8_t kHeaderPx = 24;
  const String kTimeWindowSample = "88:88";
  const uint16_t header_left_x = static_cast<uint16_t>(layout.header_bar.x + 6);
  const uint16_t header_date_y = static_cast<uint16_t>(layout.header_y + 6);
  const uint16_t header_time_y = static_cast<uint16_t>(header_date_y +
                                                        calendar::textHeightPx(
                                                            model.header_date, kHeaderPx,
                                                            calendar::TextFont::AsciiSmooth) +
                                                        2);
  const uint16_t time_w = calendar::textWidthPx(kTimeWindowSample, kHeaderPx,
                                                calendar::TextFont::AsciiSmooth);
  const uint16_t time_h = calendar::textHeightPx(kTimeWindowSample, kHeaderPx,
                                                 calendar::TextFont::AsciiSmooth);
  constexpr uint16_t kPadX = 6u;
  constexpr uint16_t kPadTop = 4u;
  constexpr uint16_t kPadBottom = 6u;
  const uint16_t rect_x = (header_left_x > kPadX)
                              ? static_cast<uint16_t>(header_left_x - kPadX)
                              : layout.header_bar.x;
  const uint16_t rect_y = (header_time_y > kPadTop)
                              ? static_cast<uint16_t>(header_time_y - kPadTop)
                              : layout.header_bar.y;
  const calendar::Rect raw = calendar::makeRect(
      rect_x, rect_y, static_cast<uint16_t>(time_w + kPadX * 2u),
      static_cast<uint16_t>(time_h + kPadTop + kPadBottom));
  return alignRectToPartialGrid(raw, layout.header_bar);
}

void App::redrawCalendarHeaderTime(const calendar::CalendarModel &model,
                                   const calendar::CalendarLayout &layout) {
  constexpr uint8_t kHeaderPx = 24;
  calendar::Rect rect = calendarHeaderTimeRect(model, layout);
  if (rect.w == 0 || rect.h == 0) {
    return;
  }
  if ((rect.x & 0x01u) != 0u) {
    --rect.x;
    ++rect.w;
  }
  if ((rect.w & 0x01u) != 0u) {
    ++rect.w;
  }
  if ((rect.x + rect.w) > kScreenWidth) {
    rect.w = static_cast<uint16_t>(kScreenWidth - rect.x);
    rect.w &= static_cast<uint16_t>(~0x01u);
  }
  if (rect.w < 2u) {
    return;
  }

  const calendar::TextFont date_font = calendar::TextFont::AsciiSmooth;
  const uint16_t header_left_x = static_cast<uint16_t>(layout.header_bar.x + 6);
  const uint16_t header_date_y = static_cast<uint16_t>(layout.header_y + 6);
  const uint16_t header_time_y = static_cast<uint16_t>(
      header_date_y + calendar::textHeightPx(model.header_date, kHeaderPx, date_font) + 2);
  if (!calendar_window_buffer_.ensure(rect.w, rect.h)) {
    return;
  }
  calendar_window_buffer_.clear(blue);
  if (kDebugCalendarHeaderPartialPattern) {
    const uint16_t rw = calendar_window_buffer_.widthPx();
    const uint16_t rh = calendar_window_buffer_.rows();
    const uint16_t right = (rw > 0) ? static_cast<uint16_t>(rw - 1u) : 0u;
    const uint16_t bottom = (rh > 0) ? static_cast<uint16_t>(rh - 1u) : 0u;
    fillPackedBufferRect(calendar_window_buffer_.data(), rw, rh, 0, 0, rw, 1, white);
    fillPackedBufferRect(calendar_window_buffer_.data(), rw, rh, 0, bottom, rw, 1, white);
    fillPackedBufferRect(calendar_window_buffer_.data(), rw, rh, 0, 0, 1, rh, white);
    fillPackedBufferRect(calendar_window_buffer_.data(), rw, rh, right, 0, 1, rh, white);

    const uint16_t mid_x = rw / 2u;
    const uint16_t mid_y = rh / 2u;
    fillPackedBufferRect(calendar_window_buffer_.data(), rw, rh, mid_x, 0, 1, rh, yellow);
    fillPackedBufferRect(calendar_window_buffer_.data(), rw, rh, 0, mid_y, rw, 1, green);
    drawPackedBufferLine(calendar_window_buffer_.data(), rw, rh, 0, 0, right, bottom, red);
    drawPackedBufferLine(calendar_window_buffer_.data(), rw, rh, 0, bottom, right, 0, black);

    const uint16_t block_x = (rw > 56u) ? static_cast<uint16_t>(rw - 50u) : 6u;
    const uint16_t block_y = 6u;
    const uint16_t cell = 8u;
    for (uint8_t row = 0; row < 3; ++row) {
      for (uint8_t col = 0; col < 3; ++col) {
        const uint8_t color = ((row + col) & 0x01u) ? white : black;
        fillPackedBufferRect(calendar_window_buffer_.data(), rw, rh,
                             static_cast<uint16_t>(block_x + col * cell),
                             static_cast<uint16_t>(block_y + row * cell), cell, cell, color);
      }
    }
    drawPackedBufferText(calendar_window_buffer_.data(), rw, rh, 8u,
                         static_cast<uint16_t>((rh > 28u) ? (rh - 24u) : 4u),
                         "88:88", 24, white, calendar::TextFont::AsciiSmooth,
                         calendar::TextAAMode::Threshold);

    if (calendar_frame_ != nullptr) {
      fillCalendarRect(rect.x, rect.y, rect.w, rect.h, blue);
      fillCalendarRect(rect.x, rect.y, rect.w, 1, white);
      fillCalendarRect(rect.x, static_cast<uint16_t>(rect.y + rect.h - 1u), rect.w, 1, white);
      fillCalendarRect(rect.x, rect.y, 1, rect.h, white);
      fillCalendarRect(static_cast<uint16_t>(rect.x + rect.w - 1u), rect.y, 1, rect.h, white);
      fillCalendarRect(static_cast<uint16_t>(rect.x + mid_x), rect.y, 1, rect.h, yellow);
      fillCalendarRect(rect.x, static_cast<uint16_t>(rect.y + mid_y), rect.w, 1, green);
      for (uint16_t i = 0; i < rect.w && i < rect.h; ++i) {
        fillCalendarRect(static_cast<uint16_t>(rect.x + i), static_cast<uint16_t>(rect.y + i),
                         1, 1, red);
        fillCalendarRect(static_cast<uint16_t>(rect.x + i),
                         static_cast<uint16_t>(rect.y + rect.h - 1u - i), 1, 1, black);
      }
      for (uint8_t row = 0; row < 3; ++row) {
        for (uint8_t col = 0; col < 3; ++col) {
          const uint8_t color = ((row + col) & 0x01u) ? white : black;
          fillCalendarRect(static_cast<uint16_t>(rect.x + block_x + col * cell),
                           static_cast<uint16_t>(rect.y + block_y + row * cell), cell, cell,
                           color);
        }
      }
      drawCalendarText3x5(static_cast<uint16_t>(rect.x + 8u),
                          static_cast<uint16_t>(rect.y + ((rh > 28u) ? (rh - 24u) : 4u)),
                          "88:88", 24, white, calendar::TextFont::AsciiSmooth,
                          calendar::TextAAMode::Threshold);
    }
  } else {
    drawPackedBufferText(calendar_window_buffer_.data(), calendar_window_buffer_.widthPx(),
                         calendar_window_buffer_.rows(),
                         static_cast<uint16_t>(header_left_x - rect.x),
                         static_cast<uint16_t>(header_time_y - rect.y),
                         model.header_time, kHeaderPx, white, calendar::TextFont::AsciiSmooth,
                         calendar::TextAAMode::Threshold);
    if (calendar_frame_ != nullptr) {
      fillCalendarRect(rect.x, rect.y, rect.w, rect.h, blue);
      drawCalendarText3x5(header_left_x, header_time_y, model.header_time, kHeaderPx, white,
                          calendar::TextFont::AsciiSmooth, calendar::TextAAMode::Threshold);
    }
  }
  partial_refresh::writeWindowFromBuffer(calendar_window_buffer_.data(),
                                         calendar_window_buffer_.widthPx(),
                                         rect.x, rect.y, rect.w, rect.h);
}

void App::rebuildCalendarSceneCache(const struct tm &local_tm, bool time_valid) {
  const calendar::LayoutMode layout_mode =
      (calendar_layout_ == CalendarLayout::LandscapeSplit)
          ? calendar::LayoutMode::LandscapeSplit
          : calendar::LayoutMode::PortraitSplit;
  calendar::buildCalendarModel(calendar_model_cache_, local_tm, time_valid, layout_mode,
                               wifi_manager_.settings().ui_language, wifi_manager_);
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

void App::pushCalendarPartialRefresh(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  if (calendar_frame_ == nullptr) {
    return;
  }
  partial_refresh::writeWindowFromPacked(calendar_frame_, kScreenWidth, x, y, w, h);
}

void App::renderCalendarPage(uint32_t now_ms) {
  struct tm local_tm {};
  time_t local_epoch = 0;
  const bool time_valid = getLocalTimeSnapshot(now_ms, local_tm, local_epoch);
  const int32_t minute_key = time_valid ? minuteKeyFromTm(local_tm) : -1;
  const uint32_t time_refresh_sec = wifi_manager_.settings().calendar_time_refresh_sec;
  if (time_valid) {
    last_calendar_day_key_ = dayKeyFromTm(local_tm);
  }
  rebuildCalendarSceneCache(local_tm, time_valid);

  const bool time_only_refresh =
      time_valid &&
      !force_calendar_full_refresh_ &&
      (minute_key != -1) &&
      (minute_key != last_calendar_render_minute_key_) &&
      (time_refresh_sec != 0u) &&
      (refreshBucketKey(minute_key, time_refresh_sec) !=
       refreshBucketKey(last_calendar_render_minute_key_, time_refresh_sec));
  bool use_full = force_calendar_full_refresh_;
  if (!use_full && calendar_partial_refresh_count_ >= kCalendarPartialBeforeFull) {
    use_full = true;
  }

  if (use_full) {
    pushCalendarFullRefreshStriped(calendar_model_cache_, calendar_layout_cache_);
    force_calendar_full_refresh_ = false;
    calendar_partial_refresh_count_ = 0;
    Serial.printf("[CAL] full refresh layout=%s\n",
                  (calendar_layout_ == CalendarLayout::LandscapeSplit) ? "landscape_split"
                                                                        : "portrait_split");
  } else if (time_only_refresh) {
    const uint32_t partial_begin_ms = millis();
    const calendar::Rect area =
        calendarHeaderTimeRect(calendar_model_cache_, calendar_layout_cache_);
    redrawCalendarHeaderTime(calendar_model_cache_, calendar_layout_cache_);
    Serial.printf("[CAL] header time partial area=(%u,%u,%u,%u) elapsed=%lums\n",
                  area.x, area.y, area.w, area.h,
                  static_cast<unsigned long>(millis() - partial_begin_ms));
  } else {
    if (ensureCalendarFrameBuffer("partial_refresh")) {
      drawCalendarScene(local_tm, time_valid);
      const calendar::Rect area = calendar::makeRect(0, 0, kScreenWidth, kScreenHeight);
      pushCalendarPartialRefresh(area.x, area.y, area.w, area.h);
      ++calendar_partial_refresh_count_;
      Serial.printf("[CAL] partial refresh count=%u area=(%u,%u,%u,%u)\n",
                    calendar_partial_refresh_count_, area.x, area.y, area.w, area.h);
    } else {
      pushCalendarFullRefreshStriped(calendar_model_cache_, calendar_layout_cache_);
      force_calendar_full_refresh_ = false;
      calendar_partial_refresh_count_ = 0;
      Serial.println("[CAL] partial path unavailable -> full striped refresh");
    }
  }
  last_calendar_render_minute_key_ = minute_key;
}

void App::renderWhiteScreen() {
  Serial.println("[CONFIG] white screen action begin");
  led_manager_.triggerBreath(2);
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
  if (elapsed_ms >= 200) {
    Serial.printf("[EPD] busy wait=%lums (led updated)\n", static_cast<unsigned long>(elapsed_ms));
  }
}
