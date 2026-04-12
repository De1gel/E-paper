#ifndef CALENDAR_LAYOUT_H
#define CALENDAR_LAYOUT_H

#include <Arduino.h>

#include "calendar/CalendarTypes.h"

namespace calendar {

struct CalendarLayout {
  LayoutMode mode = LayoutMode::LandscapeSplit;
  Rect screen;
  Rect header_bar;
  Rect calendar_panel;
  Rect schedule_panel;
  Rect schedule_inner;
  Rect grid;

  uint16_t header_y = 0;
  uint16_t header_h = 0;
  uint16_t title_bar_x = 0;
  uint16_t title_bar_w = 0;
  uint16_t title_x = 0;
  uint16_t title_y = 0;
  uint8_t title_scale = 0;
  uint8_t weekday_scale = 0;
  uint8_t day_scale = 0;
  uint8_t list_scale = 2;
  uint16_t weekday_y = 0;
  uint16_t weekday_h = 0;
  uint16_t cell_w = 0;
  uint16_t cell_h = 0;
  uint16_t list_top = 0;
  uint16_t list_bottom = 0;
  uint16_t row_h = 0;
  uint16_t content_x = 0;
  uint16_t items_x = 0;
  uint16_t items_w = 0;
  uint16_t lane_w = 0;
  uint8_t lane_count = 1;
  uint8_t max_rows = 0;
  bool has_grid = false;
};

bool buildCalendarLayout(CalendarLayout &layout, LayoutMode mode, uint16_t screen_width,
                         uint16_t screen_height);

}  // namespace calendar

#endif
