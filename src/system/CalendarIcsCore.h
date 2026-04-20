#ifndef SYSTEM_CALENDAR_ICS_CORE_H
#define SYSTEM_CALENDAR_ICS_CORE_H

#include <Arduino.h>
#include <time.h>
#include <stdint.h>
#include <vector>

#include "system/CalendarData.h"

namespace appfw {

struct IcsOccurrenceKey {
  bool valid = false;
  bool all_day = false;
  int32_t day_id = 0;
  int64_t minute_epoch = 0;
};

struct ParsedIcsEvent {
  String uid;
  String summary;
  String description;
  String location;
  String status;
  String rrule;
  bool start_valid = false;
  bool end_valid = false;
  bool recurrence_id_valid = false;
  bool all_day = false;
  time_t start_epoch = 0;
  time_t end_epoch = 0;
  struct tm start_tm {};
  struct tm end_tm {};
  IcsOccurrenceKey recurrence_id {};
  std::vector<IcsOccurrenceKey> exdates;
};

struct CalendarRruleCore {
  String freq;
  uint16_t interval = 1;
  uint16_t count = 0;
  bool until_valid = false;
  time_t until_epoch = 0;
  uint8_t byday_mask = 0;
};

struct IcsOverride {
  String uid;
  IcsOccurrenceKey key;
  bool cancelled = false;
};

struct ImportedCalendarEvent {
  CalendarEvent event;
  time_t sort_epoch = 0;
};

bool parseDigits(const String &value, int start, int count, int &out);
bool paramsContainDateValue(const String &params);
String icsUnescape(const String &value);
bool parseIcsDateTime(const String &raw_value, const String &params, bool device_local_time,
                      time_t &epoch_out, struct tm &local_tm_out, bool &all_day_out);
bool parseIcsDateField(const String &raw_value, const String &params, time_t &epoch_out,
                       struct tm &tm_out, bool &all_day_out);
void appendUnfoldedIcsLines(const String &body, std::vector<String> &lines);
bool splitIcsProperty(const String &line, String &name, String &params, String &value);
bool parseRruleCore(const String &raw_rrule, CalendarRruleCore &rrule);
int weekdayMon0FromTm(const struct tm &tm_value);
time_t localWeekWindowStart(time_t now_epoch);
time_t localWindowEndOneMonth(time_t window_start);
bool eventOverlapsWindow(time_t start_epoch, time_t end_epoch, time_t window_start,
                         time_t window_end);
String trimDisplayField(const String &raw, size_t max_len);
String buildImportedTitle(const String &summary, const String &location, const String &description);
IcsOccurrenceKey buildOccurrenceKey(const struct tm &tm_value, bool all_day, time_t epoch_value);
bool occurrenceKeysEqual(const IcsOccurrenceKey &a, const IcsOccurrenceKey &b);
bool hasOccurrenceKey(const std::vector<IcsOccurrenceKey> &keys, const IcsOccurrenceKey &needle);
bool parseIcsEventFromLines(const std::vector<String> &vevent_lines, ParsedIcsEvent &event);
void parseIcsBodyIntoEvents(const String &body, std::vector<ParsedIcsEvent> &masters,
                            std::vector<ParsedIcsEvent> &overrides, size_t &vevent_count);
void collectOverrideMetadata(const std::vector<ParsedIcsEvent> &override_events,
                             std::vector<IcsOverride> &override_metadata);
void expandSingleEvent(const ParsedIcsEvent &event, time_t window_start, time_t window_end,
                       std::vector<ImportedCalendarEvent> &out_items);
void expandRecurringEvent(const ParsedIcsEvent &event, const CalendarRruleCore &rrule,
                          const std::vector<IcsOverride> &override_metadata, time_t window_start,
                          time_t window_end, std::vector<ImportedCalendarEvent> &out_items);
void appendOverrideEvents(const std::vector<ParsedIcsEvent> &override_events, time_t window_start,
                          time_t window_end, std::vector<ImportedCalendarEvent> &out_items);
void sortImportedEvents(std::vector<ImportedCalendarEvent> &items);
uint8_t weekdayMaskBit(int weekday_mon0);
bool parseByDayToken(const String &token, int &weekday_mon0);

}  // namespace appfw

#endif
