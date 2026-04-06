#include "app/App.h"

#include <FS.h>
#include <SD.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Display_EPD_W21.h"
#include "Display_EPD_W21_spi.h"
#include "display/PartialRefresh.h"

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

struct Rect {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t w = 0;
  uint16_t h = 0;
};

Rect makeRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  Rect r;
  r.x = x;
  r.y = y;
  r.w = w;
  r.h = h;
  return r;
}

bool isLeapYear(int year) {
  return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
  static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  if (month < 1 || month > 12) {
    return 30;
  }
  return kDays[month - 1];
}

String twoDigits(int value) {
  if (value < 10) {
    return "0" + String(value);
  }
  return String(value);
}

int32_t dayKeyFromTm(const struct tm &t) {
  return static_cast<int32_t>((t.tm_year + 1900) * 1000 + t.tm_yday);
}

String normalizeUiLanguage(const String &raw) {
  String lang = raw;
  lang.trim();
  lang.toLowerCase();
  if (lang == "en" || lang == "fr") {
    return lang;
  }
  return "zh";
}

String dailyQuoteByLanguage(int32_t day_key, const String &lang_raw) {
  static const char *kQuoteZh[] = {
      "RI GONG YI SHAN",          "SHUI DI SHI CHUAN",      "NING JING ZHI YUAN",
      "XIN JING ZE MING",         "XUE ER BU YAN",          "REN ZAI LU SHANG",
      "YUAN JING ZAI QIAN",       "JIAN CHI JIU SHI LIANG", "DANG XIA JI SHI LIANG",
      "JIN RI SHENG YU ZUO RI",   "MEI RI YI BU",           "YAN GUANG YAO YUAN",
  };
  static const char *kQuoteEn[] = {
      "SMALL STEPS EVERY DAY",      "CONSISTENCY BEATS SPEED",
      "FOCUS CREATES CLARITY",      "COURAGE OPENS NEW PATHS",
      "LEARN FAST STAY HUMBLE",     "DO THE NEXT RIGHT THING",
      "PRACTICE BUILDS CONFIDENCE", "CALM MIND STRONG ACTION",
      "TODAY IS A FRESH PAGE",      "PROGRESS OVER PERFECTION",
      "START NOW ITERATE OFTEN",    "PATIENCE MAKES RESULTS",
  };
  static const char *kQuoteFr[] = {
      "PETITS PAS CHAQUE JOUR",     "LA CONSTANCE GAGNE",
      "ESPRIT CALME ACTION FORTE",  "LE COURAGE OUVRE DES PORTES",
      "APPRENDRE ET AVANCER",       "UN JOUR A LA FOIS",
      "LE PROGRES AVANT TOUT",      "RESTER SIMPLE RESTER VRAI",
      "CHAQUE JOUR COMPTE",         "SEMER AUJOURD HUI RECOLTER DEMAIN",
      "LA PATIENCE PORTE SES FRUITS", "FAIRE MIEUX PAS PARFAIT",
  };

  const String lang = normalizeUiLanguage(lang_raw);
  const int idx_seed = (day_key >= 0) ? day_key : -day_key;
  if (lang == "en") {
    const int idx = idx_seed % (sizeof(kQuoteEn) / sizeof(kQuoteEn[0]));
    return String(kQuoteEn[idx]);
  }
  if (lang == "fr") {
    const int idx = idx_seed % (sizeof(kQuoteFr) / sizeof(kQuoteFr[0]));
    return String(kQuoteFr[idx]);
  }
  const int idx = idx_seed % (sizeof(kQuoteZh) / sizeof(kQuoteZh[0]));
  return String(kQuoteZh[idx]);
}

String quoteTagByLanguage(const String &lang_raw) {
  const String lang = normalizeUiLanguage(lang_raw);
  if (lang == "en") return "QUOTE";
  if (lang == "fr") return "CITATION";
  return "GE YAN";
}

const uint8_t *glyph3x5(char c) {
  static const uint8_t kSpace[5] = {0x0, 0x0, 0x0, 0x0, 0x0};
  static const uint8_t kDash[5] = {0x0, 0x0, 0x7, 0x0, 0x0};
  static const uint8_t kSlash[5] = {0x1, 0x1, 0x2, 0x4, 0x4};
  static const uint8_t kColon[5] = {0x0, 0x2, 0x0, 0x2, 0x0};
  static const uint8_t kDot[5] = {0x0, 0x0, 0x0, 0x0, 0x2};
  static const uint8_t kUnknown[5] = {0x7, 0x1, 0x2, 0x0, 0x2};
  static const uint8_t kDigits[10][5] = {
      {0x7, 0x5, 0x5, 0x5, 0x7},  // 0
      {0x2, 0x6, 0x2, 0x2, 0x7},  // 1
      {0x7, 0x1, 0x7, 0x4, 0x7},  // 2
      {0x7, 0x1, 0x7, 0x1, 0x7},  // 3
      {0x5, 0x5, 0x7, 0x1, 0x1},  // 4
      {0x7, 0x4, 0x7, 0x1, 0x7},  // 5
      {0x7, 0x4, 0x7, 0x5, 0x7},  // 6
      {0x7, 0x1, 0x1, 0x1, 0x1},  // 7
      {0x7, 0x5, 0x7, 0x5, 0x7},  // 8
      {0x7, 0x5, 0x7, 0x1, 0x7},  // 9
  };
  static const uint8_t kUpper[26][5] = {
      {0x2, 0x5, 0x7, 0x5, 0x5},  // A
      {0x6, 0x5, 0x6, 0x5, 0x6},  // B
      {0x3, 0x4, 0x4, 0x4, 0x3},  // C
      {0x6, 0x5, 0x5, 0x5, 0x6},  // D
      {0x7, 0x4, 0x6, 0x4, 0x7},  // E
      {0x7, 0x4, 0x6, 0x4, 0x4},  // F
      {0x3, 0x4, 0x5, 0x5, 0x3},  // G
      {0x5, 0x5, 0x7, 0x5, 0x5},  // H
      {0x7, 0x2, 0x2, 0x2, 0x7},  // I
      {0x1, 0x1, 0x1, 0x5, 0x2},  // J
      {0x5, 0x5, 0x6, 0x5, 0x5},  // K
      {0x4, 0x4, 0x4, 0x4, 0x7},  // L
      {0x5, 0x7, 0x7, 0x5, 0x5},  // M
      {0x5, 0x7, 0x7, 0x7, 0x5},  // N
      {0x2, 0x5, 0x5, 0x5, 0x2},  // O
      {0x6, 0x5, 0x6, 0x4, 0x4},  // P
      {0x2, 0x5, 0x5, 0x3, 0x1},  // Q
      {0x6, 0x5, 0x6, 0x5, 0x5},  // R
      {0x3, 0x4, 0x2, 0x1, 0x6},  // S
      {0x7, 0x2, 0x2, 0x2, 0x2},  // T
      {0x5, 0x5, 0x5, 0x5, 0x7},  // U
      {0x5, 0x5, 0x5, 0x5, 0x2},  // V
      {0x5, 0x5, 0x7, 0x7, 0x5},  // W
      {0x5, 0x5, 0x2, 0x5, 0x5},  // X
      {0x5, 0x5, 0x2, 0x2, 0x2},  // Y
      {0x7, 0x1, 0x2, 0x4, 0x7},  // Z
  };
  if (c >= '0' && c <= '9') {
    return kDigits[c - '0'];
  }
  if (c >= 'a' && c <= 'z') {
    return kUpper[c - 'a'];
  }
  if (c >= 'A' && c <= 'Z') {
    return kUpper[c - 'A'];
  }
  if (c == '-') {
    return kDash;
  }
  if (c == '/') {
    return kSlash;
  }
  if (c == ':') {
    return kColon;
  }
  if (c == '.') {
    return kDot;
  }
  if (c == ' ') {
    return kSpace;
  }
  return kUnknown;
}

uint8_t calendarColorToNibble(const String &raw) {
  String color = raw;
  color.toLowerCase();
  if (color == "black") return black;
  if (color == "white") return white;
  if (color == "yellow") return yellow;
  if (color == "red") return red;
  if (color == "blue") return blue;
  if (color == "green") return green;
  return blue;
}

String formatDateYmd(const struct tm &local_tm) {
  return String(local_tm.tm_year + 1900) + "-" + twoDigits(local_tm.tm_mon + 1) + "-" +
         twoDigits(local_tm.tm_mday);
}

bool calendarEventMatchesToday(const appfw::WifiManager::CalendarEvent &event, const String &today_ymd,
                               int today_weekday) {
  if (event.repeat == "daily") {
    return true;
  }
  if (event.repeat == "weekly") {
    return event.weekday == today_weekday;
  }
  return event.date == today_ymd;
}

uint16_t textWidth3x5(const String &text, uint8_t scale) {
  if (text.length() == 0 || scale == 0) {
    return 0;
  }
  return static_cast<uint16_t>(text.length() * (3 * scale) + (text.length() - 1) * scale);
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
}  // namespace

void App::begin() {
  state_ = AppState::Photo;
  photo_index_ = 0;
  last_photo_switch_ms_ = millis();
  // Keep the persisted e-paper image on boot; only redraw after an explicit change.
  needs_render_ = false;
  calendar_layout_ = CalendarLayout::LandscapeSplit;
  force_calendar_full_refresh_ = true;
  calendar_partial_refresh_count_ = 0;
  last_calendar_check_ms_ = 0;
  last_calendar_day_key_ = -1;
  clock_valid_ = false;
  clock_anchor_epoch_ = 0;
  clock_anchor_ms_ = 0;
  if (calendar_frame_ == nullptr) {
    calendar_frame_ = static_cast<uint8_t *>(malloc(kCalendarFrameBytes));
    if (calendar_frame_ == nullptr) {
      Serial.printf("[CAL] framebuffer alloc failed bytes=%u\n",
                    static_cast<unsigned>(kCalendarFrameBytes));
    } else {
      Serial.printf("[CAL] framebuffer alloc ok bytes=%u\n",
                    static_cast<unsigned>(kCalendarFrameBytes));
    }
  }
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
  Serial.println("[APP] boot render skipped");
}

void App::setPeripheralPower(bool enabled) {
  if (peripheral_power_on_ == enabled) {
    return;
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
    return;
  }

  const uint32_t delta_ms = now_ms - clock_anchor_ms_;
  const time_t est_now = clock_anchor_epoch_ + static_cast<time_t>(delta_ms / 1000UL);
  const long drift = static_cast<long>(sys_now - est_now);
  if (labs(drift) > 2L) {
    clock_anchor_epoch_ = sys_now;
    clock_anchor_ms_ = now_ms;
    Serial.printf("[TIME] clock anchor corrected drift=%lds epoch=%lu\n", drift,
                  static_cast<unsigned long>(sys_now));
  }
}

bool App::getLocalTimeSnapshot(uint32_t now_ms, struct tm &local_tm, time_t &local_epoch) const {
  if (!clock_valid_) {
    return false;
  }
  const uint32_t delta_ms = now_ms - clock_anchor_ms_;
  local_epoch = clock_anchor_epoch_ + static_cast<time_t>(delta_ms / 1000UL);
  return localtime_r(&local_epoch, &local_tm) != nullptr;
}

void App::updateCalendarAutoRefresh(uint32_t now_ms) {
  if (state_ != AppState::Calendar || mode_manager_.mode() != appfw::OperationMode::Normal) {
    return;
  }
  uint32_t check_interval_ms =
      wifi_manager_.settings().calendar_refresh_sec * 1000UL;
  if (check_interval_ms < 60000UL) {
    check_interval_ms = 60000UL;
  }
  if (last_calendar_check_ms_ != 0 &&
      (now_ms - last_calendar_check_ms_) < check_interval_ms) {
    return;
  }
  last_calendar_check_ms_ = now_ms;

  struct tm local_tm {};
  time_t local_epoch = 0;
  if (!getLocalTimeSnapshot(now_ms, local_tm, local_epoch)) {
    return;
  }
  const int32_t key = dayKeyFromTm(local_tm);
  if (key != last_calendar_day_key_) {
    last_calendar_day_key_ = key;
    needs_render_ = true;
    Serial.printf("[CAL] day changed key=%ld -> render\n", static_cast<long>(key));
  }
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

  Serial.println("[APP] render begin");
  beginDisplaySession();

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
  state_ = next;
  if (state_ == AppState::Calendar) {
    force_calendar_full_refresh_ = true;
    last_calendar_day_key_ = -1;
    last_calendar_check_ms_ = 0;
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
  setPeripheralPower(true);
  if (!photo_sd_spi_started_) {
    photo_sd_spi_.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
    photo_sd_spi_started_ = true;
  }
  photo_sd_ready_ = SD.begin(kSdCsPin, photo_sd_spi_);
  if (photo_sd_ready_ && !SD.exists("/pic")) {
    SD.mkdir("/pic");
  }
  Serial.printf("[PHOTO] SD %s\n", photo_sd_ready_ ? "ready" : "not_ready");
  setPeripheralPower(false);
}

bool App::isEpd4Name(const String &name) const {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".epd4");
}

void App::refreshPhotoFileCount() {
  if (!photo_sd_ready_) {
    initPhotoStorage();
    if (!photo_sd_ready_) {
      photo_file_count_ = 0;
      return;
    }
  }
  setPeripheralPower(true);
  File dir = SD.open("/pic");
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    setPeripheralPower(false);
    photo_sd_ready_ = false;
    photo_file_count_ = 0;
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
  setPeripheralPower(false);
  photo_file_count_ = count;
  if (photo_file_count_ != last_logged_photo_file_count_) {
    Serial.printf("[PHOTO] epd4 scan count=%u\n", photo_file_count_);
    last_logged_photo_file_count_ = photo_file_count_;
  }
}

bool App::renderEpd4PhotoAtIndex(uint16_t index) {
  if (!photo_sd_ready_) {
    Serial.println("[PHOTO] epd4 render skip: sd_not_ready");
    return false;
  }
  setPeripheralPower(true);
  File dir = SD.open("/pic");
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    photo_sd_ready_ = false;
    Serial.println("[PHOTO] epd4 render skip: /pic unavailable");
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
    return false;
  }

  selected.close();
  dir.close();
  EPD_W21_WriteCMD(0x12);
  EPD_W21_WriteDATA(0x00);
  delay(1);
  waitEpdReadyWithLed();
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

void App::drawCalendarText3x5(uint16_t x, uint16_t y, const String &text, uint8_t scale,
                              uint8_t color_nibble) {
  if (scale == 0 || text.length() == 0) {
    return;
  }
  uint16_t pen_x = x;
  for (size_t i = 0; i < text.length(); ++i) {
    const uint8_t *glyph = glyph3x5(text.charAt(i));
    for (uint8_t row = 0; row < 5; ++row) {
      const uint8_t bits = glyph[row];
      for (uint8_t col = 0; col < 3; ++col) {
        if ((bits & (1u << (2u - col))) == 0u) {
          continue;
        }
        fillCalendarRect(static_cast<uint16_t>(pen_x + col * scale),
                         static_cast<uint16_t>(y + row * scale), scale, scale, color_nibble);
      }
    }
    pen_x = static_cast<uint16_t>(pen_x + 3 * scale + scale);
  }
}

void App::drawCalendarNumberInCell(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int day_number,
                                   uint8_t scale, uint8_t color_nibble) {
  if (w < 6 || h < 6) {
    return;
  }
  const String label = String(day_number);
  const uint16_t text_w = textWidth3x5(label, scale);
  const uint16_t text_h = static_cast<uint16_t>(5 * scale);
  const uint16_t text_x = static_cast<uint16_t>(x + ((w > text_w) ? ((w - text_w) / 2u) : 0u));
  const uint16_t text_y = static_cast<uint16_t>(y + ((h > text_h) ? ((h - text_h) / 2u) : 0u));
  drawCalendarText3x5(text_x, text_y, label, scale, color_nibble);
}

void App::drawCalendarScene(const struct tm &local_tm, bool time_valid) {
  clearCalendarFrame(white);

  Rect calendar_panel;
  Rect schedule_panel;
  if (calendar_layout_ == CalendarLayout::LandscapeSplit) {
    calendar_panel = makeRect(0, 0, static_cast<uint16_t>(kScreenWidth / 2), kScreenHeight);
    schedule_panel =
        makeRect(static_cast<uint16_t>(kScreenWidth / 2), 0,
                 static_cast<uint16_t>(kScreenWidth / 2), kScreenHeight);
  } else {
    calendar_panel = makeRect(0, 0, kScreenWidth, static_cast<uint16_t>(kScreenHeight / 2));
    schedule_panel = makeRect(0, static_cast<uint16_t>(kScreenHeight / 2), kScreenWidth,
                              static_cast<uint16_t>(kScreenHeight / 2));
  }

  drawCalendarRect(0, 0, kScreenWidth, kScreenHeight, blue);
  drawCalendarRect(calendar_panel.x, calendar_panel.y, calendar_panel.w, calendar_panel.h, blue);
  drawCalendarRect(schedule_panel.x, schedule_panel.y, schedule_panel.w, schedule_panel.h, green);

  if (calendar_layout_ == CalendarLayout::LandscapeSplit) {
    fillCalendarRect(static_cast<uint16_t>(kScreenWidth / 2), 0, 1, kScreenHeight, blue);
  } else {
    fillCalendarRect(0, static_cast<uint16_t>(kScreenHeight / 2), kScreenWidth, 1, blue);
  }

  const uint16_t margin = (calendar_layout_ == CalendarLayout::LandscapeSplit) ? 10 : 8;
  const uint8_t title_scale = (calendar_layout_ == CalendarLayout::LandscapeSplit) ? 4 : 3;
  const uint8_t weekday_scale = (calendar_layout_ == CalendarLayout::LandscapeSplit) ? 2 : 1;
  const uint8_t day_scale = (calendar_layout_ == CalendarLayout::LandscapeSplit) ? 3 : 2;
  String title = "-- -- --";
  if (time_valid) {
    title = String(local_tm.tm_year + 1900) + "-" + twoDigits(local_tm.tm_mon + 1) + "-" +
            twoDigits(local_tm.tm_mday);
  }
  const uint16_t title_bar_x = static_cast<uint16_t>(calendar_panel.x + margin);
  const uint16_t title_bar_w = static_cast<uint16_t>(calendar_panel.w > margin * 2
                                                         ? (calendar_panel.w - margin * 2)
                                                         : calendar_panel.w);
  const uint16_t title_w = textWidth3x5(title, title_scale);
  const uint16_t title_x = static_cast<uint16_t>(
      calendar_panel.x + ((calendar_panel.w > title_w) ? ((calendar_panel.w - title_w) / 2u) : 0u));
  const uint16_t title_y = static_cast<uint16_t>(calendar_panel.y + margin);
  fillCalendarRect(title_bar_x, static_cast<uint16_t>(title_y > 3 ? title_y - 3 : title_y),
                   title_bar_w, static_cast<uint16_t>(5 * title_scale + 6), blue);
  drawCalendarText3x5(title_x, title_y, title, title_scale, white);

  const uint16_t weekday_y = static_cast<uint16_t>(title_y + 5 * title_scale + 8);
  const uint16_t weekday_h = static_cast<uint16_t>(5 * weekday_scale + 6);
  uint16_t grid_top = static_cast<uint16_t>(weekday_y + weekday_h + 4);
  const uint16_t grid_left_base = static_cast<uint16_t>(calendar_panel.x + margin);
  const uint16_t grid_right = static_cast<uint16_t>(calendar_panel.x + calendar_panel.w - margin);
  if (grid_right <= grid_left_base || grid_top >= (calendar_panel.y + calendar_panel.h)) {
    return;
  }
  uint16_t grid_w = static_cast<uint16_t>(grid_right - grid_left_base);
  uint16_t grid_h =
      static_cast<uint16_t>(calendar_panel.y + calendar_panel.h - margin > grid_top
                                ? (calendar_panel.y + calendar_panel.h - margin - grid_top)
                                : 0);
  if (grid_w < 140 || grid_h < 60) {
    return;
  }
  uint16_t cell_w = static_cast<uint16_t>(grid_w / 7u);
  uint16_t cell_h = static_cast<uint16_t>(grid_h / 6u);
  grid_w = static_cast<uint16_t>(cell_w * 7u);
  grid_h = static_cast<uint16_t>(cell_h * 6u);
  const uint16_t grid_left =
      static_cast<uint16_t>(grid_left_base + ((grid_right - grid_left_base - grid_w) / 2u));

  const char *weekday_names[7] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
  for (uint8_t col = 0; col < 7; ++col) {
    const String label = String(weekday_names[col]);
    const uint16_t label_w = textWidth3x5(label, weekday_scale);
    const uint16_t label_box_x = static_cast<uint16_t>(grid_left + col * cell_w);
    const uint16_t label_box_y = weekday_y;
    if (col >= 5 && cell_w > 2) {
      fillCalendarRect(static_cast<uint16_t>(label_box_x + 1), static_cast<uint16_t>(label_box_y + 1),
                       static_cast<uint16_t>(cell_w - 2), static_cast<uint16_t>(weekday_h - 2), yellow);
    }
    const uint16_t label_x = static_cast<uint16_t>(grid_left + col * cell_w +
                                                   ((cell_w > label_w) ? (cell_w - label_w) / 2u : 0u));
    const uint8_t label_color = (col >= 5) ? red : green;
    drawCalendarText3x5(label_x, weekday_y, label, weekday_scale, label_color);
  }

  drawCalendarRect(grid_left, grid_top, grid_w, grid_h, blue);
  for (uint8_t col = 1; col < 7; ++col) {
    fillCalendarRect(static_cast<uint16_t>(grid_left + col * cell_w), grid_top, 1, grid_h, blue);
  }
  for (uint8_t row = 1; row < 6; ++row) {
    fillCalendarRect(grid_left, static_cast<uint16_t>(grid_top + row * cell_h), grid_w, 1, blue);
  }

  if (!time_valid) {
    drawCalendarText3x5(static_cast<uint16_t>(schedule_panel.x + 20),
                        static_cast<uint16_t>(schedule_panel.y + 18), "NO TIME", 2, red);
  }

  const uint16_t schedule_margin = 10;
  const uint16_t schedule_inner_x = static_cast<uint16_t>(schedule_panel.x + schedule_margin);
  const uint16_t schedule_inner_y = static_cast<uint16_t>(schedule_panel.y + schedule_margin);
  const uint16_t schedule_inner_w =
      static_cast<uint16_t>(schedule_panel.w > schedule_margin * 2
                                ? (schedule_panel.w - schedule_margin * 2)
                                : schedule_panel.w);
  const uint16_t schedule_inner_h =
      static_cast<uint16_t>(schedule_panel.h > schedule_margin * 2
                                ? (schedule_panel.h - schedule_margin * 2)
                                : schedule_panel.h);
  drawCalendarRect(schedule_inner_x, schedule_inner_y, schedule_inner_w, schedule_inner_h, green);
  const String schedule_title = "SCHEDULE";
  drawCalendarText3x5(static_cast<uint16_t>(schedule_inner_x + 8),
                      static_cast<uint16_t>(schedule_inner_y + 8), schedule_title, 2, green);

  const int today_weekday = time_valid ? ((local_tm.tm_wday + 6) % 7) : -1;
  const String today_ymd = time_valid ? formatDateYmd(local_tm) : "";
  appfw::WifiManager::CalendarEvent visible[appfw::WifiManager::kMaxCalendarEvents];
  size_t visible_count = 0;
  for (size_t i = 0; i < wifi_manager_.calendarEventCount(); ++i) {
    appfw::WifiManager::CalendarEvent event;
    if (!wifi_manager_.calendarEventAt(i, event)) {
      continue;
    }
    if (!time_valid || calendarEventMatchesToday(event, today_ymd, today_weekday)) {
      if (visible_count < appfw::WifiManager::kMaxCalendarEvents) {
        visible[visible_count++] = event;
      }
    }
  }
  for (size_t i = 0; i + 1 < visible_count; ++i) {
    for (size_t j = i + 1; j < visible_count; ++j) {
      bool swap_needed = false;
      if (visible[j].time_hhmm < visible[i].time_hhmm) {
        swap_needed = true;
      } else if (visible[j].time_hhmm == visible[i].time_hhmm &&
                 visible[j].title < visible[i].title) {
        swap_needed = true;
      } else if (visible[j].time_hhmm == visible[i].time_hhmm &&
                 visible[j].title == visible[i].title &&
                 visible[j].id < visible[i].id) {
        swap_needed = true;
      }
      if (swap_needed) {
        appfw::WifiManager::CalendarEvent tmp = visible[i];
        visible[i] = visible[j];
        visible[j] = tmp;
      }
    }
  }

  struct TimeGroup {
    String time_hhmm;
    uint8_t indices[appfw::WifiManager::kMaxCalendarEvents];
    uint8_t count = 0;
  };
  TimeGroup groups[appfw::WifiManager::kMaxCalendarEvents];
  size_t group_count = 0;
  for (size_t i = 0; i < visible_count; ++i) {
    if (group_count == 0 || groups[group_count - 1].time_hhmm != visible[i].time_hhmm) {
      if (group_count >= appfw::WifiManager::kMaxCalendarEvents) {
        break;
      }
      groups[group_count].time_hhmm = visible[i].time_hhmm;
      groups[group_count].count = 0;
      ++group_count;
    }
    TimeGroup &g = groups[group_count - 1];
    if (g.count < appfw::WifiManager::kMaxCalendarEvents) {
      g.indices[g.count++] = static_cast<uint8_t>(i);
    }
  }

  const uint8_t list_scale = 2;
  const uint16_t list_top = static_cast<uint16_t>(schedule_inner_y + 28);
  const uint16_t quote_box_h =
      (calendar_layout_ == CalendarLayout::LandscapeSplit) ? static_cast<uint16_t>(56)
                                                            : static_cast<uint16_t>(48);
  const uint16_t quote_box_y =
      static_cast<uint16_t>(schedule_inner_y + schedule_inner_h > quote_box_h + 6
                                ? (schedule_inner_y + schedule_inner_h - quote_box_h - 6)
                                : (schedule_inner_y + schedule_inner_h));
  const uint16_t list_bottom =
      (quote_box_y > list_top + 4) ? static_cast<uint16_t>(quote_box_y - 4) : list_top;
  const uint8_t max_rows =
      (calendar_layout_ == CalendarLayout::LandscapeSplit) ? static_cast<uint8_t>(10)
                                                            : static_cast<uint8_t>(5);
  const uint16_t usable_h =
      (list_bottom > list_top) ? static_cast<uint16_t>(list_bottom - list_top) : 0;
  uint16_t row_h = (max_rows > 0) ? static_cast<uint16_t>(usable_h / max_rows) : usable_h;
  if (row_h < 18) {
    row_h = 18;
  }
  const uint16_t left_margin = 8;
  const uint16_t right_margin = 8;
  const uint16_t time_col_w =
      static_cast<uint16_t>(textWidth3x5("23:59", list_scale) + 8);
  const uint16_t content_x = static_cast<uint16_t>(schedule_inner_x + left_margin);
  const uint16_t items_x = static_cast<uint16_t>(content_x + time_col_w);
  const uint16_t items_w = static_cast<uint16_t>(
      (schedule_inner_w > (left_margin + right_margin + time_col_w))
          ? (schedule_inner_w - left_margin - right_margin - time_col_w)
          : 0);
  uint8_t lane_count = 1;
  if (items_w >= 220) {
    lane_count = 3;
  } else if (items_w >= 120) {
    lane_count = 2;
  }
  uint16_t lane_w = (lane_count > 0) ? static_cast<uint16_t>(items_w / lane_count) : items_w;
  if (lane_w < 50) {
    lane_count = 1;
    lane_w = items_w;
  }

  uint8_t rendered_rows = 0;
  for (uint8_t row = 0; row < max_rows; ++row) {
    const uint16_t y = static_cast<uint16_t>(list_top + row * row_h);
    if (y + row_h > list_bottom) {
      break;
    }
    rendered_rows = static_cast<uint8_t>(row + 1);
    fillCalendarRect(static_cast<uint16_t>(schedule_inner_x + 6),
                     static_cast<uint16_t>(y + row_h - 1),
                     static_cast<uint16_t>(schedule_inner_w - 12), 1, green);
    if (row >= group_count) {
      continue;
    }
    const TimeGroup &g = groups[row];
    drawCalendarText3x5(content_x, static_cast<uint16_t>(y + 4), g.time_hhmm, list_scale, blue);

    if (items_w == 0) {
      continue;
    }
    for (uint8_t lane = 0; lane < lane_count; ++lane) {
      const uint16_t lane_x = static_cast<uint16_t>(items_x + lane * lane_w);
      const uint16_t lane_inner_x = static_cast<uint16_t>(lane_x + 2);
      const uint16_t lane_inner_w = static_cast<uint16_t>(lane_w > 4 ? (lane_w - 4) : lane_w);
      if (lane > 0) {
        fillCalendarRect(lane_x, static_cast<uint16_t>(y + 2), 1,
                         static_cast<uint16_t>(row_h > 4 ? (row_h - 4) : row_h), green);
      }

      bool draw_more = false;
      uint8_t more_count = 0;
      if (lane == static_cast<uint8_t>(lane_count - 1) &&
          g.count > static_cast<uint8_t>(lane_count)) {
        draw_more = true;
        more_count = static_cast<uint8_t>(g.count - (lane_count - 1));
      }

      if (!draw_more && lane >= g.count) {
        continue;
      }

      String title;
      uint8_t event_color = yellow;
      if (draw_more) {
        title = "+" + String(more_count);
      } else {
        const appfw::WifiManager::CalendarEvent &event = visible[g.indices[lane]];
        title = event.title;
        event_color = calendarColorToNibble(event.color);
      }

      const uint16_t chip_h = static_cast<uint16_t>(row_h > 8 ? (row_h - 8) : 8);
      fillCalendarRect(lane_inner_x, static_cast<uint16_t>(y + 4), 8, chip_h, event_color);
      drawCalendarRect(lane_inner_x, static_cast<uint16_t>(y + 4), 8, chip_h, black);

      const uint16_t text_x = static_cast<uint16_t>(lane_inner_x + 12);
      const uint16_t text_space = static_cast<uint16_t>(
          lane_inner_w > 14 ? (lane_inner_w - 14) : 0);
      const uint16_t char_w = static_cast<uint16_t>(3 * list_scale + list_scale);
      size_t max_chars = (char_w > 0) ? static_cast<size_t>(text_space / char_w) : 0;
      if (max_chars < 1) {
        max_chars = 1;
      }
      if (title.length() > max_chars) {
        if (max_chars > 1) {
          title = title.substring(0, max_chars - 1) + "~";
        } else {
          title = "~";
        }
      }
      drawCalendarText3x5(text_x, static_cast<uint16_t>(y + 4), title, list_scale, black);
    }
  }

  if (group_count > rendered_rows) {
    const uint16_t footer_y =
        (quote_box_y > 12) ? static_cast<uint16_t>(quote_box_y - 11) : schedule_inner_y;
    const String more_text = "+" + String(group_count - rendered_rows) + " MORE";
    drawCalendarText3x5(static_cast<uint16_t>(schedule_inner_x + 8), footer_y, more_text, 1, red);
  }

  if (quote_box_y + quote_box_h <= (schedule_inner_y + schedule_inner_h)) {
    const uint16_t quote_x = static_cast<uint16_t>(schedule_inner_x + 6);
    const uint16_t quote_w =
        static_cast<uint16_t>(schedule_inner_w > 12 ? (schedule_inner_w - 12) : schedule_inner_w);
    drawCalendarRect(quote_x, quote_box_y, quote_w, quote_box_h, blue);

    const String ui_lang = normalizeUiLanguage(wifi_manager_.settings().ui_language);
    const String quote_tag = quoteTagByLanguage(ui_lang);
    const int32_t quote_key = time_valid
                                  ? dayKeyFromTm(local_tm)
                                  : static_cast<int32_t>(millis() / 86400000UL);
    String quote_text = dailyQuoteByLanguage(quote_key, ui_lang);

    drawCalendarText3x5(static_cast<uint16_t>(quote_x + 4), static_cast<uint16_t>(quote_box_y + 4),
                        quote_tag, 1, blue);

    const uint16_t quote_text_x = static_cast<uint16_t>(quote_x + 4);
    const uint16_t quote_text_y = static_cast<uint16_t>(quote_box_y + 12);
    const uint16_t quote_text_w = static_cast<uint16_t>(quote_w > 8 ? (quote_w - 8) : 0);
    const uint16_t quote_text_h = static_cast<uint16_t>(quote_box_h > 16 ? (quote_box_h - 16) : 0);
    const uint8_t quote_scale = 1;
    const uint16_t quote_char_w = static_cast<uint16_t>(3 * quote_scale + quote_scale);
    const uint16_t quote_line_h = static_cast<uint16_t>(5 * quote_scale + 1);
    const uint8_t max_quote_lines = (quote_line_h > 0)
                                        ? static_cast<uint8_t>(quote_text_h / quote_line_h)
                                        : 0;
    const size_t max_quote_chars = (quote_char_w > 0) ? (quote_text_w / quote_char_w) : 0;

    if (max_quote_lines > 0 && max_quote_chars > 0 && quote_text.length() > 0) {
      size_t pos = 0;
      for (uint8_t line = 0; line < max_quote_lines && pos < quote_text.length(); ++line) {
        while (pos < quote_text.length() && quote_text.charAt(pos) == ' ') {
          ++pos;
        }
        if (pos >= quote_text.length()) {
          break;
        }

        size_t take = quote_text.length() - pos;
        if (take > max_quote_chars) {
          take = max_quote_chars;
          const int search_end = static_cast<int>(pos + max_quote_chars - 1);
          const int space_pos = quote_text.lastIndexOf(' ', search_end);
          if (space_pos > static_cast<int>(pos)) {
            take = static_cast<size_t>(space_pos - static_cast<int>(pos));
          }
        }
        if (take == 0) {
          break;
        }

        String line_text = quote_text.substring(pos, pos + take);
        pos += take;
        while (pos < quote_text.length() && quote_text.charAt(pos) == ' ') {
          ++pos;
        }

        if (line == static_cast<uint8_t>(max_quote_lines - 1) && pos < quote_text.length()) {
          if (line_text.length() >= max_quote_chars && max_quote_chars > 1) {
            line_text = line_text.substring(0, max_quote_chars - 1);
          }
          line_text += "~";
        }

        drawCalendarText3x5(
            quote_text_x,
            static_cast<uint16_t>(quote_text_y + line * quote_line_h),
            line_text, quote_scale, black);
      }
    }
  }

  if (!time_valid) {
    return;
  }

  const int year = local_tm.tm_year + 1900;
  const int month = local_tm.tm_mon + 1;
  const int today = local_tm.tm_mday;
  struct tm first_day = local_tm;
  first_day.tm_mday = 1;
  first_day.tm_hour = 12;
  first_day.tm_min = 0;
  first_day.tm_sec = 0;
  mktime(&first_day);

  const int first_col = (first_day.tm_wday + 6) % 7;  // Monday=0
  const int cur_days = daysInMonth(year, month);
  const int prev_month = (month == 1) ? 12 : (month - 1);
  const int prev_year = (month == 1) ? (year - 1) : year;
  const int prev_days = daysInMonth(prev_year, prev_month);

  for (int index = 0; index < 42; ++index) {
    const int row = index / 7;
    const int col = index % 7;
    const uint16_t cell_x = static_cast<uint16_t>(grid_left + col * cell_w);
    const uint16_t cell_y = static_cast<uint16_t>(grid_top + row * cell_h);

    bool in_current = false;
    int day = 0;
    if (index < first_col) {
      day = prev_days - (first_col - index - 1);
    } else if (index < (first_col + cur_days)) {
      day = index - first_col + 1;
      in_current = true;
    } else {
      day = index - (first_col + cur_days) + 1;
    }

    const bool is_today = in_current && (day == today);
    if (is_today && cell_w > 2 && cell_h > 2) {
      fillCalendarRect(static_cast<uint16_t>(cell_x + 1), static_cast<uint16_t>(cell_y + 1),
                       static_cast<uint16_t>(cell_w - 2), static_cast<uint16_t>(cell_h - 2), yellow);
    }
    uint8_t text_color = in_current ? black : blue;
    if (in_current && col >= 5) {
      text_color = green;
    }
    if (is_today) {
      text_color = red;
    }
    drawCalendarNumberInCell(static_cast<uint16_t>(cell_x + 1), static_cast<uint16_t>(cell_y + 1),
                             static_cast<uint16_t>(cell_w - 2), static_cast<uint16_t>(cell_h - 2),
                             day, day_scale, text_color);
  }
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

void App::pushCalendarPartialRefresh(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  if (calendar_frame_ == nullptr) {
    return;
  }
  partial_refresh::writeWindowFromPacked(calendar_frame_, kScreenWidth, x, y, w, h);
}

void App::renderCalendarPage(uint32_t now_ms) {
  if (calendar_frame_ == nullptr) {
    Serial.println("[CAL] framebuffer unavailable, fallback clear");
    PIC_display_Clear();
    return;
  }
  struct tm local_tm {};
  time_t local_epoch = 0;
  const bool time_valid = getLocalTimeSnapshot(now_ms, local_tm, local_epoch);
  if (time_valid) {
    last_calendar_day_key_ = dayKeyFromTm(local_tm);
  }

  drawCalendarScene(local_tm, time_valid);

  bool use_full = force_calendar_full_refresh_;
  if (!use_full && calendar_partial_refresh_count_ >= kCalendarPartialBeforeFull) {
    use_full = true;
  }

  if (use_full) {
    pushCalendarFullRefresh();
    force_calendar_full_refresh_ = false;
    calendar_partial_refresh_count_ = 0;
    Serial.printf("[CAL] full refresh layout=%s\n",
                  (calendar_layout_ == CalendarLayout::LandscapeSplit) ? "landscape_split"
                                                                        : "portrait_split");
  } else {
    Rect area = makeRect(0, 0, kScreenWidth, kScreenHeight);
    pushCalendarPartialRefresh(area.x, area.y, area.w, area.h);
    ++calendar_partial_refresh_count_;
    Serial.printf("[CAL] partial refresh count=%u area=(%u,%u,%u,%u)\n",
                  calendar_partial_refresh_count_, area.x, area.y, area.w, area.h);
  }
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
