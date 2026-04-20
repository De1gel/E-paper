#include "system/CalendarIcsCore.h"

#include <cstring>
#include <stdlib.h>
#include <time.h>

namespace appfw {

bool parseDigits(const String &value, int start, int count, int &out) {
  if (start < 0 || count <= 0 || (start + count) > static_cast<int>(value.length())) {
    return false;
  }
  int parsed = 0;
  for (int i = 0; i < count; ++i) {
    const char c = value[start + i];
    if (c < '0' || c > '9') {
      return false;
    }
    parsed = parsed * 10 + (c - '0');
  }
  out = parsed;
  return true;
}

bool paramsContainDateValue(const String &params) {
  String upper = params;
  upper.toUpperCase();
  return upper.indexOf("VALUE=DATE") >= 0;
}

String icsUnescape(const String &value) {
  String out;
  out.reserve(value.length());
  bool escaping = false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (!escaping) {
      if (c == '\\') {
        escaping = true;
        continue;
      }
      out += c;
      continue;
    }
    switch (c) {
      case 'n':
      case 'N':
        out += '\n';
        break;
      case '\\':
        out += '\\';
        break;
      case ',':
        out += ',';
        break;
      case ';':
        out += ';';
        break;
      default:
        out += c;
        break;
    }
    escaping = false;
  }
  if (escaping) {
    out += '\\';
  }
  return out;
}

namespace {

String currentTimezoneEnv() {
  const char *tz = getenv("TZ");
  return (tz == nullptr) ? String("") : String(tz);
}

time_t mktimeWithTimezone(struct tm tm_value, const char *tz_spec) {
  const String previous_tz = currentTimezoneEnv();
  if (tz_spec != nullptr && tz_spec[0] != '\0') {
    setenv("TZ", tz_spec, 1);
  } else {
    unsetenv("TZ");
  }
  tzset();
  const time_t epoch = mktime(&tm_value);
  if (previous_tz.length() > 0) {
    setenv("TZ", previous_tz.c_str(), 1);
  } else {
    unsetenv("TZ");
  }
  tzset();
  return epoch;
}

bool isAsciiDigit(char c) {
  return c >= '0' && c <= '9';
}

int32_t dayIdFromTm(const struct tm &tm_value) {
  return static_cast<int32_t>((tm_value.tm_year + 1900) * 10000 + (tm_value.tm_mon + 1) * 100 +
                              tm_value.tm_mday);
}

String formatDateYmd(const struct tm &tm_value) {
  char buf[20] = {0};
  const unsigned year = static_cast<unsigned>(tm_value.tm_year + 1900);
  const unsigned month = static_cast<unsigned>(tm_value.tm_mon + 1);
  const unsigned day = static_cast<unsigned>(tm_value.tm_mday);
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u", year, month, day);
  return String(buf);
}

String formatTimeHm(const struct tm &tm_value) {
  char buf[8] = {0};
  snprintf(buf, sizeof(buf), "%02d:%02d", tm_value.tm_hour, tm_value.tm_min);
  return String(buf);
}

time_t startOfLocalDay(time_t epoch_value) {
  struct tm day_tm {};
  if (localtime_r(&epoch_value, &day_tm) == nullptr) {
    return epoch_value;
  }
  day_tm.tm_hour = 0;
  day_tm.tm_min = 0;
  day_tm.tm_sec = 0;
  return mktime(&day_tm);
}

time_t localEpochFromDateAndTime(time_t day_epoch, const struct tm &source_tm) {
  struct tm tm_value {};
  if (localtime_r(&day_epoch, &tm_value) == nullptr) {
    return day_epoch;
  }
  tm_value.tm_hour = source_tm.tm_hour;
  tm_value.tm_min = source_tm.tm_min;
  tm_value.tm_sec = source_tm.tm_sec;
  return mktime(&tm_value);
}

time_t localWeekStartFromEpoch(time_t epoch_value) {
  const time_t day_start = startOfLocalDay(epoch_value);
  struct tm day_tm {};
  if (localtime_r(&day_start, &day_tm) == nullptr) {
    return day_start;
  }
  day_tm.tm_mday -= weekdayMon0FromTm(day_tm);
  return mktime(&day_tm);
}

bool shouldEmitDailyOccurrence(const ParsedIcsEvent &event, const CalendarRruleCore &rrule,
                               time_t candidate_start) {
  if (candidate_start < event.start_epoch) {
    return false;
  }
  if (rrule.until_valid && candidate_start > rrule.until_epoch) {
    return false;
  }
  const long delta_days =
      static_cast<long>((startOfLocalDay(candidate_start) - startOfLocalDay(event.start_epoch)) /
                        86400);
  if (delta_days < 0 || (delta_days % static_cast<long>(rrule.interval)) != 0) {
    return false;
  }
  if (rrule.count > 0) {
    const long occurrence_index = delta_days / static_cast<long>(rrule.interval);
    if (occurrence_index >= static_cast<long>(rrule.count)) {
      return false;
    }
  }
  return true;
}

bool shouldEmitWeeklyOccurrence(const ParsedIcsEvent &event, const CalendarRruleCore &rrule,
                                time_t candidate_start) {
  if (candidate_start < event.start_epoch) {
    return false;
  }
  if (rrule.until_valid && candidate_start > rrule.until_epoch) {
    return false;
  }

  struct tm candidate_tm {};
  if (localtime_r(&candidate_start, &candidate_tm) == nullptr) {
    return false;
  }
  uint8_t byday_mask = rrule.byday_mask;
  if (byday_mask == 0u) {
    byday_mask = weekdayMaskBit(weekdayMon0FromTm(event.start_tm));
  }
  if ((byday_mask & weekdayMaskBit(weekdayMon0FromTm(candidate_tm))) == 0u) {
    return false;
  }

  const long week_delta = static_cast<long>(
      (localWeekStartFromEpoch(candidate_start) - localWeekStartFromEpoch(event.start_epoch)) /
      (7 * 86400));
  if (week_delta < 0 || (week_delta % static_cast<long>(rrule.interval)) != 0) {
    return false;
  }
  return true;
}

bool findOverride(const std::vector<IcsOverride> &overrides, const String &uid,
                  const IcsOccurrenceKey &needle, bool &cancelled) {
  for (const IcsOverride &item : overrides) {
    if (item.uid == uid && occurrenceKeysEqual(item.key, needle)) {
      cancelled = item.cancelled;
      return true;
    }
  }
  cancelled = false;
  return false;
}

bool importOccurrence(const ParsedIcsEvent &source_event, time_t occurrence_start,
                      time_t occurrence_end, std::vector<ImportedCalendarEvent> &out_items) {
  if (out_items.size() >= static_cast<size_t>(kMaxCalendarEvents * 4)) {
    return false;
  }

  struct tm start_tm {};
  struct tm end_tm {};
  if (localtime_r(&occurrence_start, &start_tm) == nullptr ||
      localtime_r(&occurrence_end, &end_tm) == nullptr) {
    return false;
  }

  ImportedCalendarEvent imported;
  imported.sort_epoch = occurrence_start;
  imported.event.title =
      buildImportedTitle(source_event.summary, source_event.location, source_event.description);
  imported.event.date = formatDateYmd(start_tm);
  imported.event.time_hhmm = source_event.all_day ? String("00:00") : formatTimeHm(start_tm);
  if (!source_event.all_day && dayIdFromTm(start_tm) == dayIdFromTm(end_tm) &&
      occurrence_end > occurrence_start) {
    imported.event.end_time_hhmm = formatTimeHm(end_tm);
  } else {
    imported.event.end_time_hhmm = "";
  }
  imported.event.color = "blue";
  imported.event.repeat = "once";
  imported.event.weekday = -1;
  imported.event.source = "ics";
  String external_id = source_event.uid;
  external_id += "#";
  external_id += imported.event.date;
  external_id += "T";
  external_id += imported.event.time_hhmm;
  imported.event.external_id = external_id;
  out_items.push_back(imported);
  return true;
}

}  // namespace

bool parseIcsDateTime(const String &raw_value, const String &params, bool device_local_time,
                      time_t &epoch_out, struct tm &local_tm_out, bool &all_day_out) {
  String value = raw_value;
  value.trim();
  if (value.length() < 8) {
    return false;
  }

  const bool all_day = paramsContainDateValue(params) || value.indexOf('T') < 0;
  all_day_out = all_day;

  int year = 0;
  int month = 0;
  int day = 0;
  if (!parseDigits(value, 0, 4, year) || !parseDigits(value, 4, 2, month) ||
      !parseDigits(value, 6, 2, day)) {
    return false;
  }

  struct tm tm_value {};
  tm_value.tm_year = year - 1900;
  tm_value.tm_mon = month - 1;
  tm_value.tm_mday = day;
  tm_value.tm_hour = 0;
  tm_value.tm_min = 0;
  tm_value.tm_sec = 0;
  tm_value.tm_isdst = -1;

  bool use_utc = false;
  if (!all_day) {
    const int t_pos = value.indexOf('T');
    if (t_pos < 0 || (t_pos + 5) >= static_cast<int>(value.length())) {
      return false;
    }
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (!parseDigits(value, t_pos + 1, 2, hour) || !parseDigits(value, t_pos + 3, 2, minute)) {
      return false;
    }
    if ((t_pos + 7) < static_cast<int>(value.length()) &&
        isAsciiDigit(value[t_pos + 5]) && isAsciiDigit(value[t_pos + 6])) {
      if (!parseDigits(value, t_pos + 5, 2, second)) {
        second = 0;
      }
    }
    tm_value.tm_hour = hour;
    tm_value.tm_min = minute;
    tm_value.tm_sec = second;
    use_utc = value.endsWith("Z");
  }

  const time_t epoch = use_utc ? mktimeWithTimezone(tm_value, "UTC0") : mktime(&tm_value);
  if (epoch <= 0 && !all_day) {
    return false;
  }
  epoch_out = epoch;
  if (device_local_time) {
    if (localtime_r(&epoch_out, &local_tm_out) == nullptr) {
      return false;
    }
  } else {
    local_tm_out = tm_value;
  }
  return true;
}

bool parseIcsDateField(const String &raw_value, const String &params, time_t &epoch_out,
                       struct tm &tm_out, bool &all_day_out) {
  String upper_params = params;
  upper_params.toUpperCase();
  const bool treat_as_floating = upper_params.indexOf("TZID=") >= 0 && !raw_value.endsWith("Z");
  return parseIcsDateTime(raw_value, params, !treat_as_floating, epoch_out, tm_out, all_day_out);
}

void appendUnfoldedIcsLines(const String &body, std::vector<String> &lines) {
  lines.clear();
  String current;
  for (size_t i = 0; i < body.length(); ++i) {
    const char c = body[i];
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if ((current.startsWith(" ") || current.startsWith("\t")) && !lines.empty()) {
        lines.back() += current.substring(1);
      } else {
        lines.push_back(current);
      }
      current = "";
      continue;
    }
    current += c;
  }
  if (current.length() > 0) {
    if ((current.startsWith(" ") || current.startsWith("\t")) && !lines.empty()) {
      lines.back() += current.substring(1);
    } else {
      lines.push_back(current);
    }
  }
}

bool splitIcsProperty(const String &line, String &name, String &params, String &value) {
  const int colon = line.indexOf(':');
  if (colon <= 0) {
    return false;
  }
  const String left = line.substring(0, colon);
  value = line.substring(colon + 1);
  const int semi = left.indexOf(';');
  if (semi >= 0) {
    name = left.substring(0, semi);
    params = left.substring(semi + 1);
  } else {
    name = left;
    params = "";
  }
  name.trim();
  name.toUpperCase();
  return name.length() > 0;
}

bool parseRruleCore(const String &raw_rrule, CalendarRruleCore &rrule) {
  rrule = CalendarRruleCore{};
  int start = 0;
  while (start < static_cast<int>(raw_rrule.length())) {
    int end = raw_rrule.indexOf(';', start);
    if (end < 0) {
      end = static_cast<int>(raw_rrule.length());
    }
    const String token = raw_rrule.substring(start, end);
    const int eq = token.indexOf('=');
    if (eq > 0) {
      String key = token.substring(0, eq);
      String value = token.substring(eq + 1);
      key.trim();
      value.trim();
      key.toUpperCase();
      value.toUpperCase();
      if (key == "FREQ") {
        rrule.freq = value;
      } else if (key == "INTERVAL") {
        const int parsed = value.toInt();
        if (parsed > 0) {
          rrule.interval = static_cast<uint16_t>(parsed);
        }
      } else if (key == "COUNT") {
        const int parsed = value.toInt();
        if (parsed > 0) {
          rrule.count = static_cast<uint16_t>(parsed);
        }
      } else if (key == "UNTIL") {
        time_t until_epoch = 0;
        struct tm until_tm {};
        bool all_day = false;
        if (parseIcsDateTime(value, "", true, until_epoch, until_tm, all_day)) {
          rrule.until_valid = true;
          rrule.until_epoch = until_epoch;
        }
      } else if (key == "BYDAY") {
        int part_start = 0;
        while (part_start < static_cast<int>(value.length())) {
          int part_end = value.indexOf(',', part_start);
          if (part_end < 0) {
            part_end = static_cast<int>(value.length());
          }
          int weekday = -1;
          if (parseByDayToken(value.substring(part_start, part_end), weekday)) {
            rrule.byday_mask = static_cast<uint8_t>(rrule.byday_mask | weekdayMaskBit(weekday));
          }
          part_start = part_end + 1;
        }
      }
    }
    start = end + 1;
  }
  return rrule.freq == "DAILY" || rrule.freq == "WEEKLY";
}

int weekdayMon0FromTm(const struct tm &tm_value) {
  return (tm_value.tm_wday + 6) % 7;
}

time_t localWeekWindowStart(time_t now_epoch) {
  struct tm local_tm {};
  if (localtime_r(&now_epoch, &local_tm) == nullptr) {
    return now_epoch;
  }
  local_tm.tm_hour = 0;
  local_tm.tm_min = 0;
  local_tm.tm_sec = 0;
  local_tm.tm_mday -= weekdayMon0FromTm(local_tm);
  return mktime(&local_tm);
}

time_t localWindowEndOneMonth(time_t window_start) {
  struct tm local_tm {};
  if (localtime_r(&window_start, &local_tm) == nullptr) {
    return window_start + 31 * 86400;
  }
  local_tm.tm_mon += 1;
  return mktime(&local_tm);
}

bool eventOverlapsWindow(time_t start_epoch, time_t end_epoch, time_t window_start,
                         time_t window_end) {
  if (end_epoch <= start_epoch) {
    end_epoch = start_epoch + 60;
  }
  return start_epoch < window_end && end_epoch > window_start;
}

String trimDisplayField(const String &raw, size_t max_len) {
  String value = icsUnescape(raw);
  value.replace("\r", " ");
  value.replace("\n", " / ");
  value.trim();
  if (value.length() > max_len) {
    value = value.substring(0, max_len);
  }
  return value;
}

String buildImportedTitle(const String &summary, const String &location, const String &description) {
  String title = trimDisplayField(summary, 32);
  String trimmed_location = trimDisplayField(location, 18);
  String trimmed_description = trimDisplayField(description, 18);
  if (title.length() == 0) {
    title = (trimmed_location.length() > 0) ? trimmed_location : trimmed_description;
  }
  if (title.length() == 0) {
    title = "Busy";
  }
  if (trimmed_location.length() > 0 && title.indexOf(trimmed_location.c_str()) < 0) {
    title += " @";
    title += trimmed_location;
  } else if (trimmed_description.length() > 0 &&
             title.indexOf(trimmed_description.c_str()) < 0 &&
             title.length() < 24) {
    title += " - ";
    title += trimmed_description;
  }
  if (title.length() > 32) {
    title = title.substring(0, 32);
  }
  return title;
}

IcsOccurrenceKey buildOccurrenceKey(const struct tm &tm_value, bool all_day, time_t epoch_value) {
  IcsOccurrenceKey key;
  key.valid = true;
  key.all_day = all_day;
  if (all_day) {
    key.day_id = dayIdFromTm(tm_value);
  } else {
    key.minute_epoch = static_cast<int64_t>(epoch_value / 60);
  }
  return key;
}

bool occurrenceKeysEqual(const IcsOccurrenceKey &a, const IcsOccurrenceKey &b) {
  if (!a.valid || !b.valid || a.all_day != b.all_day) {
    return false;
  }
  return a.all_day ? (a.day_id == b.day_id) : (a.minute_epoch == b.minute_epoch);
}

bool hasOccurrenceKey(const std::vector<IcsOccurrenceKey> &keys, const IcsOccurrenceKey &needle) {
  for (const IcsOccurrenceKey &key : keys) {
    if (occurrenceKeysEqual(key, needle)) {
      return true;
    }
  }
  return false;
}

bool parseIcsEventFromLines(const std::vector<String> &vevent_lines, ParsedIcsEvent &event) {
  event = ParsedIcsEvent{};
  for (const String &line : vevent_lines) {
    String name;
    String params;
    String value;
    if (!splitIcsProperty(line, name, params, value)) {
      continue;
    }
    if (name == "UID") {
      event.uid = value;
      event.uid.trim();
    } else if (name == "SUMMARY") {
      event.summary = value;
    } else if (name == "DESCRIPTION") {
      event.description = value;
    } else if (name == "LOCATION") {
      event.location = value;
    } else if (name == "STATUS") {
      event.status = value;
      event.status.trim();
      event.status.toUpperCase();
    } else if (name == "RRULE") {
      event.rrule = value;
      event.rrule.trim();
    } else if (name == "DTSTART") {
      event.start_valid =
          parseIcsDateField(value, params, event.start_epoch, event.start_tm, event.all_day);
    } else if (name == "DTEND") {
      bool end_all_day = false;
      event.end_valid =
          parseIcsDateField(value, params, event.end_epoch, event.end_tm, end_all_day);
    } else if (name == "RECURRENCE-ID") {
      bool recur_all_day = false;
      time_t recur_epoch = 0;
      struct tm recur_tm {};
      if (parseIcsDateField(value, params, recur_epoch, recur_tm, recur_all_day)) {
        event.recurrence_id_valid = true;
        event.recurrence_id = buildOccurrenceKey(recur_tm, recur_all_day, recur_epoch);
      }
    } else if (name == "EXDATE") {
      int start = 0;
      while (start < static_cast<int>(value.length())) {
        int end = value.indexOf(',', start);
        if (end < 0) {
          end = value.length();
        }
        const String part = value.substring(start, end);
        bool ex_all_day = false;
        time_t ex_epoch = 0;
        struct tm ex_tm {};
        if (parseIcsDateField(part, params, ex_epoch, ex_tm, ex_all_day)) {
          event.exdates.push_back(buildOccurrenceKey(ex_tm, ex_all_day, ex_epoch));
        }
        start = end + 1;
      }
    }
  }

  if (!event.start_valid || event.uid.length() == 0) {
    return false;
  }
  if (!event.end_valid) {
    event.end_epoch = event.start_epoch + (event.all_day ? 86400 : 3600);
    localtime_r(&event.end_epoch, &event.end_tm);
    event.end_valid = true;
  }
  if (event.end_epoch <= event.start_epoch) {
    event.end_epoch = event.start_epoch + (event.all_day ? 86400 : 3600);
    localtime_r(&event.end_epoch, &event.end_tm);
  }
  return true;
}

void parseIcsBodyIntoEvents(const String &body, std::vector<ParsedIcsEvent> &masters,
                            std::vector<ParsedIcsEvent> &overrides, size_t &vevent_count) {
  masters.clear();
  overrides.clear();
  vevent_count = 0;

  std::vector<String> lines;
  appendUnfoldedIcsLines(body, lines);

  bool in_vevent = false;
  std::vector<String> vevent_lines;
  for (const String &line : lines) {
    if (line == "BEGIN:VEVENT") {
      in_vevent = true;
      vevent_lines.clear();
      continue;
    }
    if (line == "END:VEVENT") {
      if (in_vevent) {
        ParsedIcsEvent event;
        if (parseIcsEventFromLines(vevent_lines, event)) {
          ++vevent_count;
          if (event.recurrence_id_valid) {
            overrides.push_back(event);
          } else {
            masters.push_back(event);
          }
        }
      }
      in_vevent = false;
      vevent_lines.clear();
      continue;
    }
    if (in_vevent) {
      vevent_lines.push_back(line);
    }
  }
}

void collectOverrideMetadata(const std::vector<ParsedIcsEvent> &override_events,
                             std::vector<IcsOverride> &override_metadata) {
  override_metadata.clear();
  for (const ParsedIcsEvent &event : override_events) {
    if (!event.recurrence_id_valid) {
      continue;
    }
    IcsOverride override_item;
    override_item.uid = event.uid;
    override_item.key = event.recurrence_id;
    override_item.cancelled = (event.status == "CANCELLED");
    override_metadata.push_back(override_item);
  }
}

void expandSingleEvent(const ParsedIcsEvent &event, time_t window_start, time_t window_end,
                       std::vector<ImportedCalendarEvent> &out_items) {
  if (event.status == "CANCELLED") {
    return;
  }
  if (eventOverlapsWindow(event.start_epoch, event.end_epoch, window_start, window_end)) {
    importOccurrence(event, event.start_epoch, event.end_epoch, out_items);
  }
}

void expandRecurringEvent(const ParsedIcsEvent &event, const CalendarRruleCore &rrule,
                          const std::vector<IcsOverride> &override_metadata, time_t window_start,
                          time_t window_end, std::vector<ImportedCalendarEvent> &out_items) {
  const time_t duration =
      std::max<time_t>(event.end_epoch - event.start_epoch, event.all_day ? 86400 : 60);
  time_t day_cursor = startOfLocalDay(std::max(window_start, event.start_epoch));
  if (day_cursor > window_start) {
    day_cursor -= 86400;
  }
  for (; day_cursor < window_end; day_cursor += 86400) {
    time_t occurrence_start =
        event.all_day ? day_cursor : localEpochFromDateAndTime(day_cursor, event.start_tm);
    if (occurrence_start < event.start_epoch) {
      continue;
    }

    bool should_emit = false;
    if (rrule.freq == "DAILY") {
      should_emit = shouldEmitDailyOccurrence(event, rrule, occurrence_start);
    } else if (rrule.freq == "WEEKLY") {
      should_emit = shouldEmitWeeklyOccurrence(event, rrule, occurrence_start);
    }
    if (!should_emit) {
      continue;
    }

    struct tm occurrence_tm {};
    if (localtime_r(&occurrence_start, &occurrence_tm) == nullptr) {
      continue;
    }
    const IcsOccurrenceKey occurrence_key =
        buildOccurrenceKey(occurrence_tm, event.all_day, occurrence_start);
    if (hasOccurrenceKey(event.exdates, occurrence_key)) {
      continue;
    }
    bool cancelled = false;
    if (findOverride(override_metadata, event.uid, occurrence_key, cancelled)) {
      continue;
    }

    const time_t occurrence_end = occurrence_start + duration;
    if (!eventOverlapsWindow(occurrence_start, occurrence_end, window_start, window_end)) {
      continue;
    }
    importOccurrence(event, occurrence_start, occurrence_end, out_items);
  }
}

void appendOverrideEvents(const std::vector<ParsedIcsEvent> &override_events, time_t window_start,
                          time_t window_end, std::vector<ImportedCalendarEvent> &out_items) {
  for (const ParsedIcsEvent &event : override_events) {
    if (event.status == "CANCELLED") {
      continue;
    }
    if (eventOverlapsWindow(event.start_epoch, event.end_epoch, window_start, window_end)) {
      importOccurrence(event, event.start_epoch, event.end_epoch, out_items);
    }
  }
}

void sortImportedEvents(std::vector<ImportedCalendarEvent> &items) {
  std::sort(items.begin(), items.end(), [](const ImportedCalendarEvent &a,
                                           const ImportedCalendarEvent &b) {
    if (a.sort_epoch != b.sort_epoch) {
      return a.sort_epoch < b.sort_epoch;
    }
    return strcmp(a.event.title.c_str(), b.event.title.c_str()) < 0;
  });
}

uint8_t weekdayMaskBit(int weekday_mon0) {
  if (weekday_mon0 < 0 || weekday_mon0 > 6) {
    return 0u;
  }
  return static_cast<uint8_t>(1u << weekday_mon0);
}

bool parseByDayToken(const String &token, int &weekday_mon0) {
  String upper = token;
  upper.trim();
  upper.toUpperCase();
  if (upper.endsWith("MO")) {
    weekday_mon0 = 0;
  } else if (upper.endsWith("TU")) {
    weekday_mon0 = 1;
  } else if (upper.endsWith("WE")) {
    weekday_mon0 = 2;
  } else if (upper.endsWith("TH")) {
    weekday_mon0 = 3;
  } else if (upper.endsWith("FR")) {
    weekday_mon0 = 4;
  } else if (upper.endsWith("SA")) {
    weekday_mon0 = 5;
  } else if (upper.endsWith("SU")) {
    weekday_mon0 = 6;
  } else {
    return false;
  }
  return true;
}

}  // namespace appfw
