#ifndef SYSTEM_CALENDAR_EVENT_NORMALIZE_H
#define SYSTEM_CALENDAR_EVENT_NORMALIZE_H

#include <Arduino.h>

namespace appfw {

bool normalizeCalendarTimeValue(const String &raw, String &normalized);
bool normalizeCalendarDateValue(const String &raw, String &normalized);
String normalizeCalendarColorValue(const String &raw);
String normalizeCalendarRepeatValue(const String &raw);
String normalizeCalendarSourceValue(const String &raw);
String normalizeCalendarExternalIdValue(const String &raw);
String normalizeCalendarUpdatedAtValue(const String &raw);

}  // namespace appfw

#endif
