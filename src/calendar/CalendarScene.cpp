#include "calendar/CalendarScene.h"

#include "Display_EPD_W21.h"
#include "calendar/CalendarText.h"

namespace calendar {
namespace {

constexpr uint8_t kAsciiBasePx = 7;
constexpr uint8_t kZhWeekdayPx = 26;
constexpr uint8_t kZhItemPx = 24;
constexpr uint8_t kZhMetaPx = 16;
constexpr uint8_t kZhScheduleTitlePx = 48;
constexpr uint8_t kHeaderPx = 24;
constexpr bool kShowAATestPanel = false;
constexpr uint16_t kScheduleStartMinute = 8u * 60u;
constexpr uint16_t kScheduleEndMinute = 22u * 60u;
constexpr uint8_t kScheduleSlotCount = 28u;

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
  const uint16_t marker_w = 7;
  const uint16_t marker_h = 9;
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
                  black);
  }
}

}  // namespace

void emitCalendarScene(const CalendarModel &model, const CalendarLayout &layout, SceneSink &sink) {
  const bool zh_ui = (model.ui_language == "zh");
  const uint8_t title_px = asciiPixelHeight(layout.title_scale);
  const uint8_t weekday_px =
      zh_ui ? kZhWeekdayPx
            : static_cast<uint8_t>(asciiPixelHeight(layout.weekday_scale) +
                                   (layout.mode == LayoutMode::LandscapeSplit ? 2 : 1));
  const uint8_t item_px = zh_ui ? static_cast<uint8_t>(14) : static_cast<uint8_t>(10);
  const uint8_t meta_px = zh_ui ? kZhMetaPx : kAsciiBasePx;
  const uint8_t header_px = kHeaderPx;
  const uint8_t day_px =
      (layout.mode == LayoutMode::LandscapeSplit) ? static_cast<uint8_t>(20)
                                                  : asciiPixelHeight(layout.day_scale);
  const TextFont weekday_font = zh_ui ? TextFont::CjkAuto : TextFont::Auto;
  const TextFont schedule_font = zh_ui ? TextFont::CjkAuto : TextFont::Auto;
  const TextFont item_font = zh_ui ? TextFont::CjkAuto : TextFont::Auto;
  const TextFont meta_font = zh_ui ? TextFont::CjkAuto : TextFont::Auto;
  const TextFont header_font = zh_ui ? TextFont::CjkAuto : TextFont::Auto;
  const TextFont header_date_font = preferredTextFont(model.header_date, header_font, header_px);
  const TextFont header_time_font = preferredTextFont(model.header_time, TextFont::Auto, header_px);
  const TextFont header_weather_font =
      preferredTextFont(model.header_weather, header_font, header_px);
  const TextFont header_sensors_font =
      preferredTextFont(model.header_sensors, header_font, header_px);
  const TextAAMode header_date_aa =
      preferredAsciiAAMode(model.header_date, header_date_font, header_px);
  const TextAAMode header_time_aa =
      preferredAsciiAAMode(model.header_time, header_time_font, header_px);
  const TextAAMode header_weather_aa =
      preferredAsciiAAMode(model.header_weather, header_weather_font, header_px);
  const TextAAMode header_sensors_aa =
      preferredAsciiAAMode(model.header_sensors, header_sensors_font, header_px);
  sink.strokeRect(layout.screen, blue);
  sink.fillRect(layout.header_bar, blue);
  sink.strokeRect(layout.calendar_panel, blue);
  sink.strokeRect(layout.schedule_panel, green);

  const uint16_t header_left_x = static_cast<uint16_t>(layout.header_bar.x + 6);
  const uint16_t header_date_y = static_cast<uint16_t>(layout.header_y + 6);
  const uint16_t header_time_y =
      static_cast<uint16_t>(header_date_y + textHeightPx(model.header_date, header_px, header_font) + 2);
  sink.text(header_left_x, header_date_y, model.header_date, header_px, white, header_date_font,
            header_date_aa);
  sink.text(header_left_x, header_time_y, model.header_time, header_px, white, header_time_font,
            header_time_aa);
  const uint16_t header_right_w = std::max(textWidthPx(model.header_weather, header_px, header_font),
                                           textWidthPx(model.header_sensors, header_px, header_font));
  const uint16_t header_right_x =
      (layout.header_bar.w > header_right_w + 12)
          ? static_cast<uint16_t>(layout.header_bar.x + layout.header_bar.w - header_right_w - 6)
          : header_left_x;
  sink.text(header_right_x, header_date_y, model.header_weather, header_px, white, header_weather_font,
            header_weather_aa);
  sink.text(header_right_x, header_time_y, model.header_sensors, header_px, white, header_sensors_font,
            header_sensors_aa);

  if (layout.mode == LayoutMode::LandscapeSplit) {
    sink.fillRect(makeRect(static_cast<uint16_t>(layout.screen.w / 2), 0, 1, layout.screen.h), blue);
  } else {
    sink.fillRect(makeRect(0, static_cast<uint16_t>(layout.screen.h / 2), layout.screen.w, 1), blue);
  }

  if (layout.has_grid) {
    for (uint8_t col = 0; col < 7; ++col) {
      const String label = model.weekday_labels[col];
      const uint16_t label_w = textWidthPx(label, weekday_px, weekday_font);
      const uint16_t label_box_x = static_cast<uint16_t>(layout.grid.x + col * layout.cell_w);
      const uint16_t label_x = static_cast<uint16_t>(
          layout.grid.x + col * layout.cell_w +
          ((layout.cell_w > label_w) ? (layout.cell_w - label_w) / 2u : 0u));
      const TextFont label_font = preferredTextFont(label, weekday_font, weekday_px);
      sink.text(label_x, layout.weekday_y, label, weekday_px, isWeekendColumn(col) ? red : green,
                label_font, preferredAsciiAAMode(label, label_font, weekday_px));
    }
  }

  sink.strokeRect(layout.schedule_inner, green);

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
        sink.text(axis_x, static_cast<uint16_t>(y + 1), hour_label, asciiPixelHeight(1), blue,
                  TextFont::AsciiSmooth,
                  preferredAsciiAAMode(hour_label, TextFont::AsciiSmooth, asciiPixelHeight(1)));
      }
    }
    if (timeline_w == 0) {
      continue;
    }
    const uint16_t line_x = is_hour_line ? timeline_left : static_cast<uint16_t>(timeline_left + 8);
    const uint16_t line_w = is_hour_line ? timeline_w : static_cast<uint16_t>(timeline_w - 8);
    sink.fillRect(makeRect(line_x, y, line_w, 1), is_hour_line ? green : blue);
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
    sink.fillRect(block, event.color_nibble);
    sink.strokeRect(block, black);

    const uint16_t text_pad_x = 3;
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
    const TextFont empty_font = preferredTextFont(model.no_time_label, schedule_font, empty_px);
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
    const uint16_t content_h =
        (layout.cell_h > static_cast<uint16_t>(layout.cell_pad_y * 2u))
            ? static_cast<uint16_t>(layout.cell_h - layout.cell_pad_y * 2u)
            : layout.cell_h;
    if (cell.is_today) {
      const uint16_t cell_cx = static_cast<uint16_t>(cell_x + layout.cell_w / 2u);
      const uint16_t cell_cy = static_cast<uint16_t>(cell_y + layout.cell_h / 2u);
      const uint16_t text_box = (text_w > text_h) ? text_w : text_h;
      uint16_t radius = static_cast<uint16_t>(text_box / 2u + 5u);
      const uint16_t max_radius =
          (layout.cell_w < content_h ? layout.cell_w : content_h) > 8
              ? static_cast<uint16_t>((layout.cell_w < content_h ? layout.cell_w : content_h) /
                                      2u - 3u)
              : 0u;
      if (radius > max_radius) {
        radius = max_radius;
      }
      emitFilledCircle(sink, cell_cx, cell_cy, radius, red);
    }
    const uint16_t text_x = static_cast<uint16_t>(
        cell_x +
        ((layout.cell_w > text_w)
             ? ((layout.cell_w - text_w) / 2u)
             : 0u));
    const uint16_t text_y = static_cast<uint16_t>(
        static_cast<uint16_t>(cell_y + layout.cell_pad_y) +
        ((content_h > text_h)
             ? ((content_h - text_h) / 2u)
             : 0u));
    sink.text(text_x, text_y, label, day_px, cell.text_color, TextFont::AsciiSmooth,
              preferredAsciiAAMode(label, TextFont::AsciiSmooth, day_px));
  }
}

}  // namespace calendar
