#include "app/App.h"

#include "Display_EPD_W21.h"
#include "Display_EPD_W21_spi.h"
#include "display/ColorMap.h"
#include "image.h"
#include <SPI.h>

namespace {
constexpr uint32_t kPhotoIntervalMs = 300000;  // 5 minutes
constexpr uint8_t kKeyUpPin = 34;
constexpr uint8_t kKeyMidPin = 35;
constexpr uint8_t kKeyDownPin = 0;
constexpr uint8_t kIndicatorLedPin = 2;
constexpr uint8_t kPowerCtrlPin = 32;
constexpr uint8_t kSpiSckPin = 13;
constexpr uint8_t kSpiMisoPin = 12;
constexpr uint8_t kSpiMosiPin = 14;
constexpr uint8_t kSpiCsPin = 33;

enum class PhotoFrame : uint8_t {
  PatternPaletteBands = 0,
  BuiltinImage = 1,
  PatternHorizontalBars = 2,
  PatternChecker = 3,
};

constexpr PhotoFrame kPhotoFrames[] = {
    PhotoFrame::PatternPaletteBands,
    PhotoFrame::BuiltinImage,
    PhotoFrame::PatternHorizontalBars,
    PhotoFrame::PatternChecker,
};

constexpr uint16_t kPhotoCount =
    static_cast<uint16_t>(sizeof(kPhotoFrames) / sizeof(kPhotoFrames[0]));

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
  needs_render_ = true;

  input_.begin(kKeyUpPin, kKeyMidPin, kKeyDownPin);
  mode_manager_.begin(last_photo_switch_ms_);
  led_manager_.begin(kIndicatorLedPin);
  wifi_manager_.begin();
  pinMode(kPowerCtrlPin, OUTPUT);
  digitalWrite(kPowerCtrlPin, LOW);

  Serial.println("[APP] begin");
  Serial.printf("[INPUT] key pins up=%u mid=%u down=%u\n", kKeyUpPin, kKeyMidPin,
                kKeyDownPin);
  Serial.printf("[POWER] epd rail pin=%u default=OFF\n", kPowerCtrlPin);
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

  if (mode_manager_.consumeApRequest()) {
    wifi_manager_.startAP();
  }
  if (mode_manager_.consumeStaRequest()) {
    wifi_manager_.startSTA();
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
      prevPhoto("key_up");
    } else if (state_ == AppState::Photo && event == appfw::InputEvent::DownShort) {
      nextPhoto("key_down");
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
  if ((now_ms - last_photo_switch_ms_) < kPhotoIntervalMs) {
    return;
  }
  nextPhoto("auto");
}

void App::nextPhoto(const char *reason) {
  if (kPhotoCount == 0) {
    return;
  }
  led_manager_.triggerBreath(2);
  photo_index_ = static_cast<uint16_t>((photo_index_ + 1) % kPhotoCount);
  last_photo_switch_ms_ = millis();
  needs_render_ = true;
  Serial.printf("[PHOTO] next -> index=%u/%u reason=%s\n", photo_index_ + 1, kPhotoCount,
                reason);
}

void App::prevPhoto(const char *reason) {
  if (kPhotoCount == 0) {
    return;
  }
  led_manager_.triggerBreath(2);
  if (photo_index_ == 0) {
    photo_index_ = static_cast<uint16_t>(kPhotoCount - 1);
  } else {
    photo_index_ = static_cast<uint16_t>(photo_index_ - 1);
  }
  last_photo_switch_ms_ = millis();
  needs_render_ = true;
  Serial.printf("[PHOTO] prev -> index=%u/%u reason=%s\n", photo_index_ + 1, kPhotoCount,
                reason);
}

void App::beginDisplaySession() {
  digitalWrite(kPowerCtrlPin, HIGH);
  delay(3);

  // Re-init SPI each active session to align with low-power power-cycle policy.
  SPI.end();
  SPI.begin(kSpiSckPin, kSpiMisoPin, kSpiMosiPin, kSpiCsPin);

  EPD_init_fast();
}

void App::endDisplaySession() {
  EPD_sleep();
  delay(2);
  digitalWrite(kPowerCtrlPin, LOW);
}

void App::displayImage(const unsigned char *pic_data) {
  EPD_W21_WriteCMD(0x10);
  for (uint16_t y = 0; y < 480; ++y) {
    if ((y & 0x0FU) == 0) {
      led_manager_.update(mode_manager_.mode(), millis());
    }
    uint16_t k = 0;
    for (uint16_t x_pair = 0; x_pair < 400; ++x_pair) {
      const uint8_t p0 = pic_data[y * 800 + k++];
      const uint8_t p1 = pic_data[y * 800 + k++];
      const uint8_t c0 = color_map::mapImageByteToNibble(p0);
      const uint8_t c1 = color_map::mapImageByteToNibble(p1);
      EPD_W21_WriteDATA(color_map::packNibbles(c0, c1));
    }
  }
  EPD_W21_WriteCMD(0x12);
  EPD_W21_WriteDATA(0x00);
  delay(1);
  lcd_chkstatus();
}

void App::setState(AppState next) {
  if (next == state_) {
    return;
  }
  state_ = next;
  needs_render_ = true;
}

void App::renderPhotoPage() {
  if (kPhotoCount == 0) {
    PIC_display_Clear();
    return;
  }

  const PhotoFrame frame = kPhotoFrames[photo_index_];
  if (frame == PhotoFrame::PatternPaletteBands) {
    drawPatternPaletteBands();
    return;
  }
  if (frame == PhotoFrame::BuiltinImage) {
    displayImage(gImage_1);
    return;
  }
  if (frame == PhotoFrame::PatternHorizontalBars) {
    drawPatternHorizontalBars();
    return;
  }
  drawPatternChecker();
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
  lcd_chkstatus();
}

void App::drawPatternPaletteBands() {
  static constexpr uint16_t band_width = 800 / 7;
  static constexpr uint8_t colors[] = {black, white, yellow, red, blue, green};

  EPD_W21_WriteCMD(0x10);
  for (uint16_t y = 0; y < 480; ++y) {
    if ((y & 0x0FU) == 0) {
      led_manager_.update(mode_manager_.mode(), millis());
    }
    for (uint16_t x_pair = 0; x_pair < 400; ++x_pair) {
      const uint16_t x0 = static_cast<uint16_t>(x_pair * 2);
      const uint16_t x1 = static_cast<uint16_t>(x0 + 1);
      const uint8_t band0 = static_cast<uint8_t>(x0 / band_width);
      const uint8_t band1 = static_cast<uint8_t>(x1 / band_width);

      uint8_t c0 = (band0 < 6) ? colors[band0] : color_map::orangeApproxNibble4x4(x0, y);
      uint8_t c1 = (band1 < 6) ? colors[band1] : color_map::orangeApproxNibble4x4(x1, y);
      EPD_W21_WriteDATA(color_map::packNibbles(c0, c1));
    }
  }
  EPD_W21_WriteCMD(0x12);
  EPD_W21_WriteDATA(0x00);
  delay(1);
  lcd_chkstatus();
}

void App::drawPatternHorizontalBars() {
  static constexpr uint8_t colors[] = {white, black, green, blue, red, yellow};
  static constexpr uint16_t bar_height = 480 / 6;

  EPD_W21_WriteCMD(0x10);
  for (uint16_t y = 0; y < 480; ++y) {
    if ((y & 0x0FU) == 0) {
      led_manager_.update(mode_manager_.mode(), millis());
    }
    const uint8_t c = colors[(y / bar_height) % 6];
    const uint8_t packed = color_map::packNibbles(c, c);
    for (uint16_t x_pair = 0; x_pair < 400; ++x_pair) {
      EPD_W21_WriteDATA(packed);
    }
  }
  EPD_W21_WriteCMD(0x12);
  EPD_W21_WriteDATA(0x00);
  delay(1);
  lcd_chkstatus();
}

void App::drawPatternChecker() {
  static constexpr uint16_t cell = 40;

  EPD_W21_WriteCMD(0x10);
  for (uint16_t y = 0; y < 480; ++y) {
    if ((y & 0x0FU) == 0) {
      led_manager_.update(mode_manager_.mode(), millis());
    }
    for (uint16_t x_pair = 0; x_pair < 400; ++x_pair) {
      const uint16_t x0 = static_cast<uint16_t>(x_pair * 2);
      const uint16_t x1 = static_cast<uint16_t>(x0 + 1);
      const bool b0 = (((x0 / cell) + (y / cell)) & 1U) != 0;
      const bool b1 = (((x1 / cell) + (y / cell)) & 1U) != 0;
      const uint8_t c0 = b0 ? blue : yellow;
      const uint8_t c1 = b1 ? blue : yellow;
      EPD_W21_WriteDATA(color_map::packNibbles(c0, c1));
    }
  }
  EPD_W21_WriteCMD(0x12);
  EPD_W21_WriteDATA(0x00);
  delay(1);
  lcd_chkstatus();
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
  lcd_chkstatus();
  endDisplaySession();
  Serial.println("[CONFIG] white screen action done");
}
