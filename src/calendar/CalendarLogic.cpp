#include "calendar/CalendarLogic.h"

namespace calendar {

namespace {

bool equalsText(const char *lhs, const char *rhs) {
  if (lhs == rhs) {
    return true;
  }
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }
  while (*lhs != '\0' && *rhs != '\0') {
    if (*lhs != *rhs) {
      return false;
    }
    ++lhs;
    ++rhs;
  }
  return *lhs == *rhs;
}

}  // namespace

bool calendarEventMatchesToday(const char *repeat, int weekday, const char *date,
                               const char *today_ymd, int today_weekday) {
  if (equalsText(repeat, "daily")) {
    return true;
  }
  if (equalsText(repeat, "weekly")) {
    return weekday == today_weekday;
  }
  return equalsText(date, today_ymd);
}

void assignTimelineLanes(TimelineEventSlot *events, size_t event_count, size_t max_lanes) {
  if (events == nullptr || event_count == 0 || max_lanes == 0) {
    return;
  }

  size_t cluster_begin = 0;
  uint16_t cluster_end = events[0].end_minute;
  uint8_t cluster_max_lanes = 1;
  size_t active_indices[32] = {};
  size_t active_count = 0;

  if (max_lanes > (sizeof(active_indices) / sizeof(active_indices[0]))) {
    max_lanes = sizeof(active_indices) / sizeof(active_indices[0]);
  }

  for (size_t i = 0; i < event_count; ++i) {
    TimelineEventSlot &event = events[i];

    size_t write = 0;
    for (size_t j = 0; j < active_count; ++j) {
      const TimelineEventSlot &active = events[active_indices[j]];
      if (active.end_minute > event.start_minute) {
        active_indices[write++] = active_indices[j];
      }
    }
    active_count = write;

    bool lane_used[32] = {};
    for (size_t j = 0; j < active_count; ++j) {
      lane_used[events[active_indices[j]].lane] = true;
    }
    uint8_t lane = 0;
    while (lane < max_lanes && lane_used[lane]) {
      ++lane;
    }
    event.lane = lane;
    active_indices[active_count++] = i;

    const uint8_t active_lanes = static_cast<uint8_t>(active_count);
    if (i == cluster_begin) {
      cluster_end = event.end_minute;
      cluster_max_lanes = active_lanes;
      continue;
    }

    if (event.start_minute < cluster_end) {
      if (event.end_minute > cluster_end) {
        cluster_end = event.end_minute;
      }
      if (active_lanes > cluster_max_lanes) {
        cluster_max_lanes = active_lanes;
      }
    } else {
      for (size_t idx = cluster_begin; idx < i; ++idx) {
        events[idx].lane_count = cluster_max_lanes;
      }
      cluster_begin = i;
      cluster_end = event.end_minute;
      cluster_max_lanes = active_lanes;
    }
  }

  for (size_t idx = cluster_begin; idx < event_count; ++idx) {
    events[idx].lane_count = cluster_max_lanes;
  }
}

}  // namespace calendar
