#include "calendar/CalendarScene.h"

#include "Display_EPD_W21.h"
#include "calendar/CalendarText.h"

namespace calendar {
namespace {

constexpr uint8_t kAsciiBasePx = 5;
constexpr uint8_t kZhWeekdayPx = 18;
constexpr uint8_t kZhItemPx = 24;
constexpr uint8_t kZhMetaPx = 16;
constexpr uint8_t kZhScheduleTitlePx = 48;
constexpr uint8_t kHeaderPx = 10;
constexpr bool kShowAATestPanel = false;

uint8_t asciiPixelHeight(uint8_t scale) {
  return static_cast<uint8_t>(kAsciiBasePx * scale);
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
  const uint8_t weekday_px = zh_ui ? kZhWeekdayPx : asciiPixelHeight(layout.weekday_scale);
  const uint8_t schedule_title_px = zh_ui ? kZhScheduleTitlePx : asciiPixelHeight(2);
  const uint8_t item_px = zh_ui ? kZhItemPx : asciiPixelHeight(layout.list_scale);
  const uint8_t meta_px = zh_ui ? kZhMetaPx : kAsciiBasePx;
  const uint8_t header_px = kHeaderPx;
  const TextFont weekday_font = zh_ui ? TextFont::CjkAuto : TextFont::Auto;
  const TextFont schedule_font = zh_ui ? TextFont::CjkAuto : TextFont::Auto;
  const TextFont item_font = zh_ui ? TextFont::CjkAuto : TextFont::Auto;
  const TextFont meta_font = zh_ui ? TextFont::CjkAuto : TextFont::Auto;
  const TextFont header_font = zh_ui ? TextFont::CjkAuto : TextFont::Auto;
  sink.strokeRect(layout.screen, blue);
  sink.fillRect(layout.header_bar, blue);
  sink.strokeRect(layout.calendar_panel, blue);
  sink.strokeRect(layout.schedule_panel, green);

  const uint16_t header_left_x = static_cast<uint16_t>(layout.header_bar.x + 6);
  const uint16_t header_y = static_cast<uint16_t>(layout.header_y + 6);
  sink.text(header_left_x, header_y, model.header_datetime, header_px, white);
  const String header_right = model.header_weather + "  " + model.header_sensors;
  const uint16_t header_right_w = textWidthPx(header_right, header_px, header_font);
  const uint16_t header_right_x =
      (layout.header_bar.w > header_right_w + 12)
          ? static_cast<uint16_t>(layout.header_bar.x + layout.header_bar.w - header_right_w - 6)
          : header_left_x;
  sink.text(header_right_x, header_y, header_right, header_px, white, header_font);

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
      if (col >= 5 && layout.cell_w > 2) {
        sink.fillRect(makeRect(static_cast<uint16_t>(label_box_x + 1),
                               static_cast<uint16_t>(layout.weekday_y + 1),
                               static_cast<uint16_t>(layout.cell_w - 2),
                               static_cast<uint16_t>(layout.weekday_h - 2)),
                      yellow);
      }
      const uint16_t label_x = static_cast<uint16_t>(
          layout.grid.x + col * layout.cell_w +
          ((layout.cell_w > label_w) ? (layout.cell_w - label_w) / 2u : 0u));
      sink.text(label_x, layout.weekday_y, label, weekday_px, (col >= 5) ? red : green,
                weekday_font);
    }

    sink.strokeRect(layout.grid, blue);
    for (uint8_t col = 1; col < 7; ++col) {
      sink.fillRect(makeRect(static_cast<uint16_t>(layout.grid.x + col * layout.cell_w), layout.grid.y, 1,
                             layout.grid.h),
                    blue);
    }
    for (uint8_t row = 1; row < 6; ++row) {
      sink.fillRect(makeRect(layout.grid.x, static_cast<uint16_t>(layout.grid.y + row * layout.cell_h),
                             layout.grid.w, 1),
                    blue);
    }
  }

  sink.strokeRect(layout.schedule_inner, green);
  const uint16_t schedule_title_x = static_cast<uint16_t>(layout.schedule_inner.x + 8);
  const uint16_t schedule_title_y = static_cast<uint16_t>(layout.schedule_inner.y + 8);
  const uint16_t schedule_title_h = textHeightPx(model.schedule_title, schedule_title_px, schedule_font);
  sink.text(schedule_title_x, schedule_title_y, model.schedule_title, schedule_title_px, green,
            schedule_font);

  if (kShowAATestPanel && layout.mode == LayoutMode::LandscapeSplit) {
    emitAATestPanel(sink, layout);
    return;
  }

  if (!model.time_valid) {
    sink.text(static_cast<uint16_t>(layout.schedule_panel.x + 20),
              static_cast<uint16_t>(schedule_title_y + schedule_title_h + 8),
              model.no_time_label, schedule_title_px, red, schedule_font);
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
    sink.text(layout.content_x, static_cast<uint16_t>(y + 4), group.time_hhmm,
              asciiPixelHeight(layout.list_scale), blue);
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
      sink.text(text_x, static_cast<uint16_t>(y + 4),
                truncateTextToWidth(title, text_space, item_px, item_font),
                item_px, black, item_font);
    }
  }

  if (model.schedule_group_count > rendered_rows) {
    const uint16_t schedule_bottom =
        static_cast<uint16_t>(layout.schedule_inner.y + layout.schedule_inner.h);
    const uint16_t footer_y = (layout.list_bottom + meta_px + 4u <= schedule_bottom)
                                  ? static_cast<uint16_t>(layout.list_bottom + 2)
                                  : static_cast<uint16_t>(schedule_bottom - meta_px - 4);
    sink.text(static_cast<uint16_t>(layout.schedule_inner.x + 8), footer_y,
              "+" + String(model.schedule_group_count - rendered_rows) + " " + model.more_label,
              meta_px, red, meta_font);
  }

  if (!model.time_valid || !layout.has_grid) {
    return;
  }

  for (int index = 0; index < 42; ++index) {
    const int row = index / 7;
    const int col = index % 7;
    const DateCell &cell = model.date_cells[index];
    const uint16_t cell_x = static_cast<uint16_t>(layout.grid.x + col * layout.cell_w);
    const uint16_t cell_y = static_cast<uint16_t>(layout.grid.y + row * layout.cell_h);

    if (cell.is_today && layout.cell_w > 2 && layout.cell_h > 2) {
      sink.fillRect(makeRect(static_cast<uint16_t>(cell_x + 1), static_cast<uint16_t>(cell_y + 1),
                             static_cast<uint16_t>(layout.cell_w - 2),
                             static_cast<uint16_t>(layout.cell_h - 2)),
                    yellow);
    }

    const String label = String(cell.day);
    const uint16_t day_px = asciiPixelHeight(layout.day_scale);
    const uint16_t text_w = textWidthPx(label, day_px);
    const uint16_t text_h = textHeightPx(label, day_px);
    const uint16_t text_x = static_cast<uint16_t>(
        static_cast<uint16_t>(cell_x + 1) +
        (((layout.cell_w > 2 ? layout.cell_w - 2 : 0) > text_w)
             ? (((layout.cell_w - 2) - text_w) / 2u)
             : 0u));
    const uint16_t text_y = static_cast<uint16_t>(
        static_cast<uint16_t>(cell_y + 1) +
        (((layout.cell_h > 2 ? layout.cell_h - 2 : 0) > text_h)
             ? (((layout.cell_h - 2) - text_h) / 2u)
             : 0u));
    sink.text(text_x, text_y, label, day_px, cell.text_color);
  }
}

}  // namespace calendar
