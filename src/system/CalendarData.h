#ifndef SYSTEM_CALENDAR_DATA_H
#define SYSTEM_CALENDAR_DATA_H

#include <Arduino.h>

namespace appfw {

static constexpr size_t kMaxCalendarEvents = 24;

struct CalendarEvent {
  uint16_t id = 0;
  String title;
  String date;       // YYYY-MM-DD for one-time event.
  String time_hhmm;  // HH:MM
  String end_time_hhmm;  // Optional HH:MM, reserved for future sync.
  String color;      // black/white/yellow/red/blue/green
  String repeat;     // once/daily/weekly
  int8_t weekday = -1;  // 0-6 (Mon-Sun), used for weekly.
  String source;  // manual/outlook/google...
  String external_id;  // Provider-side stable id.
  String updated_at;   // Optional ISO-8601 or epoch string.
};

}  // namespace appfw

#endif
