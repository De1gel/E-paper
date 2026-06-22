#ifndef CALENDAR_MODEL_H
#define CALENDAR_MODEL_H

#include <Arduino.h>
#include <time.h>

#include "calendar/CalendarTypes.h"
#include "system/CalendarData.h"
#include "system/WifiManager.h"

namespace calendar {

struct DateCell {
  int day = 0;
  bool in_current = false;
  bool is_today = false;
  uint8_t text_color = 0;
};

struct DaySummary {
  static constexpr uint8_t kMaxItems = 3;

  struct Item {
    uint8_t color_nibble = 0;
    char label[16] = {};
  };

  uint8_t item_count = 0;
  uint8_t hidden_count = 0;
  Item items[kMaxItems] = {};
};

struct VisibleEvent {
  uint16_t id = 0;
  String title;
  String time_hhmm;
  String end_time_hhmm;
  uint16_t start_minute = 0;
  uint16_t end_minute = 0;
  uint8_t lane = 0;
  uint8_t lane_count = 1;
  uint8_t color_nibble = 0;
};

struct CalendarModel {
  bool time_valid = false;
  bool schedule_two_columns = false;
  LayoutMode layout_mode = LayoutMode::LandscapeSplit;
  uint16_t current_minute_of_day = 0;
  String ui_language;
  String header_date;
  String header_weather;
  String header_sensors;
  int16_t header_weather_code = -1;
  bool header_wifi_connected = false;
  int16_t header_battery_pct = -1;
  String weekday_labels[7];
  uint8_t month_row_count = 6;

  size_t visible_event_count = 0;
  VisibleEvent visible_events[appfw::kMaxCalendarEvents];

  DateCell date_cells[42];
  DaySummary day_summaries[42];
};

void buildCalendarModel(CalendarModel &model, const struct tm &local_tm, bool time_valid,
                        LayoutMode layout_mode, const String &ui_language,
                        const appfw::WifiManager &wifi_manager,
                        bool force_header_wifi_connected = false);

}  // namespace calendar

#endif
