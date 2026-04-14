#include "render/SceneRasterizer.h"

#include <vector>

#include "Display_EPD_W21.h"
#include "calendar/CalendarScene.h"
#include "calendar/CalendarText.h"

namespace render {
namespace {

bool logicalToPhysical(const calendar::CalendarLayout &layout, uint16_t x, uint16_t y,
                       uint16_t &physical_x, uint16_t &physical_y) {
  if (layout.mode == calendar::LayoutMode::PortraitSplit) {
    if (x >= layout.screen.w || y >= layout.screen.h) {
      return false;
    }
    physical_x = y;
    physical_y = static_cast<uint16_t>(layout.screen.w - 1u - x);
    return true;
  }
  if (x >= layout.screen.w || y >= layout.screen.h) {
    return false;
  }
  physical_x = x;
  physical_y = y;
  return true;
}

class StripeSceneSink : public calendar::SceneSink {
 public:
  StripeSceneSink(StripeBuffer &buffer, uint16_t stripe_y, const calendar::CalendarLayout &layout)
      : buffer_(buffer), stripe_y_(stripe_y), layout_(layout) {}

  void fillRect(const calendar::Rect &rect, uint8_t color_nibble) override {
    if (!buffer_.ready() || rect.w == 0 || rect.h == 0) {
      return;
    }
    int32_t x0 = rect.x;
    int32_t y0 = rect.y;
    int32_t x1 = static_cast<int32_t>(rect.x + rect.w);
    int32_t y1 = static_cast<int32_t>(rect.y + rect.h);

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > static_cast<int32_t>(layout_.screen.w)) x1 = layout_.screen.w;
    if (y1 > static_cast<int32_t>(layout_.screen.h)) y1 = layout_.screen.h;
    if (x0 >= x1 || y0 >= y1) {
      return;
    }

    for (int32_t y = y0; y < y1; ++y) {
      for (int32_t x = x0; x < x1; ++x) {
        setPixel(static_cast<uint16_t>(x), static_cast<uint16_t>(y), color_nibble);
      }
    }
  }

  void strokeRect(const calendar::Rect &rect, uint8_t color_nibble) override {
    if (rect.w < 2 || rect.h < 2) {
      return;
    }
    fillRect(calendar::makeRect(rect.x, rect.y, rect.w, 1), color_nibble);
    fillRect(calendar::makeRect(rect.x, static_cast<uint16_t>(rect.y + rect.h - 1), rect.w, 1),
             color_nibble);
    fillRect(calendar::makeRect(rect.x, rect.y, 1, rect.h), color_nibble);
    fillRect(calendar::makeRect(static_cast<uint16_t>(rect.x + rect.w - 1), rect.y, 1, rect.h),
             color_nibble);
  }

  void text(uint16_t x, uint16_t y, const String &text, uint8_t pixel_height, uint8_t color_nibble,
            calendar::TextFont font, calendar::TextAAMode aa_mode) override {
    if (!buffer_.ready() || text.length() == 0 || pixel_height == 0) {
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
            fillRect(calendar::makeRect(static_cast<uint16_t>(x + col), static_cast<uint16_t>(y + row),
                                        1, 1),
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
          fillRect(calendar::makeRect(static_cast<uint16_t>(pen_x + dx), static_cast<uint16_t>(y + dy),
                                      1, 1),
                   color_nibble);
        }
      }
      pen_x = static_cast<uint16_t>(pen_x + draw_w + calendar::glyphLetterSpacingPx(glyph, style));
    }
  }

 private:
  void setPixel(uint16_t x, uint16_t y, uint8_t color_nibble) {
    uint16_t physical_x = 0;
    uint16_t physical_y = 0;
    if (!logicalToPhysical(layout_, x, y, physical_x, physical_y)) {
      return;
    }
    if (physical_x >= buffer_.widthPx() || physical_y < stripe_y_ ||
        physical_y >= static_cast<uint16_t>(stripe_y_ + buffer_.rows())) {
      return;
    }
    const uint16_t local_y = static_cast<uint16_t>(physical_y - stripe_y_);
    const uint32_t pixel_index = static_cast<uint32_t>(local_y) * buffer_.widthPx() + physical_x;
    const uint32_t byte_index = pixel_index >> 1;
    const uint8_t nib = static_cast<uint8_t>(color_nibble & 0x0Fu);
    uint8_t *bytes = buffer_.data();
    if ((pixel_index & 0x01u) == 0u) {
      bytes[byte_index] = static_cast<uint8_t>((bytes[byte_index] & 0x0Fu) | (nib << 4));
    } else {
      bytes[byte_index] = static_cast<uint8_t>((bytes[byte_index] & 0xF0u) | nib);
    }
  }

  StripeBuffer &buffer_;
  uint16_t stripe_y_ = 0;
  const calendar::CalendarLayout &layout_;
};

}  // namespace

uint16_t calendarPhysicalWidth(const calendar::CalendarLayout &layout) {
  return (layout.mode == calendar::LayoutMode::PortraitSplit) ? layout.screen.h : layout.screen.w;
}

uint16_t calendarPhysicalHeight(const calendar::CalendarLayout &layout) {
  return (layout.mode == calendar::LayoutMode::PortraitSplit) ? layout.screen.w : layout.screen.h;
}

bool calendarLogicalToPhysical(const calendar::CalendarLayout &layout, uint16_t x, uint16_t y,
                               uint16_t &physical_x, uint16_t &physical_y) {
  return logicalToPhysical(layout, x, y, physical_x, physical_y);
}

bool rasterizeCalendarSceneStripe(const calendar::CalendarModel &model,
                                  const calendar::CalendarLayout &layout, uint16_t stripe_y,
                                  uint16_t stripe_rows, StripeBuffer &buffer) {
  if (!buffer.ensure(calendarPhysicalWidth(layout), stripe_rows)) {
    return false;
  }
  buffer.clear(white);
  StripeSceneSink sink(buffer, stripe_y, layout);
  calendar::emitCalendarScene(model, layout, sink);
  return true;
}

}  // namespace render
