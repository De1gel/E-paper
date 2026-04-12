#include "calendar/CalendarLayout.h"

#include "calendar/CalendarText.h"

namespace calendar {

bool buildCalendarLayout(CalendarLayout &layout, LayoutMode mode, uint16_t screen_width,
                         uint16_t screen_height) {
  layout = CalendarLayout{};
  layout.mode = mode;
  layout.screen = makeRect(0, 0, screen_width, screen_height);
  layout.header_h =
      (mode == LayoutMode::LandscapeSplit) ? static_cast<uint16_t>(38) : static_cast<uint16_t>(34);
  if (mode == LayoutMode::LandscapeSplit) {
    layout.calendar_panel = makeRect(0, 0, static_cast<uint16_t>(screen_width / 2), screen_height);
    layout.schedule_panel = makeRect(static_cast<uint16_t>(screen_width / 2), 0,
                                     static_cast<uint16_t>(screen_width / 2), screen_height);
  } else {
    layout.calendar_panel = makeRect(0, 0, screen_width, static_cast<uint16_t>(screen_height / 2));
    layout.schedule_panel = makeRect(0, static_cast<uint16_t>(screen_height / 2), screen_width,
                                     static_cast<uint16_t>(screen_height / 2));
  }

  const uint16_t margin = (mode == LayoutMode::LandscapeSplit) ? 10 : 8;
  layout.header_y = static_cast<uint16_t>(layout.calendar_panel.y + margin);
  layout.header_bar = makeRect(static_cast<uint16_t>(layout.calendar_panel.x + margin), layout.header_y,
                               static_cast<uint16_t>(layout.calendar_panel.w > margin * 2
                                                         ? (layout.calendar_panel.w - margin * 2)
                                                         : layout.calendar_panel.w),
                               layout.header_h);
  layout.title_scale = (mode == LayoutMode::LandscapeSplit) ? 4 : 3;
  layout.weekday_scale = (mode == LayoutMode::LandscapeSplit) ? 2 : 1;
  layout.day_scale = (mode == LayoutMode::LandscapeSplit) ? 3 : 2;

  layout.title_y = layout.header_y;
  layout.title_bar_x = layout.header_bar.x;
  layout.title_bar_w = layout.header_bar.w;
  layout.weekday_y = static_cast<uint16_t>(layout.header_y + layout.header_h + 8);
  layout.weekday_h =
      (mode == LayoutMode::LandscapeSplit) ? static_cast<uint16_t>(34) : static_cast<uint16_t>(30);
  const uint16_t grid_top = static_cast<uint16_t>(layout.weekday_y + layout.weekday_h + 4);
  const uint16_t grid_left_base = static_cast<uint16_t>(layout.calendar_panel.x + margin);
  const uint16_t grid_right =
      static_cast<uint16_t>(layout.calendar_panel.x + layout.calendar_panel.w - margin);
  if (grid_right <= grid_left_base || grid_top >= (layout.calendar_panel.y + layout.calendar_panel.h)) {
    return true;
  }

  uint16_t grid_w = static_cast<uint16_t>(grid_right - grid_left_base);
  uint16_t grid_h = static_cast<uint16_t>(
      layout.calendar_panel.y + layout.calendar_panel.h - margin > grid_top
          ? (layout.calendar_panel.y + layout.calendar_panel.h - margin - grid_top)
          : 0);
  if (grid_w >= 140 && grid_h >= 60) {
    layout.cell_w = static_cast<uint16_t>(grid_w / 7u);
    layout.cell_h = static_cast<uint16_t>(grid_h / 6u);
    grid_w = static_cast<uint16_t>(layout.cell_w * 7u);
    grid_h = static_cast<uint16_t>(layout.cell_h * 6u);
    const uint16_t grid_left = static_cast<uint16_t>(
        grid_left_base + ((grid_right - grid_left_base - grid_w) / 2u));
    layout.grid = makeRect(grid_left, grid_top, grid_w, grid_h);
    layout.has_grid = true;
  }

  const uint16_t schedule_margin = 10;
  layout.schedule_inner = makeRect(
      static_cast<uint16_t>(layout.schedule_panel.x + schedule_margin),
      static_cast<uint16_t>(layout.schedule_panel.y + schedule_margin),
      static_cast<uint16_t>(layout.schedule_panel.w > schedule_margin * 2
                                ? (layout.schedule_panel.w - schedule_margin * 2)
                                : layout.schedule_panel.w),
      static_cast<uint16_t>(layout.schedule_panel.h > schedule_margin * 2
                                ? (layout.schedule_panel.h - schedule_margin * 2)
                                : layout.schedule_panel.h));

  layout.list_top = static_cast<uint16_t>(layout.schedule_inner.y + 40);
  const uint16_t schedule_bottom =
      static_cast<uint16_t>(layout.schedule_inner.y + layout.schedule_inner.h);
  layout.list_bottom = (schedule_bottom > layout.list_top + 8)
                           ? static_cast<uint16_t>(schedule_bottom - 8)
                           : layout.list_top;
  layout.max_rows =
      (mode == LayoutMode::LandscapeSplit) ? static_cast<uint8_t>(10) : static_cast<uint8_t>(5);
  const uint16_t usable_h = (layout.list_bottom > layout.list_top)
                                ? static_cast<uint16_t>(layout.list_bottom - layout.list_top)
                                : 0;
  layout.row_h = (layout.max_rows > 0) ? static_cast<uint16_t>(usable_h / layout.max_rows) : usable_h;
  if (layout.row_h < 18) {
    layout.row_h = 18;
  }

  const uint16_t left_margin = 8;
  const uint16_t right_margin = 8;
  const uint16_t time_col_w = static_cast<uint16_t>(textWidth3x5("23:59", layout.list_scale) + 8);
  layout.content_x = static_cast<uint16_t>(layout.schedule_inner.x + left_margin);
  layout.items_x = static_cast<uint16_t>(layout.content_x + time_col_w);
  layout.items_w = static_cast<uint16_t>(
      (layout.schedule_inner.w > (left_margin + right_margin + time_col_w))
          ? (layout.schedule_inner.w - left_margin - right_margin - time_col_w)
          : 0);
  if (layout.items_w >= 220) {
    layout.lane_count = 3;
  } else if (layout.items_w >= 120) {
    layout.lane_count = 2;
  }
  layout.lane_w =
      (layout.lane_count > 0) ? static_cast<uint16_t>(layout.items_w / layout.lane_count)
                              : layout.items_w;
  if (layout.lane_w < 50) {
    layout.lane_count = 1;
    layout.lane_w = layout.items_w;
  }
  return true;
}

}  // namespace calendar
