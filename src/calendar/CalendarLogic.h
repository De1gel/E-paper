#ifndef CALENDAR_LOGIC_H
#define CALENDAR_LOGIC_H

#include <stddef.h>
#include <stdint.h>

namespace calendar {

struct TimelineEventSlot {
  uint16_t start_minute = 0;
  uint16_t end_minute = 0;
  uint8_t lane = 0;
  uint8_t lane_count = 1;
};

bool calendarEventMatchesToday(const char *repeat, int weekday, const char *date,
                               const char *today_ymd, int today_weekday);
void assignTimelineLanes(TimelineEventSlot *events, size_t event_count, size_t max_lanes);

}  // namespace calendar

#endif
