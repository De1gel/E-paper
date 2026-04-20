#ifndef APP_CALENDAR_REFRESH_PLANNER_H
#define APP_CALENDAR_REFRESH_PLANNER_H

#include <Arduino.h>

#include "calendar/CalendarTypes.h"

namespace appfw {

enum class CalendarDirtyRegionKind : uint8_t {
  None = 0,
  FullScreen,
  HeaderTime,
  HeaderWeather,
  HeaderSensors,
};

enum class CalendarRefreshMode : uint8_t {
  None = 0,
  Full,
  Partial,
};

enum class CalendarRefreshReason : uint8_t {
  None = 0,
  ForcedFull,
  DayChanged,
  PartialBudgetExceeded,
  TimeTick,
  HeaderFieldsChanged,
  PartialCompatibilityPath,
};

struct CalendarDirtyRegionSet {
  static constexpr size_t kMaxRegions = 4;

  bool full_screen = false;
  size_t count = 0;
  CalendarDirtyRegionKind kinds[kMaxRegions] = {};
  calendar::Rect rects[kMaxRegions] = {};
};

struct CalendarRefreshInputs {
  bool force_full_refresh = false;
  bool time_valid = false;
  int32_t day_key = -1;
  int32_t last_day_key = -1;
  int32_t minute_key = -1;
  int32_t last_render_minute_key = -1;
  uint32_t time_refresh_sec = 0;
  uint16_t partial_refresh_count = 0;
  uint16_t partial_before_full = 0;
  bool body_changed = false;
  bool header_time_changed = false;
  bool header_weather_changed = false;
  bool header_sensors_changed = false;
  calendar::Rect full_screen_rect = {};
  calendar::Rect header_time_rect = {};
  calendar::Rect header_weather_rect = {};
  calendar::Rect header_sensors_rect = {};
};

struct CalendarRefreshPlan {
  CalendarRefreshMode mode = CalendarRefreshMode::None;
  CalendarRefreshReason reason = CalendarRefreshReason::None;
  bool time_only_refresh = false;
  CalendarDirtyRegionSet dirty = {};
};

CalendarRefreshPlan planCalendarRefresh(const CalendarRefreshInputs &inputs);
const char *calendarRefreshReasonName(CalendarRefreshReason reason);

}  // namespace appfw

#endif
