#include "app/App.h"

#include <FS.h>
#include <SD.h>

#include "Display_EPD_W21.h"
#include "Display_EPD_W21_spi.h"

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

  input_.begin(kKeyUpPin, kKeyMidPin, kKeyDownPin);
  mode_manager_.begin(last_photo_switch_ms_);
  led_manager_.begin(kIndicatorLedPin);
  wifi_manager_.begin();
  photo_interval_ms_ = wifi_manager_.settings().photo_interval_sec * 1000UL;
  if (photo_interval_ms_ < 30000UL) {
    photo_interval_ms_ = 30000UL;
  }
  pinMode(kPowerCtrlPin, OUTPUT);
  digitalWrite(kPowerCtrlPin, LOW);
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

void App::update(uint32_t now_ms) {
  input_.update(now_ms);

  appfw::InputEvent event = appfw::InputEvent::None;
  while (input_.pollEvent(event)) {
    handleInputEvent(event, now_ms);
  }

  mode_manager_.update(now_ms);
  led_manager_.update(mode_manager_.mode(), now_ms);
  wifi_manager_.update(now_ms);
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

  if (mode_manager_.mode() == appfw::OperationMode::Normal &&
      state_ == AppState::Photo) {
    updatePhotoCarousel(now_ms);
  }
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

  Serial.println("[APP] render begin");
  beginDisplaySession();

  if (state_ == AppState::Photo) {
    renderPhotoPage();
  } else if (state_ == AppState::Calendar) {
    renderCalendarPage();
  } else {
    renderCalendarPage();
  }

  endDisplaySession();
  Serial.println("[APP] render done, epd sleep");
  needs_render_ = false;
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
  led_manager_.triggerBreath(2);
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
  led_manager_.triggerBreath(2);
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
  digitalWrite(kPowerCtrlPin, HIGH);
  delay(3);
  EPD_init_fast();
}

void App::endDisplaySession() {
  EPD_sleep();
  delay(2);
  digitalWrite(kPowerCtrlPin, LOW);
}

void App::setState(AppState next) {
  if (next == state_) {
    return;
  }
  state_ = next;
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
  if (!photo_sd_spi_started_) {
    photo_sd_spi_.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
    photo_sd_spi_started_ = true;
  }
  photo_sd_ready_ = SD.begin(kSdCsPin, photo_sd_spi_);
  if (photo_sd_ready_ && !SD.exists("/pic")) {
    SD.mkdir("/pic");
  }
  Serial.printf("[PHOTO] SD %s\n", photo_sd_ready_ ? "ready" : "not_ready");
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
  File dir = SD.open("/pic");
  if (!dir || !dir.isDirectory()) {
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
  File dir = SD.open("/pic");
  if (!dir || !dir.isDirectory()) {
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
    Serial.printf("[PHOTO] epd4 size mismatch got=%u expected=%u\n",
                  static_cast<unsigned>(total), static_cast<unsigned>(kEpd4Bytes));
    return false;
  }

  EPD_W21_WriteCMD(0x12);
  EPD_W21_WriteDATA(0x00);
  delay(1);
  waitEpdReadyWithLed();
  return true;
}

void App::renderCalendarPage() {
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
