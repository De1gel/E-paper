#include "calendar/CalendarScene.h"

#include <algorithm>

#include "Display_EPD_W21.h"
#include "calendar/CalendarLogic.h"
#include "calendar/CalendarText.h"

namespace calendar {
namespace {

constexpr uint8_t kAsciiBasePx = 7;
constexpr uint8_t kZhWeekdayPx = 26;
constexpr uint8_t kHeaderDatePx = 24;
constexpr uint8_t kHeaderWeatherPx = 30;
constexpr uint8_t kHeaderSensorsPx = 20;
constexpr uint8_t kHeaderLocationSlotPx = 30;
constexpr uint8_t kHeaderLocationVisualOffsetY = 2;
constexpr uint16_t kHeaderWeatherIconSize = 24u;
constexpr uint16_t kHeaderWeatherIconOffsetY = 2u;
constexpr uint16_t kHeaderStatusIconSize = 16u;
constexpr uint16_t kHeaderBatteryIconW = 22u;
constexpr uint16_t kHeaderBatteryIconH = 12u;
constexpr uint16_t kHeaderStatusIconGap = 15u;
constexpr uint16_t kHeaderMetaBlockLandscapeW = 168u;
constexpr uint16_t kHeaderMetaBlockPortraitW = 136u;
constexpr bool kShowAATestPanel = false;
constexpr uint16_t kScheduleStartMinute = 8u * 60u;
constexpr uint16_t kScheduleSplitMinute = 14u * 60u;
constexpr uint16_t kScheduleEndMinute = 22u * 60u;
constexpr uint8_t kSchedulePeriodHeaderH = 22u;
constexpr uint16_t kMonthSummaryCircleGap = 3u;
constexpr uint16_t kMonthSummaryOffsetUp = 3u;

struct HeaderMetrics {
  uint16_t card_x = 0;
  uint16_t card_y = 0;
  uint16_t card_w = 0;
  uint16_t card_h = 0;
  uint16_t date_x = 0;
  uint16_t date_y = 0;
  uint16_t location_x = 0;
  uint16_t location_y = 0;
  uint16_t left_x = 0;
  uint16_t left_w = 0;
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

uint8_t utf8CharByteLen(const String &text, size_t byte_index) {
  if (byte_index >= text.length()) {
    return 0u;
  }
  const uint8_t first = static_cast<uint8_t>(text[byte_index]);
  if ((first & 0x80u) == 0u) {
    return 1u;
  }
  if ((first & 0xE0u) == 0xC0u) {
    return (byte_index + 1u < text.length()) ? 2u : 1u;
  }
  if ((first & 0xF0u) == 0xE0u) {
    return (byte_index + 2u < text.length()) ? 3u : 1u;
  }
  if ((first & 0xF8u) == 0xF0u) {
    return (byte_index + 3u < text.length()) ? 4u : 1u;
  }
  return 1u;
}

bool isAsciiAt(const String &text, size_t byte_index) {
  return byte_index < text.length() && static_cast<uint8_t>(text[byte_index]) < 0x80u;
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
    case TextFont::AsciiSmooth14:
      return 14u;
    case TextFont::AsciiSmooth16:
      return 16u;
    case TextFont::AsciiSmooth:
      return 20u;
    case TextFont::Digit10:
      return 10u;
    case TextFont::Digit14:
      return 14u;
    case TextFont::Digit16:
      return 16u;
    case TextFont::Digit26:
      return 26u;
    case TextFont::Digit30:
      return 30u;
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
    if (requested_px >= 16u) {
      return TextFont::AsciiSmooth16;
    }
    if (requested_px >= 14u) {
      return TextFont::AsciiSmooth14;
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
  (void)pixel_height;
  if (font == TextFont::AsciiSmooth || font == TextFont::AsciiSmooth14) {
    return TextAAMode::Burkes;
  }
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

constexpr uint8_t kScheduleTitleAsciiPx = 16u;
constexpr uint8_t kScheduleTitleCjkPx = 16u;
constexpr uint8_t kScheduleCompactAsciiPx = 10u;
constexpr uint8_t kScheduleCompactCjkPx = 12u;
constexpr uint8_t kScheduleMaxTextLines = 8u;
constexpr uint8_t kScheduleLineGap = 2u;

struct ScheduleTextStyle {
  uint8_t ascii_px;
  uint8_t cjk_px;
  TextFont ascii_font;
  TextFont cjk_font;
};

constexpr ScheduleTextStyle kScheduleLargeTextStyle = {
    kScheduleTitleAsciiPx, kScheduleTitleCjkPx, TextFont::AsciiSmooth16, TextFont::Cjk16};
constexpr ScheduleTextStyle kScheduleCompactTextStyle = {
    kScheduleCompactAsciiPx, kScheduleCompactCjkPx, TextFont::Ascii10, TextFont::Cjk10};

struct ScheduleTitleLayout {
  String lines[kScheduleMaxTextLines];
  uint8_t line_count = 0u;
  uint16_t line_height = 0u;
  uint16_t total_height = 0u;
  const ScheduleTextStyle *style = &kScheduleLargeTextStyle;
};

uint16_t scheduleTitleRunWidth(const String &run, bool ascii_run,
                               const ScheduleTextStyle &style) {
  return textWidthPx(run, ascii_run ? style.ascii_px : style.cjk_px,
                     ascii_run ? style.ascii_font : style.cjk_font);
}

uint16_t scheduleTitleWidth(const String &text, const ScheduleTextStyle &style) {
  uint16_t total = 0;
  size_t byte_index = 0;
  while (byte_index < text.length()) {
    const bool ascii_run = isAsciiAt(text, byte_index);
    const size_t run_start = byte_index;
    while (byte_index < text.length() && isAsciiAt(text, byte_index) == ascii_run) {
      const uint8_t char_len = utf8CharByteLen(text, byte_index);
      byte_index += (char_len > 0u) ? char_len : 1u;
    }
    total = static_cast<uint16_t>(
        total + scheduleTitleRunWidth(text.substring(run_start, byte_index), ascii_run, style));
  }
  return total;
}

uint16_t scheduleTitleHeight(const String &text, const ScheduleTextStyle &style) {
  uint16_t max_height = 0u;
  size_t byte_index = 0u;
  while (byte_index < text.length()) {
    const bool ascii_run = isAsciiAt(text, byte_index);
    const size_t run_start = byte_index;
    while (byte_index < text.length() && isAsciiAt(text, byte_index) == ascii_run) {
      const uint8_t char_len = utf8CharByteLen(text, byte_index);
      byte_index += (char_len > 0u) ? char_len : 1u;
    }
    const uint16_t run_height =
        textHeightPx(text.substring(run_start, byte_index),
                     ascii_run ? style.ascii_px : style.cjk_px,
                     ascii_run ? style.ascii_font : style.cjk_font);
    max_height = std::max<uint16_t>(max_height, run_height);
  }
  return max_height;
}

String truncateScheduleTitleToWidth(const String &text, uint16_t max_width,
                                    const ScheduleTextStyle &style) {
  if (scheduleTitleWidth(text, style) <= max_width) {
    return text;
  }
  String out;
  out.reserve(text.length());
  size_t byte_index = 0;
  while (byte_index < text.length()) {
    const uint8_t char_len = utf8CharByteLen(text, byte_index);
    const size_t next = byte_index + ((char_len > 0u) ? char_len : 1u);
    String candidate = out + text.substring(byte_index, next);
    if (scheduleTitleWidth(candidate, style) > max_width) {
      break;
    }
    out = candidate;
    byte_index = next;
  }
  return out;
}

bool isScheduleBreakSpace(const String &text, size_t byte_index) {
  return byte_index < text.length() && text[byte_index] == ' ';
}

bool wrapScheduleTitle(const String &text, uint16_t max_width, uint8_t max_lines,
                       const ScheduleTextStyle &style, bool truncate_overflow,
                       ScheduleTitleLayout &layout) {
  layout = ScheduleTitleLayout{};
  layout.style = &style;
  layout.line_height = scheduleTitleHeight(text, style);
  if (text.length() == 0u || max_width == 0u || max_lines == 0u) {
    return text.length() == 0u;
  }

  size_t line_start = 0u;
  while (line_start < text.length() && layout.line_count < max_lines) {
    while (isScheduleBreakSpace(text, line_start)) {
      ++line_start;
    }
    if (line_start >= text.length()) {
      break;
    }

    if (layout.line_count + 1u == max_lines && truncate_overflow) {
      layout.lines[layout.line_count++] =
          truncateScheduleTitleToWidth(text.substring(line_start), max_width, style);
      line_start = text.length();
      break;
    }

    size_t byte_index = line_start;
    size_t fit_end = line_start;
    size_t break_end = line_start;
    while (byte_index < text.length()) {
      const uint8_t char_len = utf8CharByteLen(text, byte_index);
      const size_t next = byte_index + ((char_len > 0u) ? char_len : 1u);
      const String candidate = text.substring(line_start, next);
      if (scheduleTitleWidth(candidate, style) > max_width) {
        break;
      }
      fit_end = next;
      if (isScheduleBreakSpace(text, byte_index)) {
        break_end = byte_index;
      }
      byte_index = next;
    }

    if (fit_end == line_start) {
      const uint8_t char_len = utf8CharByteLen(text, line_start);
      fit_end = line_start + ((char_len > 0u) ? char_len : 1u);
    }
    const bool has_more = fit_end < text.length();
    const size_t line_end = (has_more && break_end > line_start) ? break_end : fit_end;
    layout.lines[layout.line_count++] = text.substring(line_start, line_end);
    line_start = (line_end < fit_end) ? line_end + 1u : fit_end;
  }

  while (isScheduleBreakSpace(text, line_start)) {
    ++line_start;
  }
  const bool complete = line_start >= text.length();
  layout.total_height = static_cast<uint16_t>(
      layout.line_count * layout.line_height +
      ((layout.line_count > 0u) ? (layout.line_count - 1u) * kScheduleLineGap : 0u));
  return complete;
}

uint8_t scheduleLineCapacity(uint16_t max_height, uint16_t line_height) {
  if (line_height == 0u || max_height < line_height) {
    return 0u;
  }
  const uint16_t count = static_cast<uint16_t>(
      (max_height + kScheduleLineGap) / (line_height + kScheduleLineGap));
  return static_cast<uint8_t>(std::min<uint16_t>(count, kScheduleMaxTextLines));
}

ScheduleTitleLayout layoutScheduleTitle(const String &text, uint16_t max_width,
                                        uint16_t max_height) {
  ScheduleTitleLayout layout;
  const uint8_t large_lines = scheduleLineCapacity(
      max_height, scheduleTitleHeight(text, kScheduleLargeTextStyle));
  if (wrapScheduleTitle(text, max_width, large_lines, kScheduleLargeTextStyle, false,
                        layout)) {
    return layout;
  }

  const uint8_t compact_lines = scheduleLineCapacity(
      max_height, scheduleTitleHeight(text, kScheduleCompactTextStyle));
  if (wrapScheduleTitle(text, max_width, compact_lines, kScheduleCompactTextStyle, false,
                        layout)) {
    return layout;
  }

  wrapScheduleTitle(text, max_width, compact_lines, kScheduleCompactTextStyle, true, layout);
  return layout;
}

void emitScheduleTitleText(SceneSink &sink, uint16_t x, uint16_t y, const String &text,
                           const ScheduleTextStyle &style, uint8_t color_nibble,
                           bool dithered) {
  const uint16_t line_h = scheduleTitleHeight(text, style);
  uint16_t pen_x = x;
  size_t byte_index = 0;
  while (byte_index < text.length()) {
    const bool ascii_run = isAsciiAt(text, byte_index);
    const size_t run_start = byte_index;
    while (byte_index < text.length() && isAsciiAt(text, byte_index) == ascii_run) {
      const uint8_t char_len = utf8CharByteLen(text, byte_index);
      byte_index += (char_len > 0u) ? char_len : 1u;
    }

    const String run = text.substring(run_start, byte_index);
    const uint8_t px = ascii_run ? style.ascii_px : style.cjk_px;
    const TextFont font = ascii_run ? style.ascii_font : style.cjk_font;
    const uint16_t run_y =
        static_cast<uint16_t>(y + ((line_h > px) ? ((line_h - px) / 2u) : 0u));
    if (dithered) {
      emitDitheredText(sink, pen_x, run_y, run, px, color_nibble, font);
    } else {
      sink.text(pen_x, run_y, run, px, color_nibble, font,
                preferredAsciiAAMode(run, font, px));
    }
    pen_x =
        static_cast<uint16_t>(pen_x + scheduleTitleRunWidth(run, ascii_run, style));
  }
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
  metrics.left_x = static_cast<uint16_t>(metrics.card_x + left_pad);
  metrics.left_w =
      (metrics.meta_x > metrics.left_x + right_pad)
          ? static_cast<uint16_t>(metrics.meta_x - metrics.left_x - right_pad)
          : static_cast<uint16_t>(metrics.card_w > left_pad + right_pad
                                      ? metrics.card_w - left_pad - right_pad
                                      : 0u);
  const uint16_t date_w = textWidthPx(model.header_date, kHeaderDatePx, header_date_font);
  const uint16_t date_h = textHeightPx(model.header_date, kHeaderDatePx, header_date_font);
  metrics.date_x = static_cast<uint16_t>(
      metrics.left_x + ((metrics.left_w > date_w) ? (metrics.left_w - date_w) / 2u : 0u));
  constexpr uint16_t kDateLocationGap = 5u;
  const uint16_t date_location_h =
      static_cast<uint16_t>(date_h + kDateLocationGap + kHeaderLocationSlotPx);
  metrics.date_y = static_cast<uint16_t>(
      metrics.card_y +
      ((metrics.card_h > date_location_h) ? (metrics.card_h - date_location_h) / 2u : 0u));
  metrics.location_y = static_cast<uint16_t>(metrics.date_y + date_h + kDateLocationGap);
  metrics.weather_y = static_cast<uint16_t>(metrics.card_y + top_pad);
  metrics.sensors_y = static_cast<uint16_t>(metrics.weather_y + kHeaderWeatherIconSize + 10u);
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

uint16_t weatherHeaderIconX(const HeaderMetrics &header) {
  const uint16_t status_group_w =
      static_cast<uint16_t>(kHeaderWeatherIconSize + kHeaderStatusIconGap +
                            kHeaderStatusIconSize + kHeaderStatusIconGap +
                            kHeaderBatteryIconW + 2u);
  return (header.meta_w > status_group_w)
             ? static_cast<uint16_t>(header.meta_x + (header.meta_w - status_group_w) / 2u)
             : header.meta_x;
}

uint16_t weatherHeaderIconY(const HeaderMetrics &header) {
  return static_cast<uint16_t>(header.weather_y + kHeaderWeatherIconOffsetY);
}

uint16_t batteryHeaderIconX(const HeaderMetrics &header) {
  const uint16_t weather_x = weatherHeaderIconX(header);
  const uint16_t wifi_x =
      static_cast<uint16_t>(weather_x + kHeaderWeatherIconSize + kHeaderStatusIconGap);
  return static_cast<uint16_t>(wifi_x + kHeaderStatusIconSize + kHeaderStatusIconGap);
}

uint16_t wifiHeaderIconX(const HeaderMetrics &header) {
  const uint16_t weather_x = weatherHeaderIconX(header);
  return static_cast<uint16_t>(weather_x + kHeaderWeatherIconSize + kHeaderStatusIconGap);
}

uint16_t statusHeaderIconY(const HeaderMetrics &header) {
  return static_cast<uint16_t>(weatherHeaderIconY(header) +
                               ((kHeaderWeatherIconSize > kHeaderStatusIconSize)
                                    ? (kHeaderWeatherIconSize - kHeaderStatusIconSize) / 2u
                                    : 0u));
}

uint16_t headerLocationTextY(const HeaderMetrics &header, const String &text, uint8_t px,
                             TextFont font) {
  const uint16_t text_h = textHeightPx(text, px, font);
  const uint16_t centered_offset =
      (kHeaderLocationSlotPx > text_h)
          ? static_cast<uint16_t>((kHeaderLocationSlotPx - text_h) / 2u)
          : 0u;
  const uint8_t visual_offset = isAsciiOnlyText(text) ? kHeaderLocationVisualOffsetY : 0u;
  return static_cast<uint16_t>(header.location_y + centered_offset + visual_offset);
}

struct HeaderLocationTextLayout {
  String text;
  uint8_t px = 30u;
  TextFont font = TextFont::Auto;
  TextAAMode aa = TextAAMode::Threshold;
  uint16_t x = 0;
  uint16_t y = 0;
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

HeaderLocationTextLayout layoutHeaderLocationText(const CalendarModel &model,
                                                  const HeaderMetrics &header) {
  HeaderLocationTextLayout out;
  out.text = model.header_weather;
  out.y = header.location_y;
  const uint16_t available_w = header.left_w;
  if (isAsciiOnlyText(model.header_weather)) {
    struct AsciiLocationCandidate {
      TextFont font;
      uint8_t px;
    };
    static const AsciiLocationCandidate kAsciiCandidates[] = {
        {TextFont::AsciiSmooth, 20u},
        {TextFont::AsciiSmooth16, 16u},
        {TextFont::AsciiSmooth14, 14u},
    };
    for (const auto &candidate : kAsciiCandidates) {
      const uint8_t intrinsic_px = intrinsicTextPx(candidate.font, candidate.px);
      const uint16_t text_w = textWidthPx(out.text, intrinsic_px, candidate.font);
      if (text_w <= available_w) {
        out.font = candidate.font;
        out.px = intrinsic_px;
        out.aa = preferredAsciiAAMode(model.header_weather, out.font, out.px);
        out.x = static_cast<uint16_t>(
            header.left_x + ((available_w > text_w) ? (available_w - text_w) / 2u : 0u));
        out.y = headerLocationTextY(header, out.text, out.px, out.font);
        return out;
      }
    }

    const AsciiLocationCandidate fallback =
        kAsciiCandidates[sizeof(kAsciiCandidates) / sizeof(kAsciiCandidates[0]) - 1u];
    out.font = fallback.font;
    out.px = intrinsicTextPx(out.font, fallback.px);
    out.aa = preferredAsciiAAMode(model.header_weather, out.font, out.px);
    out.text = truncateTextToWidth(model.header_weather, available_w, out.px, out.font);
    const uint16_t text_w = textWidthPx(out.text, out.px, out.font);
    out.x = static_cast<uint16_t>(
        header.left_x + ((available_w > text_w) ? (available_w - text_w) / 2u : 0u));
    out.y = headerLocationTextY(header, out.text, out.px, out.font);
    return out;
  }
  static const uint8_t kCandidatePx[] = {30u, 26u, 20u, 16u, 10u, 8u, 6u};
  for (const uint8_t px : kCandidatePx) {
    const TextFont font = weatherCandidateFont(model.header_weather, px);
    const uint8_t intrinsic_px = intrinsicTextPx(font, px);
    const uint16_t text_w = textWidthPx(model.header_weather, intrinsic_px, font);
    if (text_w <= available_w) {
      out.px = intrinsic_px;
      out.font = font;
      out.aa = preferredAsciiAAMode(model.header_weather, font, intrinsic_px);
      out.x = static_cast<uint16_t>(
          header.left_x + ((available_w > text_w) ? (available_w - text_w) / 2u : 0u));
      out.y = headerLocationTextY(header, out.text, out.px, out.font);
      return out;
    }
  }

  const uint8_t fallback_px = kCandidatePx[sizeof(kCandidatePx) / sizeof(kCandidatePx[0]) - 1u];
  out.font = weatherCandidateFont(model.header_weather, fallback_px);
  out.px = intrinsicTextPx(out.font, fallback_px);
  out.aa = preferredAsciiAAMode(model.header_weather, out.font, out.px);
  out.text = truncateTextToWidth(model.header_weather, available_w, out.px, out.font);
  const uint16_t text_w = textWidthPx(out.text, out.px, out.font);
  out.x = static_cast<uint16_t>(
      header.left_x + ((available_w > text_w) ? (available_w - text_w) / 2u : 0u));
  out.y = headerLocationTextY(header, out.text, out.px, out.font);
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

struct SchedulePeriodGeometry {
  Rect bounds;
  uint16_t start_minute = 0;
  uint16_t end_minute = 0;
  uint16_t axis_x = 0;
  uint16_t timeline_left = 0;
  uint16_t timeline_right = 0;
  uint16_t timeline_top = 0;
  uint16_t timeline_bottom = 0;
};

SchedulePeriodGeometry schedulePeriodGeometry(const CalendarLayout &layout, uint8_t period_index,
                                              bool two_columns) {
  constexpr uint16_t kOuterPad = 7u;
  constexpr uint16_t kColumnGap = 8u;
  const uint16_t column_gap = two_columns ? kColumnGap : 0u;
  const uint16_t available_w =
      (layout.schedule_inner.w > kOuterPad * 2u + column_gap)
          ? static_cast<uint16_t>(layout.schedule_inner.w - kOuterPad * 2u - column_gap)
          : 0u;
  const uint16_t left_w = two_columns ? static_cast<uint16_t>(available_w / 2u) : available_w;
  const uint16_t right_w = two_columns ? static_cast<uint16_t>(available_w - left_w) : 0u;
  const uint16_t left_x = static_cast<uint16_t>(layout.schedule_inner.x + kOuterPad);
  const uint16_t right_x = static_cast<uint16_t>(left_x + left_w + kColumnGap);

  SchedulePeriodGeometry period;
  period.bounds =
      (period_index == 0u) ? makeRect(left_x, layout.list_top, left_w,
                                      static_cast<uint16_t>(layout.list_bottom - layout.list_top))
                           : makeRect(right_x, layout.list_top, right_w,
                                      static_cast<uint16_t>(layout.list_bottom - layout.list_top));
  period.start_minute =
      (!two_columns || period_index == 0u) ? kScheduleStartMinute : kScheduleSplitMinute;
  period.end_minute = !two_columns ? kScheduleEndMinute
                                   : ((period_index == 0u) ? kScheduleSplitMinute
                                                           : kScheduleEndMinute);
  period.axis_x = static_cast<uint16_t>(period.bounds.x + 1u);
  const uint16_t axis_label_w = textWidthPx("22", 10u, TextFont::Digit10);
  period.timeline_left = static_cast<uint16_t>(period.axis_x + axis_label_w + 5u);
  period.timeline_right =
      static_cast<uint16_t>(period.bounds.x + period.bounds.w - 2u);
  period.timeline_top = static_cast<uint16_t>(
      layout.list_top + (two_columns ? kSchedulePeriodHeaderH : 0u));
  period.timeline_bottom = layout.list_bottom;
  return period;
}

uint16_t timelineYForMinute(const SchedulePeriodGeometry &period, uint16_t minute_value) {
  const uint16_t height =
      (period.timeline_bottom > period.timeline_top)
          ? static_cast<uint16_t>(period.timeline_bottom - period.timeline_top)
          : 0u;
  return static_cast<uint16_t>(
      period.timeline_top +
      timelineOffsetForMinute(minute_value, period.start_minute, period.end_minute, height));
}

void emitScheduleContinuation(const Rect &block, bool continues_before, bool continues_after,
                              uint8_t color_nibble, SceneSink &sink) {
  const uint16_t cx = static_cast<uint16_t>(block.x + block.w / 2u);
  if (continues_before && block.y >= 1u) {
    sink.fillRect(makeRect(static_cast<uint16_t>(cx - 1u), block.y, 3u, 1u), color_nibble);
    sink.fillRect(makeRect(static_cast<uint16_t>(cx - 2u), static_cast<uint16_t>(block.y + 1u),
                           5u, 1u), color_nibble);
    sink.fillRect(makeRect(static_cast<uint16_t>(cx - 3u), static_cast<uint16_t>(block.y + 2u),
                           7u, 1u), color_nibble);
  }
  if (continues_after && block.h >= 3u) {
    const uint16_t bottom = static_cast<uint16_t>(block.y + block.h - 1u);
    sink.fillRect(makeRect(static_cast<uint16_t>(cx - 3u), static_cast<uint16_t>(bottom - 2u),
                           7u, 1u), color_nibble);
    sink.fillRect(makeRect(static_cast<uint16_t>(cx - 2u), static_cast<uint16_t>(bottom - 1u),
                           5u, 1u), color_nibble);
    sink.fillRect(makeRect(static_cast<uint16_t>(cx - 1u), bottom, 3u, 1u), color_nibble);
  }
}

}  // namespace

void emitCalendarWeatherHeader(const CalendarModel &model, const CalendarLayout &layout, SceneSink &sink) {
  const TextFont header_date_font =
      preferredTextFont(model.header_date, TextFont::Auto, kHeaderDatePx);
  const HeaderMetrics header = computeHeaderMetrics(layout, model, header_date_font);
  const HeaderLocationTextLayout location_text = layoutHeaderLocationText(model, header);
  const uint16_t icon_x = weatherHeaderIconX(header);
  const uint16_t icon_y = weatherHeaderIconY(header);
  const uint16_t wifi_x = wifiHeaderIconX(header);
  const uint16_t battery_x = batteryHeaderIconX(header);
  const uint16_t status_y = statusHeaderIconY(header);
  sink.text(location_text.x, location_text.y, location_text.text, location_text.px, green,
            location_text.font, TextAAMode::Threshold);
  emitBatteryIcon(sink, battery_x, status_y, model.header_battery_pct);
  emitWeatherIcon(sink, icon_x, icon_y, kHeaderWeatherIconSize, model.header_weather_code);
  emitWifiIcon(sink, wifi_x, status_y, model.header_wifi_connected);
}

void emitCalendarScene(const CalendarModel &model, const CalendarLayout &layout, SceneSink &sink) {
  const bool zh_ui = (model.ui_language == "zh");
  const uint8_t weekday_px =
      zh_ui ? kZhWeekdayPx
            : static_cast<uint8_t>(asciiPixelHeight(layout.weekday_scale) +
                                   (layout.mode == LayoutMode::LandscapeSplit ? 2 : 1));
  const uint8_t header_date_px = kHeaderDatePx;
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
  const TextFont header_sensors_font =
      preferredTextFont(model.header_sensors, header_font, header_sensors_px);
  const TextAAMode header_date_aa = TextAAMode::Threshold;
  const TextAAMode header_sensors_aa = TextAAMode::Threshold;
  const HeaderMetrics header = computeHeaderMetrics(layout, model, header_date_font);
  const Rect header_card = makeRect(header.card_x, header.card_y, header.card_w, header.card_h);
  emitRoundedOutline(sink, header_card, 12u, black, white, 2u);
  sink.text(header.date_x, header.date_y, model.header_date, header_date_px, black, header_date_font,
            header_date_aa);
  emitCalendarWeatherHeader(model, layout, sink);
  const uint16_t sensors_w = textWidthPx(model.header_sensors, header_sensors_px, header_sensors_font);
  const uint16_t sensors_x =
      (header.meta_w > sensors_w)
          ? static_cast<uint16_t>(header.meta_x + (header.meta_w - sensors_w) / 2u)
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
      sink.text(label_x, layout.weekday_y, label, weekday_px, isWeekendColumn(col) ? red : black,
                label_font, preferredAsciiAAMode(label, label_font, weekday_px));
    }
  }

  if (kShowAATestPanel && layout.mode == LayoutMode::LandscapeSplit) {
    emitAATestPanel(sink, layout);
    return;
  }

  const uint8_t axis_label_px = 10u;
  const TextFont axis_label_font = TextFont::Digit10;
  const uint16_t axis_label_h = textHeightPx("22", axis_label_px, axis_label_font);
  const bool two_columns = model.schedule_two_columns;
  const uint8_t period_count = two_columns ? 2u : 1u;
  SchedulePeriodGeometry periods[2] = {
      schedulePeriodGeometry(layout, 0u, two_columns),
      schedulePeriodGeometry(layout, 1u, two_columns),
  };
  const uint16_t divider_x =
      static_cast<uint16_t>(periods[0].bounds.x + periods[0].bounds.w + 3u);
  if (two_columns && layout.list_bottom > layout.list_top) {
    sink.fillRect(makeRect(divider_x, layout.list_top, 1u,
                           static_cast<uint16_t>(layout.list_bottom - layout.list_top)),
                  black);
  }

  for (uint8_t period_index = 0; period_index < period_count; ++period_index) {
    const SchedulePeriodGeometry &period = periods[period_index];
    if (two_columns) {
      String period_label;
      if (zh_ui) {
        period_label = (period_index == 0u) ? "\xE4\xB8\x8A\xE5\x8D\x88"
                                           : "\xE4\xB8\x8B\xE5\x8D\x88";
      } else if (model.ui_language == "fr") {
        period_label = (period_index == 0u) ? "MATIN" : "APRES-MIDI";
      } else {
        period_label = (period_index == 0u) ? "AM" : "PM";
      }
      const TextFont period_font = zh_ui ? TextFont::Cjk16 : TextFont::AsciiSmooth16;
      const uint16_t period_label_w = textWidthPx(period_label, 16u, period_font);
      const uint16_t period_label_x =
          (period.bounds.w > period_label_w)
              ? static_cast<uint16_t>(period.bounds.x + (period.bounds.w - period_label_w) / 2u)
              : period.bounds.x;
      sink.text(period_label_x, layout.list_top, period_label, 16u, black, period_font,
                preferredAsciiAAMode(period_label, period_font, 16u));
    }

    const uint16_t timeline_w =
        (period.timeline_right > period.timeline_left)
            ? static_cast<uint16_t>(period.timeline_right - period.timeline_left)
            : 0u;
    for (uint16_t minute_value = period.start_minute; minute_value <= period.end_minute;
         minute_value = static_cast<uint16_t>(minute_value + 60u)) {
      const uint16_t y = timelineYForMinute(period, minute_value);
      const String hour_label =
          String((minute_value / 60u < 10u) ? "0" : "") + String(minute_value / 60u);
      uint16_t label_y =
          (y > (axis_label_h / 2u)) ? static_cast<uint16_t>(y - axis_label_h / 2u) : 0u;
      if (label_y + axis_label_h > period.timeline_bottom) {
        label_y = static_cast<uint16_t>(period.timeline_bottom - axis_label_h);
      }
      sink.text(period.axis_x, label_y, hour_label, axis_label_px, black, axis_label_font,
                TextAAMode::Threshold);
      if (timeline_w > 0u) {
        sink.fillRect(makeRect(period.timeline_left, y, timeline_w, 1u), blue);
      }
    }
  }

  for (size_t i = 0; i < model.visible_event_count; ++i) {
    const VisibleEvent &event = model.visible_events[i];
    uint16_t start_minute = event.start_minute;
    uint16_t end_minute = event.end_minute;
    if (end_minute <= start_minute) {
      end_minute = static_cast<uint16_t>(start_minute + 30u);
    }
    for (uint8_t period_index = 0u; period_index < period_count; ++period_index) {
      const SchedulePeriodGeometry &period = periods[period_index];
      TimelineSegment segment;
      if (!clipTimelineSegment(start_minute, end_minute, period.start_minute,
                               period.end_minute, segment)) {
        continue;
      }
      const uint16_t timeline_w =
          (period.timeline_right > period.timeline_left)
              ? static_cast<uint16_t>(period.timeline_right - period.timeline_left)
              : 0u;
      if (timeline_w == 0u) {
        continue;
      }
      const uint16_t y0 = timelineYForMinute(period, segment.start_minute);
      const uint16_t y1 = timelineYForMinute(period, segment.end_minute);
      const uint16_t block_h =
          (y1 > y0) ? static_cast<uint16_t>(y1 - y0) : 1u;
      const uint16_t block_y = y0;

      const uint8_t lane_count = (event.lane_count == 0u) ? 1u : event.lane_count;
      const uint16_t lane_gap = 3u;
      const uint16_t available_w =
          (timeline_w > static_cast<uint16_t>((lane_count - 1u) * lane_gap))
              ? static_cast<uint16_t>(timeline_w - (lane_count - 1u) * lane_gap)
              : timeline_w;
      const uint16_t lane_w =
          (lane_count > 0u) ? static_cast<uint16_t>(available_w / lane_count) : available_w;
      const uint16_t block_x = static_cast<uint16_t>(
          period.timeline_left + event.lane * static_cast<uint16_t>(lane_w + lane_gap));
      const uint16_t block_w = (lane_w > 1u) ? static_cast<uint16_t>(lane_w - 1u) : lane_w;
      if (block_w == 0u) {
        continue;
      }
      const Rect block = makeRect(block_x, block_y, block_w, block_h);
      const uint8_t accent_color = (event.color_nibble == white) ? blue : event.color_nibble;
      emitRoundedOutline(sink, block, 4u, accent_color, white, 1u);
      const uint16_t accent_y =
          (block.h > 4u) ? static_cast<uint16_t>(block.y + 2u) : block.y;
      const uint16_t accent_h =
          (block.h > 4u) ? static_cast<uint16_t>(block.h - 4u) : block.h;
      const Rect accent = makeRect(static_cast<uint16_t>(block.x + 2u),
                                   accent_y, 4u, accent_h);
      sink.fillRect(accent, accent_color);
      emitScheduleContinuation(block, segment.continues_before, segment.continues_after,
                               accent_color, sink);

      const uint16_t text_pad_x = 9u;
      const uint16_t text_space =
          (block.w > text_pad_x * 2u)
              ? static_cast<uint16_t>(block.w - text_pad_x * 2u)
              : 0u;
      const uint16_t continuation_top = segment.continues_before ? 3u : 0u;
      const uint16_t continuation_bottom = segment.continues_after ? 3u : 0u;
      const uint16_t vertical_pad =
          (block.h >= static_cast<uint16_t>(kScheduleTitleCjkPx + 2u)) ? 1u : 0u;
      const uint16_t content_y =
          static_cast<uint16_t>(block.y + vertical_pad + continuation_top);
      const uint16_t content_bottom =
          static_cast<uint16_t>(block.y + block.h - vertical_pad - continuation_bottom);
      const uint16_t content_h =
          (content_bottom > content_y) ? static_cast<uint16_t>(content_bottom - content_y) : 0u;
      if (text_space == 0u) {
        continue;
      }
      const ScheduleTitleLayout title_layout =
          layoutScheduleTitle(event.title, text_space, content_h);
      if (title_layout.line_count == 0u || content_h < title_layout.total_height) {
        continue;
      }
      const uint16_t text_y = static_cast<uint16_t>(
          content_y + ((content_h > title_layout.total_height)
                           ? ((content_h - title_layout.total_height) / 2u)
                           : 0u));
      for (uint8_t line_index = 0u; line_index < title_layout.line_count; ++line_index) {
        const uint16_t line_y = static_cast<uint16_t>(
            text_y + line_index * (title_layout.line_height + kScheduleLineGap));
        emitScheduleTitleText(sink, static_cast<uint16_t>(block.x + text_pad_x), line_y,
                              title_layout.lines[line_index], *title_layout.style, black, false);
      }
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
    const TextFont day_font =
        (layout.mode == LayoutMode::LandscapeSplit) ? TextFont::Digit16 : TextFont::Digit14;
    const uint16_t text_w = textWidthPx(label, day_px, day_font);
    const uint16_t text_h = textHeightPx(label, day_px, day_font);
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
    uint16_t today_marker_extent = today_radius;
    if (cell.is_today) {
      const uint16_t circle_diameter = static_cast<uint16_t>(today_radius * 2u);
      const uint16_t marker_side =
          (circle_diameter > 2u) ? static_cast<uint16_t>(circle_diameter - 2u) : circle_diameter;
      today_marker_extent = static_cast<uint16_t>((marker_side + 1u) / 2u);
      const uint16_t marker_x = static_cast<uint16_t>(
          text_cx > marker_side / 2u ? text_cx - marker_side / 2u : 0u);
      const uint16_t marker_y = static_cast<uint16_t>(
          text_cy > marker_side / 2u ? text_cy - marker_side / 2u : 0u);
      const uint16_t corner_radius = std::max<uint16_t>(2u, marker_side / 5u);
      emitFilledRoundedRect(sink, makeRect(marker_x, marker_y, marker_side, marker_side),
                            corner_radius, red);
    }
    sink.text(text_x, text_y, label, day_px, cell.text_color, day_font, TextAAMode::Threshold);

    const DaySummary &summary = model.day_summaries[index];
    if (summary.item_count == 0u) {
      continue;
    }
    const uint8_t visible_limit =
        (layout.mode == LayoutMode::PortraitSplit || layout.grid_rows >= 6u) ? 2u : 3u;
    const uint8_t shown_count = std::min(summary.item_count, visible_limit);
    const uint16_t summary_y_base_raw = static_cast<uint16_t>(text_cy + today_marker_extent +
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
      const uint8_t summary_px = summary_ascii ? static_cast<uint8_t>(10) : static_cast<uint8_t>(12);
      const TextFont summary_font = summary_ascii ? TextFont::Ascii10 : TextFont::Cjk10;
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
