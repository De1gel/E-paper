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
constexpr uint16_t kHeaderStatusIconSize = 16u;
constexpr uint16_t kHeaderBatteryIconW = 22u;
constexpr uint16_t kHeaderBatteryIconH = 12u;
constexpr uint16_t kHeaderStatusIconGap = 5u;
constexpr uint16_t kHeaderMetaBlockLandscapeW = 168u;
constexpr uint16_t kHeaderMetaBlockPortraitW = 136u;
constexpr bool kShowAATestPanel = false;
constexpr uint16_t kScheduleStartMinute = 8u * 60u;
constexpr uint16_t kScheduleEndMinute = 22u * 60u;
constexpr uint8_t kScheduleSlotCount = 28u;
constexpr uint16_t kMonthSummaryCircleGap = 3u;
constexpr uint16_t kMonthSummaryOffsetUp = 2u;

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
  uint16_t meta_w = 0;
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

TextFont dynamicTextFont(const String &text, TextFont cjk_font, TextFont ascii_font,
                         uint8_t pixel_height) {
  return isAsciiOnlyText(text) ? preferredTextFont(text, ascii_font, pixel_height) : cjk_font;
}

uint8_t intrinsicTextPx(TextFont font, uint8_t requested_px) {
  switch (font) {
    case TextFont::Ascii6:
      return 6u;
    case TextFont::Ascii8:
      return 8u;
    case TextFont::Ascii10:
      return 10u;
    case TextFont::AsciiSmooth:
      return 20u;
    case TextFont::Cjk10:
      return 10u;
    case TextFont::Cjk16:
      return 16u;
    case TextFont::Cjk26:
      return 26u;
    case TextFont::Cjk30:
      return 30u;
    default:
      return requested_px;
  }
}

TextFont weatherCandidateFont(const String &text, uint8_t requested_px) {
  if (isAsciiOnlyText(text)) {
    if (requested_px >= 18u) {
      return TextFont::AsciiSmooth;
    }
    if (requested_px >= 10u) {
      return TextFont::Ascii10;
    }
    if (requested_px >= 8u) {
      return TextFont::Ascii8;
    }
    return TextFont::Ascii6;
  }
  if (requested_px >= 30u) {
    return TextFont::Cjk30;
  }
  if (requested_px >= 26u) {
    return TextFont::Cjk26;
  }
  if (requested_px >= 16u) {
    return TextFont::Cjk16;
  }
  return TextFont::Cjk10;
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
String truncateTextToWidth(const String &text, uint16_t max_width_px, uint8_t pixel_height,
                           TextFont font);

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

void emitCheckerRect(SceneSink &sink, const Rect &rect, uint8_t color_a, uint8_t color_b) {
  for (uint16_t y = rect.y; y < static_cast<uint16_t>(rect.y + rect.h); ++y) {
    for (uint16_t x = rect.x; x < static_cast<uint16_t>(rect.x + rect.w); ++x) {
      sink.fillRect(makeRect(x, y, 1, 1), ((x + y) & 0x01u) ? color_a : color_b);
    }
  }
}

void emitCheckerOverlay(SceneSink &sink, const Rect &rect, uint8_t color_nibble) {
  for (uint16_t y = rect.y; y < static_cast<uint16_t>(rect.y + rect.h); ++y) {
    for (uint16_t x = rect.x; x < static_cast<uint16_t>(rect.x + rect.w); ++x) {
      if (((x + y) & 0x01u) == 0u) {
        sink.fillRect(makeRect(x, y, 1, 1), color_nibble);
      }
    }
  }
}

void emitDitheredRoundedOutline(SceneSink &sink, const Rect &rect, uint16_t radius,
                                uint8_t color_nibble, uint8_t background_nibble,
                                uint16_t thickness = 1u) {
  if (rect.w == 0 || rect.h == 0) {
    return;
  }
  emitFilledRoundedRect(sink, rect, radius, color_nibble);
  emitCheckerOverlay(sink, rect, background_nibble);
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

void emitDitheredText(SceneSink &sink, uint16_t x, uint16_t y, const String &text,
                      uint8_t pixel_height, uint8_t color_nibble, TextFont font) {
  TextCoverageMap map;
  if (!buildTextCoverageMap(text, pixel_height, font, map)) {
    return;
  }
  for (uint16_t yy = 0; yy < map.height; ++yy) {
    for (uint16_t xx = 0; xx < map.width; ++xx) {
      const uint8_t alpha = map.alpha[static_cast<uint32_t>(yy) * map.width + xx];
      if (alpha >= 2u && (((x + xx + y + yy) & 0x01u) == 0u)) {
        sink.fillRect(makeRect(static_cast<uint16_t>(x + xx), static_cast<uint16_t>(y + yy), 1, 1),
                      color_nibble);
      }
    }
  }
  freeTextCoverageMap(map);
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

  const uint16_t meta_block_w = (layout.mode == LayoutMode::LandscapeSplit)
                                    ? kHeaderMetaBlockLandscapeW
                                    : kHeaderMetaBlockPortraitW;
  metrics.meta_x =
      (metrics.card_w > meta_block_w + right_pad)
          ? static_cast<uint16_t>(metrics.card_x + metrics.card_w - meta_block_w - right_pad)
          : metrics.date_x;
  metrics.meta_w =
      static_cast<uint16_t>(metrics.card_x + metrics.card_w > metrics.meta_x + right_pad
                                ? (metrics.card_x + metrics.card_w - metrics.meta_x - right_pad)
                                : 0u);
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
  return WeatherIconKind::None;
}

uint16_t weatherHeaderIconX(const HeaderMetrics &header, const String &weather_text,
                            TextFont weather_font) {
  (void)weather_text;
  (void)weather_font;
  const uint16_t status_group_w =
      static_cast<uint16_t>(kHeaderWeatherIconSize + kHeaderStatusIconGap +
                            kHeaderStatusIconSize + kHeaderStatusIconGap +
                            kHeaderBatteryIconW + 2u);
  const uint16_t row_right = static_cast<uint16_t>(header.meta_x + header.meta_w);
  return (row_right > status_group_w)
             ? static_cast<uint16_t>(row_right - status_group_w)
             : header.meta_x;
}

uint16_t weatherHeaderIconY(const HeaderMetrics &header) {
  return static_cast<uint16_t>(header.weather_y + kHeaderWeatherIconOffsetY);
}

uint16_t batteryHeaderIconX(const HeaderMetrics &header, const String &weather_text,
                            TextFont weather_font) {
  const uint16_t weather_x = weatherHeaderIconX(header, weather_text, weather_font);
  const uint16_t wifi_x =
      static_cast<uint16_t>(weather_x + kHeaderWeatherIconSize + kHeaderStatusIconGap);
  return static_cast<uint16_t>(wifi_x + kHeaderStatusIconSize + kHeaderStatusIconGap);
}

uint16_t wifiHeaderIconX(const HeaderMetrics &header, const String &weather_text,
                         TextFont weather_font) {
  const uint16_t weather_x = weatherHeaderIconX(header, weather_text, weather_font);
  return static_cast<uint16_t>(weather_x + kHeaderWeatherIconSize + kHeaderStatusIconGap);
}

uint16_t statusHeaderIconY(const HeaderMetrics &header) {
  return static_cast<uint16_t>(weatherHeaderIconY(header) +
                               ((kHeaderWeatherIconSize > kHeaderStatusIconSize)
                                    ? (kHeaderWeatherIconSize - kHeaderStatusIconSize) / 2u
                                    : 0u));
}

struct HeaderWeatherTextLayout {
  String text;
  uint8_t px = kHeaderWeatherPx;
  TextFont font = TextFont::Auto;
  TextAAMode aa = TextAAMode::Threshold;
  uint16_t x = 0;
};

struct HeaderSensorTextParts {
  String temperature;
  String humidity;
};

HeaderSensorTextParts splitHeaderSensors(const String &text) {
  HeaderSensorTextParts parts;
  const int split = text.indexOf(' ');
  if (split < 0) {
    parts.temperature = text;
    return parts;
  }
  parts.temperature = text.substring(0, split);
  parts.humidity = text.substring(split + 1);
  parts.temperature.trim();
  parts.humidity.trim();
  return parts;
}

void emitHeaderSensors(SceneSink &sink, uint16_t x, uint16_t y, const String &text,
                       uint8_t px, TextFont font, TextAAMode aa) {
  const HeaderSensorTextParts parts = splitHeaderSensors(text);
  if (parts.humidity.length() == 0) {
    sink.text(x, y, parts.temperature, px, red, font, aa);
    return;
  }
  const String temperature_with_space = parts.temperature + " ";
  sink.text(x, y, parts.temperature, px, red, font, aa);
  sink.text(static_cast<uint16_t>(x + textWidthPx(temperature_with_space, px, font)), y,
            parts.humidity, px, blue, font, aa);
}

HeaderWeatherTextLayout layoutHeaderWeatherText(const CalendarModel &model,
                                                const HeaderMetrics &header,
                                                TextFont header_font,
                                                TextFont header_date_font) {
  HeaderWeatherTextLayout out;
  out.text = model.header_weather;
  const uint16_t date_right = static_cast<uint16_t>(
      header.date_x + textWidthPx(model.header_date, kHeaderDatePx, header_date_font));
  const uint16_t left_limit = static_cast<uint16_t>(date_right + 8u);
  const uint16_t icon_x = weatherHeaderIconX(header, model.header_weather, header_font);
  const uint16_t right_limit =
      (icon_x > kHeaderWeatherIconGap) ? static_cast<uint16_t>(icon_x - kHeaderWeatherIconGap)
                                       : icon_x;
  const uint16_t available_w =
      (right_limit > left_limit) ? static_cast<uint16_t>(right_limit - left_limit) : 0u;
  static const uint8_t kCandidatePx[] = {30u, 26u, 20u, 16u, 10u, 8u, 6u};
  for (const uint8_t px : kCandidatePx) {
    const TextFont font = weatherCandidateFont(model.header_weather, px);
    const uint8_t intrinsic_px = intrinsicTextPx(font, px);
    const uint16_t text_w = textWidthPx(model.header_weather, intrinsic_px, font);
    if (text_w <= available_w) {
      out.px = intrinsic_px;
      out.font = font;
      out.aa = preferredAsciiAAMode(model.header_weather, font, intrinsic_px);
      out.x = static_cast<uint16_t>(right_limit - text_w);
      return out;
    }
  }

  const uint8_t fallback_px = kCandidatePx[sizeof(kCandidatePx) / sizeof(kCandidatePx[0]) - 1u];
  out.font = weatherCandidateFont(model.header_weather, fallback_px);
  out.px = intrinsicTextPx(out.font, fallback_px);
  out.aa = preferredAsciiAAMode(model.header_weather, out.font, out.px);
  out.text = truncateTextToWidth(model.header_weather, available_w, out.px, out.font);
  const uint16_t text_w = textWidthPx(out.text, out.px, out.font);
  out.x = (right_limit > text_w) ? static_cast<uint16_t>(right_limit - text_w) : left_limit;
  return out;
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

void emitWifiIcon(SceneSink &sink, uint16_t x, uint16_t y, bool connected) {
  const uint8_t color = connected ? black : blue;
  const uint16_t base_y = static_cast<uint16_t>(y + kHeaderStatusIconSize - 2u);
  for (uint8_t i = 0; i < 4u; ++i) {
    const uint16_t bar_h = static_cast<uint16_t>(4u + i * 3u);
    const uint16_t bar_x = static_cast<uint16_t>(x + 1u + i * 4u);
    sink.fillRect(makeRect(bar_x, static_cast<uint16_t>(base_y - bar_h), 3u, bar_h), color);
  }
  if (!connected) {
    emitLine(sink, static_cast<int>(x + 1u), static_cast<int>(y + 1u),
             static_cast<int>(x + kHeaderStatusIconSize - 2u),
             static_cast<int>(y + kHeaderStatusIconSize - 2u), red);
  }
}

void emitBatteryIcon(SceneSink &sink, uint16_t x, uint16_t y, int16_t battery_pct) {
  const uint16_t body_y = static_cast<uint16_t>(y + 2u);
  sink.fillRect(makeRect(x, body_y, kHeaderBatteryIconW, 1u), black);
  sink.fillRect(makeRect(x, static_cast<uint16_t>(body_y + kHeaderBatteryIconH - 1u),
                         kHeaderBatteryIconW, 1u),
                black);
  sink.fillRect(makeRect(x, body_y, 1u, kHeaderBatteryIconH), black);
  sink.fillRect(makeRect(static_cast<uint16_t>(x + kHeaderBatteryIconW - 1u), body_y, 1u,
                         kHeaderBatteryIconH),
                black);
  sink.fillRect(makeRect(static_cast<uint16_t>(x + kHeaderBatteryIconW),
                         static_cast<uint16_t>(body_y + 4u), 2u, 4u),
                black);

  if (battery_pct < 0) {
    emitLine(sink, static_cast<int>(x + 3u), static_cast<int>(body_y + kHeaderBatteryIconH - 3u),
             static_cast<int>(x + kHeaderBatteryIconW - 4u), static_cast<int>(body_y + 2u), blue);
    return;
  }

  const uint16_t pct = static_cast<uint16_t>(std::min<int16_t>(100, battery_pct));
  const uint16_t fill_w = static_cast<uint16_t>((kHeaderBatteryIconW - 4u) * pct / 100u);
  if (fill_w > 0u) {
    const uint8_t fill_color = (pct <= 20u) ? red : green;
    sink.fillRect(makeRect(static_cast<uint16_t>(x + 2u), static_cast<uint16_t>(body_y + 2u),
                           fill_w, static_cast<uint16_t>(kHeaderBatteryIconH - 4u)),
                  fill_color);
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
  const uint8_t header_weather_px = kHeaderWeatherPx;
  const TextFont header_font =
      dynamicTextFont(model.header_weather, TextFont::Cjk30, TextFont::Auto, header_weather_px);
  const TextFont header_date_font =
      preferredTextFont(model.header_date, TextFont::Auto, kHeaderDatePx);
  const HeaderMetrics header = computeHeaderMetrics(layout, model, header_date_font);
  const HeaderWeatherTextLayout weather_text =
      layoutHeaderWeatherText(model, header, header_font, header_date_font);
  const uint16_t icon_x = weatherHeaderIconX(header, model.header_weather, weather_text.font);
  const uint16_t icon_y = weatherHeaderIconY(header);
  const uint16_t wifi_x = wifiHeaderIconX(header, model.header_weather, weather_text.font);
  const uint16_t battery_x = batteryHeaderIconX(header, model.header_weather, weather_text.font);
  const uint16_t status_y = statusHeaderIconY(header);
  sink.text(weather_text.x, header.weather_y, weather_text.text, weather_text.px, green,
            weather_text.font, weather_text.aa);
  emitWifiIcon(sink, wifi_x, status_y, model.header_wifi_connected);
  emitBatteryIcon(sink, battery_x, status_y, model.header_battery_pct);
  emitWeatherIcon(sink, icon_x, icon_y, kHeaderWeatherIconSize, model.header_weather_code);
}

void emitCalendarScene(const CalendarModel &model, const CalendarLayout &layout, SceneSink &sink) {
  const bool zh_ui = (model.ui_language == "zh");
  const uint8_t weekday_px =
      zh_ui ? kZhWeekdayPx
            : static_cast<uint8_t>(asciiPixelHeight(layout.weekday_scale) +
                                   (layout.mode == LayoutMode::LandscapeSplit ? 2 : 1));
  const uint8_t header_date_px = kHeaderDatePx;
  const uint8_t header_time_px = kHeaderTimePx;
  const uint8_t header_weather_px = kHeaderWeatherPx;
  const uint8_t header_sensors_px = kHeaderSensorsPx;
  const uint8_t day_px =
      (layout.mode == LayoutMode::LandscapeSplit) ? static_cast<uint8_t>(16)
                                                  : static_cast<uint8_t>(14);
  const TextFont weekday_font = zh_ui ? TextFont::Cjk26 : TextFont::Auto;
  const TextFont header_font =
      dynamicTextFont(model.header_weather, TextFont::Cjk30, TextFont::Auto, header_weather_px);
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
  const uint16_t sensors_w = textWidthPx(model.header_sensors, header_sensors_px, header_sensors_font);
  const uint16_t sensors_x =
      (header.meta_w > sensors_w)
          ? static_cast<uint16_t>(header.meta_x + header.meta_w - sensors_w)
          : header.meta_x;
  emitHeaderSensors(sink, sensors_x, header.sensors_y, model.header_sensors, header_sensors_px,
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
  const uint16_t axis_label_h = textHeightPx("22", 10, TextFont::AsciiSmooth);

  for (uint8_t slot = 0; slot <= kScheduleSlotCount; ++slot) {
    const uint16_t minute_value = static_cast<uint16_t>(kScheduleStartMinute + slot * 30u);
    const uint16_t y = timelineYForMinute(layout, minute_value);
    const bool is_hour_line = ((slot % 2u) == 0u);
    if (is_hour_line) {
      const String hour_label =
          String((minute_value / 60u < 10u) ? "0" : "") + String(minute_value / 60u);
      const uint16_t label_y =
          (y > (axis_label_h / 2u)) ? static_cast<uint16_t>(y - axis_label_h / 2u) : 0u;
      sink.text(axis_x, label_y, hour_label, 10, black, TextFont::AsciiSmooth,
                preferredAsciiAAMode(hour_label, TextFont::AsciiSmooth, 10));
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
    const bool event_elapsed = model.time_valid && end_minute <= model.current_minute_of_day;
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
    if (event_elapsed) {
      emitDitheredRoundedOutline(sink, block, 4u, black, white, 1u);
    } else {
      emitRoundedOutline(sink, block, 4u, black, white, 1u);
    }
    const uint8_t accent_color = (event.color_nibble == white) ? blue : event.color_nibble;
    const uint16_t accent_h = (block.h > 6u) ? static_cast<uint16_t>(block.h - 4u) : block.h;
    const Rect accent = makeRect(static_cast<uint16_t>(block.x + 2u),
                                 static_cast<uint16_t>(block.y + 2u), 4u, accent_h);
    if (event_elapsed) {
      emitCheckerRect(sink, accent, black, white);
    } else {
      sink.fillRect(accent, accent_color);
    }

    const uint16_t text_pad_x = 9;
    const uint16_t text_space =
        (block.w > text_pad_x * 2) ? static_cast<uint16_t>(block.w - text_pad_x * 2) : 0u;
    if (text_space == 0 || block.h < 8) {
      continue;
    }
    const bool title_ascii = isAsciiOnlyText(event.title);
    const uint8_t title_px = title_ascii ? static_cast<uint8_t>(8) : static_cast<uint8_t>(10);
    const TextFont title_font = title_ascii ? TextFont::Ascii8 : TextFont::Cjk10;
    const String visible_title = truncateTextToWidth(event.title, text_space, title_px, title_font);
    const uint16_t text_y = static_cast<uint16_t>(
        block.y + ((block.h > textHeightPx(visible_title, title_px, title_font))
                        ? (block.h - textHeightPx(visible_title, title_px, title_font)) / 2u
                        : 0u));
    if (event_elapsed) {
      emitDitheredText(sink, static_cast<uint16_t>(block.x + text_pad_x), text_y, visible_title,
                       title_px, black, title_font);
    } else {
      sink.text(static_cast<uint16_t>(block.x + text_pad_x), text_y, visible_title, title_px, black,
                title_font, preferredAsciiAAMode(visible_title, title_font, title_px));
    }
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
    uint16_t today_radius = (layout.mode == LayoutMode::LandscapeSplit) ? 16u : 14u;
    const uint16_t content_h = static_cast<uint16_t>((layout.cell_h > 10u) ? (layout.cell_h - 10u)
                                                                            : layout.cell_h);
    const uint16_t max_radius =
        (layout.cell_w < content_h ? layout.cell_w : content_h) > 10u
            ? static_cast<uint16_t>((layout.cell_w < content_h ? layout.cell_w : content_h) /
                                    2u - 2u)
            : 0u;
    if (today_radius > max_radius) {
      today_radius = max_radius;
    }
    if (cell.is_today) {
      emitFilledCircle(sink, text_cx, text_cy, today_radius, red);
    }
    sink.text(text_x, text_y, label, day_px, cell.text_color, TextFont::AsciiSmooth,
              preferredAsciiAAMode(label, TextFont::AsciiSmooth, day_px));

    const DaySummary &summary = model.day_summaries[index];
    if (summary.item_count == 0u) {
      continue;
    }
    const uint8_t visible_limit = (layout.grid_rows >= 6u) ? 2u : 3u;
    const uint8_t shown_count = std::min(summary.item_count, visible_limit);
    const uint16_t summary_y_base_raw = static_cast<uint16_t>(text_cy + today_radius +
                                                              kMonthSummaryCircleGap);
    const uint16_t summary_y_base =
        (summary_y_base_raw > kMonthSummaryOffsetUp)
            ? static_cast<uint16_t>(summary_y_base_raw - kMonthSummaryOffsetUp)
            : summary_y_base_raw;
    uint16_t summary_y = summary_y_base;
    const uint16_t summary_x = static_cast<uint16_t>(cell_x + 7u);
    const uint16_t chip_size = (layout.mode == LayoutMode::LandscapeSplit) ? 5u : 4u;
    const uint16_t text_offset_x = static_cast<uint16_t>(chip_size + 4u);
    const uint16_t summary_row_h = 13u;
    for (uint8_t item_index = 0; item_index < shown_count; ++item_index) {
      const DaySummary::Item &item = summary.items[item_index];
      const String summary_label(item.label);
      const bool summary_ascii = isAsciiOnlyText(summary_label);
      const uint8_t summary_px = summary_ascii ? static_cast<uint8_t>(6) : static_cast<uint8_t>(10);
      const TextFont summary_font = summary_ascii ? TextFont::Ascii6 : TextFont::Cjk10;
      const uint16_t summary_text_h = textHeightPx(summary_label, summary_px, summary_font);
      const uint16_t summary_text_y =
          static_cast<uint16_t>(summary_y + ((summary_row_h > summary_text_h)
                                                 ? (summary_row_h - summary_text_h) / 2u
                                                 : 0u));
      emitFilledRoundedRect(
          sink,
          makeRect(summary_x,
                   static_cast<uint16_t>(
                       summary_y + ((summary_row_h > chip_size)
                                        ? (summary_row_h - chip_size) / 2u
                                        : 0u)),
                   chip_size, chip_size),
          1u, item.color_nibble);
      sink.text(static_cast<uint16_t>(summary_x + text_offset_x), summary_text_y, summary_label,
                summary_px, black, summary_font,
                preferredAsciiAAMode(summary_label, summary_font, summary_px));
      summary_y = static_cast<uint16_t>(summary_y + summary_row_h);
    }
  }
}

}  // namespace calendar
