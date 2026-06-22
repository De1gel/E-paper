#include "calendar/CalendarLayout.h"

namespace calendar {

namespace {
constexpr uint16_t kLandscapeCalendarContentShiftX = 3u;
}

bool buildCalendarLayout(CalendarLayout &layout, LayoutMode mode, uint16_t screen_width,
                         uint16_t screen_height, uint8_t month_row_count) {
  layout = CalendarLayout{};
  layout.mode = mode;
  const uint16_t logical_width =
      (mode == LayoutMode::PortraitSplit) ? screen_height : screen_width;
  const uint16_t logical_height =
      (mode == LayoutMode::PortraitSplit) ? screen_width : screen_height;
  layout.screen = makeRect(0, 0, logical_width, logical_height);
  layout.grid_rows =
      static_cast<uint8_t>(month_row_count < 4 ? 4 : (month_row_count > 6 ? 6 : month_row_count));
  layout.header_h =
      (mode == LayoutMode::LandscapeSplit) ? static_cast<uint16_t>(78) : static_cast<uint16_t>(68);
  if (mode == LayoutMode::LandscapeSplit) {
    layout.calendar_panel =
        makeRect(0, 0, static_cast<uint16_t>(logical_width / 2), logical_height);
    layout.schedule_panel = makeRect(static_cast<uint16_t>(logical_width / 2), 0,
                                     static_cast<uint16_t>(logical_width / 2), logical_height);
  } else {
    layout.calendar_panel =
        makeRect(0, 0, logical_width, static_cast<uint16_t>(logical_height / 2));
    layout.schedule_panel = makeRect(0, static_cast<uint16_t>(logical_height / 2), logical_width,
                                     static_cast<uint16_t>(logical_height / 2));
  }

  const uint16_t margin = (mode == LayoutMode::LandscapeSplit) ? 8 : 6;
  const uint16_t calendar_content_shift_x =
      (mode == LayoutMode::LandscapeSplit) ? kLandscapeCalendarContentShiftX : 0u;
  layout.header_y = static_cast<uint16_t>(layout.calendar_panel.y + margin);
  layout.header_bar = makeRect(static_cast<uint16_t>(layout.calendar_panel.x + margin +
                                                     calendar_content_shift_x),
                               layout.header_y,
                               static_cast<uint16_t>(layout.calendar_panel.w > margin * 2
                                                         ? (layout.calendar_panel.w - margin * 2)
                                                         : layout.calendar_panel.w),
                               layout.header_h);
  layout.title_scale = (mode == LayoutMode::LandscapeSplit) ? 4 : 3;
  layout.weekday_scale = (mode == LayoutMode::LandscapeSplit) ? 2 : 1;
  layout.day_scale = (mode == LayoutMode::LandscapeSplit) ? 4 : 3;

  layout.title_y = layout.header_y;
  layout.title_bar_x = layout.header_bar.x;
  layout.title_bar_w = layout.header_bar.w;
  layout.weekday_y = static_cast<uint16_t>(layout.header_y + layout.header_h + 4);
  layout.weekday_h =
      (mode == LayoutMode::LandscapeSplit) ? static_cast<uint16_t>(28) : static_cast<uint16_t>(24);
  const uint16_t grid_top = static_cast<uint16_t>(layout.weekday_y + layout.weekday_h + 2);
  const uint16_t grid_left_base =
      static_cast<uint16_t>(layout.calendar_panel.x + margin + calendar_content_shift_x);
  const uint16_t grid_right =
      static_cast<uint16_t>(layout.calendar_panel.x + layout.calendar_panel.w - margin +
                            calendar_content_shift_x);
  if (grid_right <= grid_left_base || grid_top >= (layout.calendar_panel.y + layout.calendar_panel.h)) {
    return true;
  }

  uint16_t grid_w = static_cast<uint16_t>(grid_right - grid_left_base);
  const uint16_t grid_bottom_margin =
      (mode == LayoutMode::LandscapeSplit) ? static_cast<uint16_t>(10) : static_cast<uint16_t>(8);
  uint16_t grid_h = static_cast<uint16_t>(
      layout.calendar_panel.y + layout.calendar_panel.h > (margin + grid_bottom_margin) &&
              (layout.calendar_panel.y + layout.calendar_panel.h - margin - grid_bottom_margin) > grid_top
          ? (layout.calendar_panel.y + layout.calendar_panel.h - margin - grid_bottom_margin - grid_top)
          : 0);
  if (grid_w >= 140 && grid_h >= 60) {
    layout.cell_w = static_cast<uint16_t>(grid_w / 7u);
    layout.cell_h = static_cast<uint16_t>(grid_h / layout.grid_rows);
    grid_w = static_cast<uint16_t>(layout.cell_w * 7u);
    grid_h = static_cast<uint16_t>(layout.cell_h * layout.grid_rows);
    layout.cell_pad_y = (layout.grid_rows <= 4) ? static_cast<uint8_t>(7)
                        : (layout.grid_rows == 5) ? static_cast<uint8_t>(5)
                                                  : static_cast<uint8_t>(3);
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

  layout.list_top = static_cast<uint16_t>(layout.schedule_inner.y + 8);
  const uint16_t schedule_bottom =
      static_cast<uint16_t>(layout.schedule_inner.y + layout.schedule_inner.h);
  layout.list_bottom = (schedule_bottom > layout.list_top + 8)
                           ? static_cast<uint16_t>(schedule_bottom - 8)
                           : layout.list_top;
  return true;
}

}  // namespace calendar
