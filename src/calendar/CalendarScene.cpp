#include "calendar/CalendarScene.h"

#include <algorithm>

#include "Display_EPD_W21.h"
#include "calendar/CalendarText.h"

namespace calendar {
namespace {

constexpr uint8_t kAsciiBasePx = 7;
constexpr uint8_t kZhWeekdayPx = 26;
constexpr uint8_t kHeaderDatePx = 20;
constexpr uint8_t kHeaderTimePx = 30;
constexpr uint8_t kHeaderWeatherPx = 30;
constexpr uint8_t kHeaderSensorsPx = 20;
constexpr uint16_t kHeaderWeatherIconSize = 24u;
constexpr uint16_t kHeaderWeatherIconGap = 6u;
constexpr uint16_t kHeaderWeatherIconOffsetX = 12u;
constexpr uint16_t kHeaderWeatherIconOffsetY = 2u;
constexpr bool kShowAATestPanel = false;
constexpr uint16_t kScheduleStartMinute = 8u * 60u;
constexpr uint16_t kScheduleEndMinute = 22u * 60u;
constexpr uint8_t kScheduleSlotCount = 28u;

struct HeaderMetrics {
  uint16_t card_x = 0;
  uint16_t card_y = 0;
  uint16_t card_w = 0;
  uint16_t card_h = 0;
  uint16_t date_x = 0;
  uint16_t date_y = 0;
  uint16_t time_x = 0;
  uint16_t time_y = 0;
  uint16_t meta_x = 0;
  uint16_t weather_y = 0;
  uint16_t sensors_y = 0;
};

uint8_t asciiPixelHeight(uint8_t scale) {
  return static_cast<uint8_t>(kAsciiBasePx * scale);
}

bool isWeekendColumn(uint8_t col) {
  return col == 0 || col == 6;
}

bool isAsciiOnlyText(const String &text) {
  for (size_t i = 0; i < text.length(); ++i) {
    if (static_cast<uint8_t>(text.charAt(i)) >= 0x80u) {
      return false;
    }
  }
  return true;
}

TextFont preferredTextFont(const String &text, TextFont fallback_font, uint8_t pixel_height) {
  if (isAsciiOnlyText(text) && pixel_height >= 14u) {
    return TextFont::AsciiSmooth;
  }
  return fallback_font;
}

TextAAMode preferredAsciiAAMode(const String &text, TextFont font, uint8_t pixel_height) {
  (void)text;
  (void)font;
  (void)pixel_height;
  return TextAAMode::Threshold;
}

void plotCirclePoints(SceneSink &sink, int cx, int cy, int x, int y, uint8_t color_nibble) {
  const Rect pts[] = {
      makeRect(static_cast<uint16_t>(cx + x), static_cast<uint16_t>(cy + y), 1, 1),
      makeRect(static_cast<uint16_t>(cx - x), static_cast<uint16_t>(cy + y), 1, 1),
      makeRect(static_cast<uint16_t>(cx + x), static_cast<uint16_t>(cy - y), 1, 1),
      makeRect(static_cast<uint16_t>(cx - x), static_cast<uint16_t>(cy - y), 1, 1),
      makeRect(static_cast<uint16_t>(cx + y), static_cast<uint16_t>(cy + x), 1, 1),
      makeRect(static_cast<uint16_t>(cx - y), static_cast<uint16_t>(cy + x), 1, 1),
      makeRect(static_cast<uint16_t>(cx + y), static_cast<uint16_t>(cy - x), 1, 1),
      makeRect(static_cast<uint16_t>(cx - y), static_cast<uint16_t>(cy - x), 1, 1),
  };
  for (const Rect &pt : pts) {
    sink.fillRect(pt, color_nibble);
  }
}

void emitHorizontalSpan(SceneSink &sink, int x0, int x1, int y, uint8_t color_nibble) {
  if (x1 < x0 || y < 0) {
    return;
  }
  sink.fillRect(makeRect(static_cast<uint16_t>(x0), static_cast<uint16_t>(y),
                         static_cast<uint16_t>(x1 - x0 + 1), 1),
                color_nibble);
}

void emitVerticalSpan(SceneSink &sink, int x, int y0, int y1, uint8_t color_nibble) {
  if (y1 < y0 || x < 0) {
    return;
  }
  sink.fillRect(makeRect(static_cast<uint16_t>(x), static_cast<uint16_t>(y0), 1,
                         static_cast<uint16_t>(y1 - y0 + 1)),
                color_nibble);
}

void emitFilledCircle(SceneSink &sink, uint16_t cx, uint16_t cy, uint16_t radius,
                      uint8_t color_nibble);

void emitLine(SceneSink &sink, int x0, int y0, int x1, int y1, uint8_t color_nibble) {
  int dx = abs(x1 - x0);
  int sx = (x0 < x1) ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx + dy;
  while (true) {
    sink.fillRect(makeRect(static_cast<uint16_t>(x0), static_cast<uint16_t>(y0), 1, 1),
                  color_nibble);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int e2 = err * 2;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void emitFilledRoundedRect(SceneSink &sink, const Rect &rect, uint16_t radius,
                           uint8_t color_nibble) {
  if (rect.w == 0 || rect.h == 0) {
    return;
  }
  const uint16_t max_radius = static_cast<uint16_t>(std::min(rect.w, rect.h) / 2u);
  radius = std::min(radius, max_radius);
  if (radius == 0u) {
    sink.fillRect(rect, color_nibble);
    return;
  }

  const uint16_t inner_h =
      (rect.h > radius * 2u) ? static_cast<uint16_t>(rect.h - radius * 2u) : 0u;
  const uint16_t inner_w =
      (rect.w > radius * 2u) ? static_cast<uint16_t>(rect.w - radius * 2u) : 0u;

  if (inner_w > 0u) {
    sink.fillRect(makeRect(static_cast<uint16_t>(rect.x + radius), rect.y, inner_w, rect.h),
                  color_nibble);
  }
  if (inner_h > 0u) {
    sink.fillRect(makeRect(rect.x, static_cast<uint16_t>(rect.y + radius), rect.w, inner_h),
                  color_nibble);
  }

  emitFilledCircle(sink, static_cast<uint16_t>(rect.x + radius),
                   static_cast<uint16_t>(rect.y + radius), radius, color_nibble);
  emitFilledCircle(sink, static_cast<uint16_t>(rect.x + rect.w - radius - 1u),
                   static_cast<uint16_t>(rect.y + radius), radius, color_nibble);
  emitFilledCircle(sink, static_cast<uint16_t>(rect.x + radius),
                   static_cast<uint16_t>(rect.y + rect.h - radius - 1u), radius, color_nibble);
  emitFilledCircle(sink, static_cast<uint16_t>(rect.x + rect.w - radius - 1u),
                   static_cast<uint16_t>(rect.y + rect.h - radius - 1u), radius, color_nibble);
}

void emitRoundedOutline(SceneSink &sink, const Rect &rect, uint16_t radius, uint8_t color_nibble,
                        uint8_t background_nibble, uint16_t thickness = 1u) {
  if (rect.w == 0 || rect.h == 0) {
    return;
  }
  emitFilledRoundedRect(sink, rect, radius, color_nibble);
  if (rect.w <= thickness * 2u || rect.h <= thickness * 2u) {
    return;
  }
  const Rect inner = makeRect(static_cast<uint16_t>(rect.x + thickness),
                              static_cast<uint16_t>(rect.y + thickness),
                              static_cast<uint16_t>(rect.w - thickness * 2u),
                              static_cast<uint16_t>(rect.h - thickness * 2u));
  const uint16_t inner_radius = (radius > thickness) ? static_cast<uint16_t>(radius - thickness) : 0u;
  emitFilledRoundedRect(sink, inner, inner_radius, background_nibble);
}

void emitCircleOutline(SceneSink &sink, uint16_t cx, uint16_t cy, uint16_t radius,
                       uint8_t color_nibble) {
  if (radius == 0) {
    return;
  }

  int x = static_cast<int>(radius);
  int y = 0;
  int decision = 1 - x;
  while (y <= x) {
    plotCirclePoints(sink, static_cast<int>(cx), static_cast<int>(cy), x, y, color_nibble);
    ++y;
    if (decision <= 0) {
      decision += 2 * y + 1;
    } else {
      --x;
      decision += 2 * (y - x) + 1;
    }
  }
}

void emitFilledCircle(SceneSink &sink, uint16_t cx, uint16_t cy, uint16_t radius,
                      uint8_t color_nibble) {
  if (radius == 0) {
    sink.fillRect(makeRect(cx, cy, 1, 1), color_nibble);
    return;
  }

  int x = static_cast<int>(radius);
  int y = 0;
  int decision = 1 - x;
  while (y <= x) {
    emitHorizontalSpan(sink, static_cast<int>(cx) - x, static_cast<int>(cx) + x,
                       static_cast<int>(cy) + y, color_nibble);
    emitHorizontalSpan(sink, static_cast<int>(cx) - x, static_cast<int>(cx) + x,
                       static_cast<int>(cy) - y, color_nibble);
    emitHorizontalSpan(sink, static_cast<int>(cx) - y, static_cast<int>(cx) + y,
                       static_cast<int>(cy) + x, color_nibble);
    emitHorizontalSpan(sink, static_cast<int>(cx) - y, static_cast<int>(cx) + y,
                       static_cast<int>(cy) - x, color_nibble);
    ++y;
    if (decision <= 0) {
      decision += 2 * y + 1;
    } else {
      --x;
      decision += 2 * (y - x) + 1;
    }
  }
}

HeaderMetrics computeHeaderMetrics(const CalendarLayout &layout, const CalendarModel &model,
                                   TextFont header_date_font) {
  HeaderMetrics metrics;
  metrics.card_x = layout.header_bar.x;
  metrics.card_y = layout.header_bar.y;
  metrics.card_w = layout.header_bar.w;
  metrics.card_h = layout.header_bar.h;

  const uint16_t left_pad = (layout.mode == LayoutMode::LandscapeSplit) ? 14u : 12u;
  const uint16_t top_pad = (layout.mode == LayoutMode::LandscapeSplit) ? 10u : 8u;
  const uint16_t right_pad = left_pad;
  metrics.date_x = static_cast<uint16_t>(metrics.card_x + left_pad);
  metrics.date_y = static_cast<uint16_t>(metrics.card_y + top_pad);
  metrics.time_x = metrics.date_x;
  metrics.time_y = static_cast<uint16_t>(
      metrics.date_y + textHeightPx(model.header_date, kHeaderDatePx, header_date_font) + 7u);

  const uint16_t meta_block_w = (layout.mode == LayoutMode::LandscapeSplit) ? 168u : 136u;
  metrics.meta_x =
      (metrics.card_w > meta_block_w + right_pad)
          ? static_cast<uint16_t>(metrics.card_x + metrics.card_w - meta_block_w - right_pad)
          : metrics.date_x;
  metrics.weather_y = (metrics.date_y > 2u) ? static_cast<uint16_t>(metrics.date_y - 2u) : metrics.date_y;
  metrics.sensors_y = static_cast<uint16_t>(metrics.weather_y + kHeaderWeatherPx + 6u);
  return metrics;
}

enum class WeatherIconKind : uint8_t {
  None = 0,
  Sun,
  PartlyCloudy,
  Cloud,
  Rain,
  Snow,
  Thunder,
  Fog,
};

WeatherIconKind weatherIconKindForCode(int weather_code) {
  switch (weather_code) {
    case 0:
      return WeatherIconKind::Sun;
    case 1:
    case 2:
      return WeatherIconKind::PartlyCloudy;
    case 3:
      return WeatherIconKind::Cloud;
    case 45:
    case 48:
      return WeatherIconKind::Fog;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
    case 80:
    case 81:
    case 82:
      return WeatherIconKind::Rain;
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86:
      return WeatherIconKind::Snow;
    case 95:
    case 96:
    case 99:
      return WeatherIconKind::Thunder;
    default:
      break;
  }
  return WeatherIconKind::Sun;
}

uint16_t weatherHeaderIconX(const HeaderMetrics &header, const String &weather_text,
                            TextFont weather_font) {
  return static_cast<uint16_t>(header.meta_x +
                               textWidthPx(weather_text, kHeaderWeatherPx, weather_font) +
                               kHeaderWeatherIconGap + kHeaderWeatherIconOffsetX);
}

uint16_t weatherHeaderIconY(const HeaderMetrics &header) {
  return static_cast<uint16_t>(header.weather_y + kHeaderWeatherIconOffsetY);
}

void emitWeatherCloud(SceneSink &sink, uint16_t x, uint16_t y, uint16_t size, uint8_t color_nibble) {
  const uint16_t base_y = static_cast<uint16_t>(y + (size * 11u) / 16u);
  const uint16_t left_cx = static_cast<uint16_t>(x + size / 3u);
  const uint16_t mid_cx = static_cast<uint16_t>(x + size / 2u);
  const uint16_t right_cx = static_cast<uint16_t>(x + (size * 2u) / 3u);
  const uint16_t left_r = static_cast<uint16_t>(size / 5u);
  const uint16_t mid_r = static_cast<uint16_t>(size / 4u);
  const uint16_t right_r = static_cast<uint16_t>(size / 5u);
  emitFilledCircle(sink, left_cx, base_y, left_r, color_nibble);
  emitFilledCircle(sink, mid_cx, static_cast<uint16_t>(base_y - left_r), mid_r, color_nibble);
  emitFilledCircle(sink, right_cx, static_cast<uint16_t>(base_y - right_r / 2u), right_r,
                   color_nibble);
  emitFilledRoundedRect(
      sink,
      makeRect(static_cast<uint16_t>(x + size / 5u), static_cast<uint16_t>(base_y),
               static_cast<uint16_t>((size * 3u) / 5u), static_cast<uint16_t>(size / 5u)),
      2u, color_nibble);
}

void emitWeatherCloudLayered(SceneSink &sink, uint16_t x, uint16_t y, uint16_t size,
                             uint8_t back_color, uint8_t front_color) {
  emitWeatherCloud(sink, x, static_cast<uint16_t>(y + 1u), size, back_color);
  emitWeatherCloud(sink, static_cast<uint16_t>(x + 1u), y, static_cast<uint16_t>(size - 1u),
                   front_color);
}

void emitWeatherDrop(SceneSink &sink, uint16_t x, uint16_t y, uint8_t color_nibble) {
  sink.fillRect(makeRect(x, static_cast<uint16_t>(y + 1u), 2u, 4u), color_nibble);
  sink.fillRect(makeRect(static_cast<uint16_t>(x + 1u), y, 1u, 1u), color_nibble);
  sink.fillRect(makeRect(static_cast<uint16_t>(x + 1u), static_cast<uint16_t>(y + 5u), 1u, 1u),
                color_nibble);
}

void emitWeatherSnowflake(SceneSink &sink, uint16_t cx, uint16_t cy, uint8_t color_nibble) {
  emitHorizontalSpan(sink, static_cast<int>(cx) - 2, static_cast<int>(cx) + 2, cy, color_nibble);
  emitVerticalSpan(sink, cx, static_cast<int>(cy) - 2, static_cast<int>(cy) + 2, color_nibble);
  emitLine(sink, static_cast<int>(cx) - 1, static_cast<int>(cy) - 1, static_cast<int>(cx) + 1,
           static_cast<int>(cy) + 1, color_nibble);
  emitLine(sink, static_cast<int>(cx) + 1, static_cast<int>(cy) - 1, static_cast<int>(cx) - 1,
           static_cast<int>(cy) + 1, color_nibble);
}

void emitWeatherSun(SceneSink &sink, uint16_t x, uint16_t y, uint16_t size) {
  const uint16_t cx = static_cast<uint16_t>(x + size / 2u);
  const uint16_t cy = static_cast<uint16_t>(y + size / 2u);
  const uint16_t disc_r = static_cast<uint16_t>(size / 4u);
  emitFilledCircle(sink, cx, cy, static_cast<uint16_t>(disc_r + 2u), red);
  emitFilledCircle(sink, cx, cy, disc_r, yellow);
  const uint16_t ray_top = static_cast<uint16_t>(y + 1u);
  const uint16_t ray_bottom = static_cast<uint16_t>(y + size - 2u);
  const uint16_t ray_left = static_cast<uint16_t>(x + 1u);
  const uint16_t ray_right = static_cast<uint16_t>(x + size - 2u);
  emitVerticalSpan(sink, cx, ray_top, static_cast<uint16_t>(cy - disc_r - 1u), red);
  emitVerticalSpan(sink, cx, static_cast<uint16_t>(cy + disc_r + 1u), ray_bottom, red);
  emitHorizontalSpan(sink, ray_left, static_cast<uint16_t>(cx - disc_r - 1u), cy, red);
  emitHorizontalSpan(sink, static_cast<uint16_t>(cx + disc_r + 1u), ray_right, cy, red);
  emitLine(sink, static_cast<int>(x + 3u), static_cast<int>(y + 3u),
           static_cast<int>(cx - disc_r), static_cast<int>(cy - disc_r), red);
  emitLine(sink, static_cast<int>(x + size - 4u), static_cast<int>(y + 3u),
           static_cast<int>(cx + disc_r), static_cast<int>(cy - disc_r), red);
  emitLine(sink, static_cast<int>(x + 3u), static_cast<int>(y + size - 4u),
           static_cast<int>(cx - disc_r), static_cast<int>(cy + disc_r), red);
  emitLine(sink, static_cast<int>(x + size - 4u), static_cast<int>(y + size - 4u),
           static_cast<int>(cx + disc_r), static_cast<int>(cy + disc_r), red);
}

void emitWeatherPartlyCloudy(SceneSink &sink, uint16_t x, uint16_t y, uint16_t size) {
  emitWeatherSun(sink, x, static_cast<uint16_t>(y - 1u), static_cast<uint16_t>(size - 4u));
  emitWeatherCloudLayered(sink, static_cast<uint16_t>(x + 6u), static_cast<uint16_t>(y + 5u),
                          static_cast<uint16_t>(size - 4u), black, blue);
}

void emitWeatherRain(SceneSink &sink, uint16_t x, uint16_t y, uint16_t size) {
  emitWeatherCloudLayered(sink, x, y, size, black, blue);
  const uint16_t drop_y = static_cast<uint16_t>(y + (size * 3u) / 4u + 1u);
  emitWeatherDrop(sink, static_cast<uint16_t>(x + size / 4u), drop_y, blue);
  emitWeatherDrop(sink, static_cast<uint16_t>(x + size / 2u - 1u),
                  static_cast<uint16_t>(drop_y + 2u), blue);
  emitWeatherDrop(sink, static_cast<uint16_t>(x + (size * 2u) / 3u + 1u), drop_y, green);
}

void emitWeatherSnow(SceneSink &sink, uint16_t x, uint16_t y, uint16_t size) {
  emitWeatherCloudLayered(sink, x, y, size, black, blue);
  const uint16_t snow_y = static_cast<uint16_t>(y + (size * 3u) / 4u + 1u);
  emitWeatherSnowflake(sink, static_cast<uint16_t>(x + size / 3u), snow_y, green);
  emitWeatherSnowflake(sink, static_cast<uint16_t>(x + (size * 2u) / 3u), snow_y, blue);
}

void emitWeatherThunder(SceneSink &sink, uint16_t x, uint16_t y, uint16_t size) {
  emitWeatherCloudLayered(sink, x, y, size, blue, black);
  const uint16_t bolt_x = static_cast<uint16_t>(x + size / 2u + 1u);
  const uint16_t bolt_y = static_cast<uint16_t>(y + size / 2u + 1u);
  sink.fillRect(makeRect(static_cast<uint16_t>(bolt_x - 1u), bolt_y, 3u,
                         static_cast<uint16_t>(size / 5u)),
                yellow);
  sink.fillRect(makeRect(static_cast<uint16_t>(bolt_x - 4u),
                         static_cast<uint16_t>(bolt_y + size / 7u), 3u,
                         static_cast<uint16_t>(size / 5u)),
                red);
  sink.fillRect(makeRect(static_cast<uint16_t>(bolt_x - 1u),
                         static_cast<uint16_t>(bolt_y + (size * 2u) / 7u), 2u,
                         static_cast<uint16_t>(size / 7u)),
                yellow);
}

void emitWeatherFog(SceneSink &sink, uint16_t x, uint16_t y, uint16_t size) {
  emitWeatherCloudLayered(sink, x, y, size, black, blue);
  for (uint8_t i = 0; i < 3u; ++i) {
    const uint16_t yy = static_cast<uint16_t>(y + size / 2u + 4u + i * 4u);
    emitHorizontalSpan(sink, static_cast<int>(x + 1u), static_cast<int>(x + size - 4u), yy, yellow);
    emitHorizontalSpan(sink, static_cast<int>(x + 3u), static_cast<int>(x + size - 2u),
                       static_cast<uint16_t>(yy + 1u), blue);
  }
}

void emitWeatherIcon(SceneSink &sink, uint16_t x, uint16_t y, uint16_t size, int weather_code) {
  switch (weatherIconKindForCode(weather_code)) {
    case WeatherIconKind::Sun:
      emitWeatherSun(sink, x, y, size);
      return;
    case WeatherIconKind::PartlyCloudy:
      emitWeatherPartlyCloudy(sink, x, y, size);
      return;
    case WeatherIconKind::Cloud:
      emitWeatherCloudLayered(sink, x, y, size, black, blue);
      return;
    case WeatherIconKind::Rain:
      emitWeatherRain(sink, x, y, size);
      return;
    case WeatherIconKind::Snow:
      emitWeatherSnow(sink, x, y, size);
      return;
    case WeatherIconKind::Thunder:
      emitWeatherThunder(sink, x, y, size);
      return;
    case WeatherIconKind::Fog:
      emitWeatherFog(sink, x, y, size);
      return;
    case WeatherIconKind::None:
    default:
      return;
  }
}

String truncateWithTilde(const String &text, size_t max_chars) {
  if (max_chars < 1) {
    return "~";
  }
  if (text.length() <= max_chars) {
    return text;
  }
  if (max_chars == 1) {
    return "~";
  }
  return text.substring(0, max_chars - 1) + "~";
}

String truncateTextToWidth(const String &text, uint16_t max_width_px, uint8_t pixel_height,
                           TextFont font) {
  if (text.length() == 0 || max_width_px == 0 || pixel_height == 0) {
    return "";
  }
  const TextStyle style = resolveTextStyle(pixel_height, font);
  const uint16_t full_width = textWidthPx(text, pixel_height, font);
  if (full_width <= max_width_px) {
    return text;
  }

  const uint16_t tilde_width = textWidthPx("~", kAsciiBasePx, TextFont::Auto);
  const uint16_t target_width =
      (max_width_px > tilde_width) ? static_cast<uint16_t>(max_width_px - tilde_width) : 0u;

  size_t byte_index = 0;
  size_t last_fit = 0;
  uint16_t width = 0;
  bool first = true;
  GlyphBitmap glyph;
  while (nextTextGlyph(text, byte_index, glyph, style.font)) {
    if (glyph.rows == nullptr || glyph.width == 0 || glyph.height == 0) {
      continue;
    }
    const uint16_t advance = static_cast<uint16_t>(
        (first ? 0 : style.letter_spacing) + glyphWidthPx(glyph, style));
    if ((width + advance) > target_width) {
      break;
    }
    width = static_cast<uint16_t>(width + advance);
    last_fit = byte_index;
    first = false;
  }

  if (last_fit == 0) {
    return "~";
  }
  return text.substring(0, last_fit) + "~";
}

void emitAATestPanel(SceneSink &sink, const CalendarLayout &layout) {
  const uint16_t panel_x = static_cast<uint16_t>(layout.schedule_inner.x + 8);
  const uint16_t panel_y = static_cast<uint16_t>(layout.schedule_inner.y + 8);
  const uint16_t panel_w =
      static_cast<uint16_t>(layout.schedule_inner.w > 16 ? (layout.schedule_inner.w - 16) : 0);
  if (panel_w == 0) {
    return;
  }

  sink.text(panel_x, panel_y, "BURKES AA", 10, green);

  static const uint8_t kSizes[] = {16, 20, 24};
  const String sample = "\xE5\xAE\x89\xE6\x8E\x92\xE6\xA0\xBC\xE8\xA8\x80";  // 安排格言

  const String sample_text = "\xE5\xAE\x89\xE6\x8E\x92\xE6\x97\xA5\xE7\xA8\x8B";
  const uint16_t row_top = static_cast<uint16_t>(panel_y + 24);
  sink.fillRect(makeRect(panel_x, row_top, panel_w, 1), green);
  sink.text(panel_x, static_cast<uint16_t>(row_top + 6), "BURKES", 10, black);

  uint16_t sample_x = static_cast<uint16_t>(panel_x + 74);
  for (uint8_t size : kSizes) {
    sink.text(sample_x, static_cast<uint16_t>(row_top + 2), sample_text, size, black,
              TextFont::CjkAuto,
              TextAAMode::Burkes);
    sample_x =
        static_cast<uint16_t>(sample_x + textWidthPx(sample_text, size, TextFont::CjkAuto) + 18);
  }
}

uint16_t timelineYForMinute(const CalendarLayout &layout, uint16_t minute_value) {
  if (minute_value <= kScheduleStartMinute) {
    return layout.list_top;
  }
  if (minute_value >= kScheduleEndMinute) {
    return layout.list_bottom;
  }
  const uint32_t usable_h =
      (layout.list_bottom > layout.list_top) ? static_cast<uint32_t>(layout.list_bottom - layout.list_top) : 0u;
  return static_cast<uint16_t>(
      layout.list_top +
      ((static_cast<uint32_t>(minute_value - kScheduleStartMinute) * usable_h) /
       static_cast<uint32_t>(kScheduleEndMinute - kScheduleStartMinute)));
}

void emitCurrentTimeMarker(SceneSink &sink, const CalendarLayout &layout,
                           uint16_t current_minute_of_day) {
  const uint16_t marker_y = timelineYForMinute(layout, current_minute_of_day);
  const uint16_t tip_x = static_cast<uint16_t>(layout.items_x);
  const uint16_t marker_w = 10;
  const uint16_t marker_h = 14;
  const int mid_y = static_cast<int>(marker_y);
  for (uint16_t dx = 0; dx < marker_w; ++dx) {
    const int span = static_cast<int>((dx * marker_h) / marker_w);
    const int y0 = mid_y - span / 2;
    const int y1 = mid_y + span / 2;
    if (y1 < static_cast<int>(layout.list_top) || y0 > static_cast<int>(layout.list_bottom)) {
      continue;
    }
    const uint16_t clamped_y0 =
        static_cast<uint16_t>(std::max(y0, static_cast<int>(layout.list_top)));
    const uint16_t clamped_y1 =
        static_cast<uint16_t>(std::min(y1, static_cast<int>(layout.list_bottom)));
    if (clamped_y1 < clamped_y0) {
      continue;
    }
    sink.fillRect(makeRect(static_cast<uint16_t>(tip_x - dx), clamped_y0, 1,
                           static_cast<uint16_t>(clamped_y1 - clamped_y0 + 1)),
                  red);
  }
}

}  // namespace

void emitCalendarWeatherHeader(const CalendarModel &model, const CalendarLayout &layout, SceneSink &sink) {
  const bool zh_ui = (model.ui_language == "zh");
  const uint8_t header_weather_px = kHeaderWeatherPx;
  const TextFont header_font = zh_ui ? TextFont::CjkAuto : TextFont::Auto;
  const TextFont header_date_font = preferredTextFont(model.header_date, header_font, kHeaderDatePx);
  const HeaderMetrics header = computeHeaderMetrics(layout, model, header_date_font);
  const TextFont header_weather_font =
      preferredTextFont(model.header_weather, header_font, header_weather_px);
  const TextAAMode header_weather_aa =
      preferredAsciiAAMode(model.header_weather, header_weather_font, header_weather_px);
  const uint16_t icon_x = weatherHeaderIconX(header, model.header_weather, header_weather_font);
  const uint16_t icon_y = weatherHeaderIconY(header);
  sink.text(header.meta_x, header.weather_y, model.header_weather, header_weather_px, blue,
            header_weather_font, header_weather_aa);
  emitWeatherIcon(sink, icon_x, icon_y, kHeaderWeatherIconSize, model.header_weather_code);
}

void emitCalendarScene(const CalendarModel &model, const CalendarLayout &layout, SceneSink &sink) {
  const bool zh_ui = (model.ui_language == "zh");
  const uint8_t weekday_px =
      zh_ui ? kZhWeekdayPx
            : static_cast<uint8_t>(asciiPixelHeight(layout.weekday_scale) +
                                   (layout.mode == LayoutMode::LandscapeSplit ? 2 : 1));
  const uint8_t item_px = zh_ui ? static_cast<uint8_t>(14) : static_cast<uint8_t>(10);
  const uint8_t header_date_px = kHeaderDatePx;
  const uint8_t header_time_px = kHeaderTimePx;
  const uint8_t header_weather_px = kHeaderWeatherPx;
  const uint8_t header_sensors_px = kHeaderSensorsPx;
  const uint8_t day_px =
      (layout.mode == LayoutMode::LandscapeSplit) ? static_cast<uint8_t>(18)
                                                  : static_cast<uint8_t>(16);
  const TextFont weekday_font = zh_ui ? TextFont::CjkAuto : TextFont::Auto;
  const TextFont item_font = zh_ui ? TextFont::CjkAuto : TextFont::Auto;
  const TextFont header_font = zh_ui ? TextFont::CjkAuto : TextFont::Auto;
  const TextFont header_date_font =
      preferredTextFont(model.header_date, header_font, header_date_px);
  const TextFont header_time_font =
      preferredTextFont(model.header_time, TextFont::Auto, header_time_px);
  const TextFont header_weather_font =
      preferredTextFont(model.header_weather, header_font, header_weather_px);
  const TextFont header_sensors_font =
      preferredTextFont(model.header_sensors, header_font, header_sensors_px);
  const TextAAMode header_date_aa =
      preferredAsciiAAMode(model.header_date, header_date_font, header_date_px);
  const TextAAMode header_time_aa =
      preferredAsciiAAMode(model.header_time, header_time_font, header_time_px);
  const TextAAMode header_weather_aa =
      preferredAsciiAAMode(model.header_weather, header_weather_font, header_weather_px);
  const TextAAMode header_sensors_aa =
      preferredAsciiAAMode(model.header_sensors, header_sensors_font, header_sensors_px);
  const HeaderMetrics header = computeHeaderMetrics(layout, model, header_date_font);
  const Rect header_card = makeRect(header.card_x, header.card_y, header.card_w, header.card_h);
  emitRoundedOutline(sink, header_card, 12u, black, white, 2u);
  sink.text(header.date_x, header.date_y, model.header_date, header_date_px, black, header_date_font,
            header_date_aa);
  sink.text(header.time_x, header.time_y, model.header_time, header_time_px, black, header_time_font,
            header_time_aa);
  emitCalendarWeatherHeader(model, layout, sink);
  sink.text(header.meta_x, header.sensors_y, model.header_sensors, header_sensors_px, green,
            header_sensors_font, header_sensors_aa);

  emitRoundedOutline(sink, layout.schedule_inner, 10u, black, white, 1u);

  if (layout.has_grid) {
    for (uint8_t col = 0; col < 7; ++col) {
      const String label = model.weekday_labels[col];
      const uint16_t label_w = textWidthPx(label, weekday_px, weekday_font);
      const uint16_t label_x = static_cast<uint16_t>(
          layout.grid.x + col * layout.cell_w +
          ((layout.cell_w > label_w) ? (layout.cell_w - label_w) / 2u : 0u));
      const TextFont label_font = preferredTextFont(label, weekday_font, weekday_px);
      sink.text(label_x, layout.weekday_y, label, weekday_px, isWeekendColumn(col) ? blue : black,
                label_font, preferredAsciiAAMode(label, label_font, weekday_px));
    }
  }

  if (kShowAATestPanel && layout.mode == LayoutMode::LandscapeSplit) {
    emitAATestPanel(sink, layout);
    return;
  }

  const uint16_t axis_x = static_cast<uint16_t>(layout.schedule_inner.x + 8);
  const uint16_t timeline_left = static_cast<uint16_t>(layout.items_x + 2);
  const uint16_t timeline_right =
      static_cast<uint16_t>(layout.schedule_inner.x + layout.schedule_inner.w - 8);
  const uint16_t timeline_w =
      (timeline_right > timeline_left) ? static_cast<uint16_t>(timeline_right - timeline_left) : 0u;

  for (uint8_t slot = 0; slot <= kScheduleSlotCount; ++slot) {
    const uint16_t minute_value = static_cast<uint16_t>(kScheduleStartMinute + slot * 30u);
    const uint16_t y = timelineYForMinute(layout, minute_value);
    const bool is_hour_line = ((slot % 2u) == 0u);
    if (slot < kScheduleSlotCount) {
      if (is_hour_line) {
        const String hour_label = String((minute_value / 60u < 10u) ? "0" : "") +
                                  String(minute_value / 60u);
        sink.text(axis_x, static_cast<uint16_t>(y + 1), hour_label, asciiPixelHeight(1), black,
                  TextFont::AsciiSmooth,
                  preferredAsciiAAMode(hour_label, TextFont::AsciiSmooth, asciiPixelHeight(1)));
      }
    }
    if (timeline_w == 0) {
      continue;
    }
    if (!is_hour_line) {
      continue;
    }
    sink.fillRect(makeRect(timeline_left, y, timeline_w, 1), blue);
  }

  if (model.time_valid && timeline_w > 0) {
    emitCurrentTimeMarker(sink, layout, model.current_minute_of_day);
  }

  for (size_t i = 0; i < model.visible_event_count; ++i) {
    const VisibleEvent &event = model.visible_events[i];
    uint16_t start_minute = event.start_minute;
    uint16_t end_minute = event.end_minute;
    if (end_minute <= start_minute) {
      end_minute = static_cast<uint16_t>(start_minute + 30u);
    }
    if (end_minute <= kScheduleStartMinute || start_minute >= kScheduleEndMinute) {
      continue;
    }
    if (start_minute < kScheduleStartMinute) {
      start_minute = kScheduleStartMinute;
    }
    if (end_minute > kScheduleEndMinute) {
      end_minute = kScheduleEndMinute;
    }

    const uint16_t y0 = timelineYForMinute(layout, start_minute);
    const uint16_t y1 = timelineYForMinute(layout, end_minute);
    uint16_t block_h = (y1 > y0) ? static_cast<uint16_t>(y1 - y0) : static_cast<uint16_t>(layout.row_h);
    if (block_h < 10) {
      block_h = 10;
    }

    const uint8_t lane_count = (event.lane_count == 0) ? 1 : event.lane_count;
    const uint16_t lane_gap = 3;
    const uint16_t available_w =
        (timeline_w > static_cast<uint16_t>((lane_count - 1u) * lane_gap))
            ? static_cast<uint16_t>(timeline_w - (lane_count - 1u) * lane_gap)
            : timeline_w;
    const uint16_t lane_w =
        (lane_count > 0) ? static_cast<uint16_t>(available_w / lane_count) : available_w;
    const uint16_t block_x = static_cast<uint16_t>(
        timeline_left + event.lane * static_cast<uint16_t>(lane_w + lane_gap));
    const uint16_t block_w =
        (lane_w > 1) ? static_cast<uint16_t>(lane_w - 1) : lane_w;
    const Rect block = makeRect(block_x, static_cast<uint16_t>(y0 + 1), block_w,
                                static_cast<uint16_t>(block_h > 2 ? block_h - 2 : block_h));
    emitRoundedOutline(sink, block, 4u, black, white, 1u);
    const uint8_t accent_color = (event.color_nibble == white) ? blue : event.color_nibble;
    const uint16_t accent_h = (block.h > 6u) ? static_cast<uint16_t>(block.h - 4u) : block.h;
    sink.fillRect(makeRect(static_cast<uint16_t>(block.x + 2u), static_cast<uint16_t>(block.y + 2u),
                           4u, accent_h),
                  accent_color);

    const uint16_t text_pad_x = 9;
    const uint16_t text_space =
        (block.w > text_pad_x * 2) ? static_cast<uint16_t>(block.w - text_pad_x * 2) : 0u;
    if (text_space == 0 || block.h < 8) {
      continue;
    }
    const String visible_title = truncateTextToWidth(event.title, text_space, item_px, item_font);
    const TextFont title_font = preferredTextFont(visible_title, item_font, item_px);
    const uint16_t text_y = static_cast<uint16_t>(
        block.y + ((block.h > textHeightPx(visible_title, item_px, title_font))
                        ? (block.h - textHeightPx(visible_title, item_px, title_font)) / 2u
                        : 0u));
    sink.text(static_cast<uint16_t>(block.x + text_pad_x), text_y, visible_title, item_px, black,
              title_font, preferredAsciiAAMode(visible_title, title_font, item_px));
  }

  if (model.visible_event_count == 0 && model.no_time_label.length() > 0) {
    const uint8_t empty_px = zh_ui ? static_cast<uint8_t>(18) : static_cast<uint8_t>(12);
    const TextFont empty_font = preferredTextFont(model.no_time_label, item_font, empty_px);
    const uint16_t empty_w = textWidthPx(model.no_time_label, empty_px, empty_font);
    const uint16_t empty_x = static_cast<uint16_t>(
        layout.schedule_inner.x +
        ((layout.schedule_inner.w > empty_w) ? (layout.schedule_inner.w - empty_w) / 2u : 8u));
    const uint16_t empty_y = static_cast<uint16_t>(layout.schedule_inner.y + 12);
    sink.text(empty_x, empty_y, model.no_time_label, empty_px, red, empty_font,
              preferredAsciiAAMode(model.no_time_label, empty_font, empty_px));
  }

  if (!model.time_valid || !layout.has_grid) {
    return;
  }

  for (int index = 0; index < 42; ++index) {
    const int row = index / 7;
    const int col = index % 7;
    if (row >= layout.grid_rows) {
      continue;
    }
    const DateCell &cell = model.date_cells[index];
    if (!cell.in_current) {
      continue;
    }
    const uint16_t cell_x = static_cast<uint16_t>(layout.grid.x + col * layout.cell_w);
    const uint16_t cell_y = static_cast<uint16_t>(layout.grid.y + row * layout.cell_h);

    const String label = String(cell.day);
    const uint16_t text_w = textWidthPx(label, day_px);
    const uint16_t text_h = textHeightPx(label, day_px);
    const uint16_t text_x = static_cast<uint16_t>(
        cell_x + ((layout.cell_w > text_w) ? ((layout.cell_w - text_w) / 2u) : 0u));
    const uint16_t text_y = static_cast<uint16_t>(cell_y + layout.cell_pad_y + 10u);
    const uint16_t text_cx = static_cast<uint16_t>(text_x + (text_w / 2u));
    const uint16_t text_cy = static_cast<uint16_t>(text_y + (text_h / 2u));
    if (cell.is_today) {
      uint16_t radius = (layout.mode == LayoutMode::LandscapeSplit) ? 16u : 14u;
      const uint16_t content_h = static_cast<uint16_t>((layout.cell_h > 10u) ? (layout.cell_h - 10u)
                                                                              : layout.cell_h);
      const uint16_t max_radius =
          (layout.cell_w < content_h ? layout.cell_w : content_h) > 10u
              ? static_cast<uint16_t>((layout.cell_w < content_h ? layout.cell_w : content_h) /
                                      2u - 2u)
              : 0u;
      if (radius > max_radius) {
        radius = max_radius;
      }
      emitFilledCircle(sink, text_cx, text_cy, radius, red);
    }
    sink.text(text_x, text_y, label, day_px, cell.text_color, TextFont::AsciiSmooth,
              preferredAsciiAAMode(label, TextFont::AsciiSmooth, day_px));

    const DaySummary &summary = model.day_summaries[index];
    if (summary.item_count == 0u) {
      continue;
    }
    const uint8_t visible_limit = (layout.grid_rows >= 6u) ? 2u : 3u;
    const uint8_t shown_count = std::min(summary.item_count, visible_limit);
    const uint8_t summary_px = zh_ui ? static_cast<uint8_t>(10) : static_cast<uint8_t>(9);
    const uint16_t summary_row_h = static_cast<uint16_t>(summary_px + 3u);
    uint16_t summary_y = static_cast<uint16_t>(text_y + text_h + 4u);
    const uint16_t summary_x = static_cast<uint16_t>(cell_x + 7u);
    const uint16_t chip_size = (layout.mode == LayoutMode::LandscapeSplit) ? 5u : 4u;
    const uint16_t text_offset_x = static_cast<uint16_t>(chip_size + 4u);
    for (uint8_t item_index = 0; item_index < shown_count; ++item_index) {
      const DaySummary::Item &item = summary.items[item_index];
      emitFilledRoundedRect(
          sink,
          makeRect(summary_x,
                   static_cast<uint16_t>(summary_y + ((summary_row_h > chip_size) ? (summary_row_h - chip_size) / 2u : 0u)),
                   chip_size, chip_size),
          1u, item.color_nibble);
      const String summary_label(item.label);
      sink.text(static_cast<uint16_t>(summary_x + text_offset_x), summary_y, summary_label, summary_px,
                black, zh_ui ? TextFont::CjkAuto : TextFont::AsciiSmooth,
                preferredAsciiAAMode(summary_label,
                                     zh_ui ? TextFont::CjkAuto : TextFont::AsciiSmooth, summary_px));
      summary_y = static_cast<uint16_t>(summary_y + summary_row_h);
    }
    const uint8_t hidden_total =
        static_cast<uint8_t>(summary.hidden_count + ((summary.item_count > shown_count)
                                                         ? (summary.item_count - shown_count)
                                                         : 0u));
    if (hidden_total > 0u) {
      sink.text(summary_x, summary_y, "+" + String(hidden_total), summary_px, black,
                TextFont::AsciiSmooth,
                preferredAsciiAAMode("+" + String(hidden_total), TextFont::AsciiSmooth, summary_px));
    }
  }
}

}  // namespace calendar
