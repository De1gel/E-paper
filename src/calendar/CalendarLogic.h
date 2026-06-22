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

struct TimelineSegment {
  uint16_t start_minute = 0;
  uint16_t end_minute = 0;
  bool continues_before = false;
  bool continues_after = false;
};

bool calendarEventMatchesToday(const char *repeat, int weekday, const char *date,
                               const char *today_ymd, int today_weekday);
void assignTimelineLanes(TimelineEventSlot *events, size_t event_count, size_t max_lanes);
bool clipTimelineSegment(uint16_t event_start, uint16_t event_end, uint16_t period_start,
                         uint16_t period_end, TimelineSegment &segment);
uint16_t timelineOffsetForMinute(uint16_t minute_value, uint16_t period_start,
                                 uint16_t period_end, uint16_t height);

}  // namespace calendar

#endif
