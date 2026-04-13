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

}  // namespace

void emitCalendarScene(const CalendarModel &model, const CalendarLayout &layout, SceneSink &sink) {
  const bool zh_ui = (model.ui_language == "zh");
  const uint8_t title_px = asciiPixelHeight(layout.title_scale);
  const uint8_t weekday_px =
      zh_ui ? kZhWeekdayPx
            : static_cast<uint8_t>(asciiPixelHeight(layout.weekday_scale) +
                                   (layout.mode == LayoutMode::LandscapeSplit ? 2 : 1));
  const uint8_t schedule_title_px = zh_ui ? kZhScheduleTitlePx : asciiPixelHeight(2);
  const uint8_t item_px = zh_ui ? kZhItemPx : asciiPixelHeight(layout.list_scale);
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
  const uint16_t schedule_title_x = static_cast<uint16_t>(layout.schedule_inner.x + 8);
  const uint16_t schedule_title_y = static_cast<uint16_t>(layout.schedule_inner.y + 8);
  const uint16_t schedule_title_h = textHeightPx(model.schedule_title, schedule_title_px, schedule_font);
  const TextFont schedule_title_font =
      preferredTextFont(model.schedule_title, schedule_font, schedule_title_px);
  sink.text(schedule_title_x, schedule_title_y, model.schedule_title, schedule_title_px, green,
            schedule_title_font,
            preferredAsciiAAMode(model.schedule_title, schedule_title_font, schedule_title_px));

  if (kShowAATestPanel && layout.mode == LayoutMode::LandscapeSplit) {
    emitAATestPanel(sink, layout);
    return;
  }

  if (!model.time_valid) {
    const TextFont no_time_font =
        preferredTextFont(model.no_time_label, schedule_font, schedule_title_px);
    sink.text(static_cast<uint16_t>(layout.schedule_panel.x + 20),
              static_cast<uint16_t>(schedule_title_y + schedule_title_h + 8),
              model.no_time_label, schedule_title_px, red, no_time_font,
              preferredAsciiAAMode(model.no_time_label, no_time_font, schedule_title_px));
  }

  uint8_t rendered_rows = 0;
  for (uint8_t row = 0; row < layout.max_rows; ++row) {
    const uint16_t y = static_cast<uint16_t>(layout.list_top + row * layout.row_h);
    if (y + layout.row_h > layout.list_bottom) {
      break;
    }
    rendered_rows = static_cast<uint8_t>(row + 1);
    sink.fillRect(makeRect(static_cast<uint16_t>(layout.schedule_inner.x + 6),
                           static_cast<uint16_t>(y + layout.row_h - 1),
                           static_cast<uint16_t>(layout.schedule_inner.w - 12), 1),
                  green);
    if (row >= model.schedule_group_count) {
      continue;
    }

    const ScheduleGroup &group = model.schedule_groups[row];
    const TextFont group_time_font =
        preferredTextFont(group.time_hhmm, TextFont::Auto, asciiPixelHeight(layout.list_scale));
    sink.text(layout.content_x, static_cast<uint16_t>(y + 4), group.time_hhmm,
              asciiPixelHeight(layout.list_scale), blue, group_time_font,
              preferredAsciiAAMode(group.time_hhmm, group_time_font,
                                   asciiPixelHeight(layout.list_scale)));
    if (layout.items_w == 0) {
      continue;
    }

    for (uint8_t lane = 0; lane < layout.lane_count; ++lane) {
      const uint16_t lane_x = static_cast<uint16_t>(layout.items_x + lane * layout.lane_w);
      const uint16_t lane_inner_x = static_cast<uint16_t>(lane_x + 2);
      const uint16_t lane_inner_w =
          static_cast<uint16_t>(layout.lane_w > 4 ? (layout.lane_w - 4) : layout.lane_w);
      if (lane > 0) {
        sink.fillRect(makeRect(lane_x, static_cast<uint16_t>(y + 2), 1,
                               static_cast<uint16_t>(layout.row_h > 4 ? (layout.row_h - 4) : layout.row_h)),
                      green);
      }

      bool draw_more = false;
      uint8_t more_count = 0;
      if (lane == static_cast<uint8_t>(layout.lane_count - 1) &&
          group.event_count > static_cast<uint8_t>(layout.lane_count)) {
        draw_more = true;
        more_count = static_cast<uint8_t>(group.event_count - (layout.lane_count - 1));
      }
      if (!draw_more && lane >= group.event_count) {
        continue;
      }

      String title;
      uint8_t event_color = yellow;
      if (draw_more) {
        title = "+" + String(more_count);
      } else {
        const VisibleEvent &event = model.visible_events[group.event_indices[lane]];
        title = event.title;
        event_color = event.color_nibble;
      }

      const uint16_t chip_h = static_cast<uint16_t>(layout.row_h > 8 ? (layout.row_h - 8) : 8);
      const Rect chip = makeRect(lane_inner_x, static_cast<uint16_t>(y + 4), 8, chip_h);
      sink.fillRect(chip, event_color);
      sink.strokeRect(chip, black);

      const uint16_t text_x = static_cast<uint16_t>(lane_inner_x + 12);
      const uint16_t text_space =
          static_cast<uint16_t>(lane_inner_w > 14 ? (lane_inner_w - 14) : 0);
      const String visible_title = truncateTextToWidth(title, text_space, item_px, item_font);
      const TextFont title_font = preferredTextFont(visible_title, item_font, item_px);
      sink.text(text_x, static_cast<uint16_t>(y + 4),
                visible_title,
                item_px, black, title_font,
                preferredAsciiAAMode(visible_title, title_font, item_px));
    }
  }

  if (model.schedule_group_count > rendered_rows) {
    const uint16_t schedule_bottom =
        static_cast<uint16_t>(layout.schedule_inner.y + layout.schedule_inner.h);
    const uint16_t footer_y = (layout.list_bottom + meta_px + 4u <= schedule_bottom)
                                  ? static_cast<uint16_t>(layout.list_bottom + 2)
                                  : static_cast<uint16_t>(schedule_bottom - meta_px - 4);
    const String footer_text =
        "+" + String(model.schedule_group_count - rendered_rows) + " " + model.more_label;
    const TextFont footer_font = preferredTextFont(footer_text, meta_font, meta_px);
    sink.text(static_cast<uint16_t>(layout.schedule_inner.x + 8), footer_y,
              footer_text,
              meta_px, red, footer_font,
              preferredAsciiAAMode(footer_text, footer_font, meta_px));
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
