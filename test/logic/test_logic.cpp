#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <vector>

#include "app/RefreshPolicy.h"
#include "calendar/CalendarLogic.h"
#include "system/CalendarStore.h"
#include "system/CalendarSettings.h"
#include "system/CalendarEventNormalize.h"
#include "system/CalendarIcsCore.h"

namespace {

void assertRefreshPlan(const char *scenario, bool calendar_page, bool daily_sync_due,
                       bool has_sta_credentials, appfw::RefreshSyncPlan expected) {
  const appfw::RefreshSyncPlan actual = appfw::selectRefreshSyncPlan(
      calendar_page, daily_sync_due, has_sta_credentials);
  printf("[TEST][REFRESH] scenario=%s page=%s daily_due=%s sta_credentials=%s "
         "plan=%s result=%s\n",
         scenario, calendar_page ? "calendar" : "photo",
         daily_sync_due ? "true" : "false",
         has_sta_credentials ? "true" : "false",
         appfw::refreshSyncPlanName(actual), actual == expected ? "PASS" : "FAIL");
  assert(actual == expected);
}

void testRefreshPolicy() {
  struct tm a {};
  a.tm_year = 126;
  a.tm_yday = 104;
  a.tm_hour = 9;
  a.tm_min = 30;

  struct tm b = a;
  assert(appfw::sameCalendarMinute(a, b));
  b.tm_min = 31;
  assert(!appfw::sameCalendarMinute(a, b));

  const int32_t minute_key = appfw::minuteKeyFromTm(a);
  assert(appfw::refreshBucketKey(minute_key, 600u) == (minute_key / 10));
  assert(appfw::refreshBucketKey(minute_key, 0u) == -1);
  assert(appfw::dayKeyFromTm(a) == 2026104);
  struct tm before_wake {};
  before_wake.tm_year = 126;
  before_wake.tm_mon = 3;
  before_wake.tm_mday = 15;
  before_wake.tm_hour = 7;
  before_wake.tm_min = 59;
  before_wake.tm_isdst = -1;
  mktime(&before_wake);
  struct tm at_wake = before_wake;
  at_wake.tm_hour = 8;
  at_wake.tm_min = 0;
  mktime(&at_wake);
  assert(appfw::dailySyncCycleKey(before_wake, 8 * 60) == 2026103);
  assert(appfw::dailySyncCycleKey(at_wake, 8 * 60) == 2026104);
  struct tm new_year_before_wake {};
  new_year_before_wake.tm_year = 126;
  new_year_before_wake.tm_mon = 0;
  new_year_before_wake.tm_mday = 1;
  new_year_before_wake.tm_hour = 7;
  new_year_before_wake.tm_min = 59;
  new_year_before_wake.tm_isdst = -1;
  mktime(&new_year_before_wake);
  assert(appfw::dailySyncCycleKey(new_year_before_wake, 8 * 60) == 2025364);

  assertRefreshPlan("daily_photo_with_wifi", false, true, true,
                    appfw::RefreshSyncPlan::SyncBeforeRender);
  assertRefreshPlan("daily_calendar_with_wifi", true, true, true,
                    appfw::RefreshSyncPlan::SyncBeforeRender);
  assertRefreshPlan("regular_photo_with_wifi", false, false, true,
                    appfw::RefreshSyncPlan::RenderOnly);
  assertRefreshPlan("regular_calendar_with_wifi", true, false, true,
                    appfw::RefreshSyncPlan::RenderThenSync);
  assertRefreshPlan("regular_calendar_without_wifi", true, false, false,
                    appfw::RefreshSyncPlan::RenderOnly);
  assertRefreshPlan("daily_photo_without_wifi", false, true, false,
                    appfw::RefreshSyncPlan::SyncBeforeRender);

  const time_t fallback = appfw::fallbackClockBaseEpoch();
  assert(fallback > 0);
}

void testCalendarLogicMatchesToday() {
  assert(calendar::calendarEventMatchesToday("daily", -1, "2026-04-15", "2026-04-15", 2));
  assert(calendar::calendarEventMatchesToday("weekly", 2, "2026-04-14", "2026-04-15", 2));
  assert(!calendar::calendarEventMatchesToday("weekly", 4, "2026-04-15", "2026-04-15", 2));
  assert(calendar::calendarEventMatchesToday("once", -1, "2026-04-15", "2026-04-15", 2));
  assert(!calendar::calendarEventMatchesToday("once", -1, "2026-04-16", "2026-04-15", 2));
}

void testCalendarLogicLanes() {
  calendar::TimelineEventSlot events[4] = {};
  events[0].start_minute = 540;
  events[0].end_minute = 600;
  events[1].start_minute = 570;
  events[1].end_minute = 630;
  events[2].start_minute = 660;
  events[2].end_minute = 720;
  events[3].start_minute = 675;
  events[3].end_minute = 700;
  calendar::assignTimelineLanes(events, 4, 24);

  assert(events[0].lane == 0);
  assert(events[1].lane == 1);
  assert(events[0].lane_count == 2);
  assert(events[1].lane_count == 2);
  assert(events[2].lane == 0);
  assert(events[3].lane == 1);
  assert(events[2].lane_count == 2);
  assert(events[3].lane_count == 2);
}

void testCalendarTimelinePeriods() {
  calendar::TimelineSegment segment;
  assert(calendar::clipTimelineSegment(13u * 60u + 30u, 14u * 60u + 30u,
                                       8u * 60u, 14u * 60u, segment));
  assert(segment.start_minute == 13u * 60u + 30u);
  assert(segment.end_minute == 14u * 60u);
  assert(!segment.continues_before);
  assert(segment.continues_after);

  assert(calendar::clipTimelineSegment(13u * 60u + 30u, 14u * 60u + 30u,
                                       14u * 60u, 22u * 60u, segment));
  assert(segment.start_minute == 14u * 60u);
  assert(segment.end_minute == 14u * 60u + 30u);
  assert(segment.continues_before);
  assert(!segment.continues_after);

  assert(!calendar::clipTimelineSegment(7u * 60u, 8u * 60u,
                                        8u * 60u, 14u * 60u, segment));
  assert(calendar::timelineOffsetForMinute(11u * 60u, 8u * 60u, 14u * 60u, 360u) == 180u);
  assert(calendar::timelineOffsetForMinute(18u * 60u, 14u * 60u, 22u * 60u, 400u) == 200u);
}

void testCalendarSettings() {
  assert(appfw::normalizeSleepWindowMinute(0u, 60u) == 0u);
  assert(appfw::normalizeSleepWindowMinute(1439u, 60u) == 1439u);
  assert(appfw::normalizeSleepWindowMinute(1440u, 60u) == 60u);
}

void testCalendarEventNormalize() {
  String normalized;
  assert(appfw::normalizeCalendarTimeValue("09:30", normalized));
  assert(normalized == "09:30");
  assert(!appfw::normalizeCalendarTimeValue("9:30", normalized));

  assert(appfw::normalizeCalendarDateValue("2026-04-15", normalized));
  assert(normalized == "2026-04-15");
  assert(appfw::truncateCalendarUtf8Value("123456", 4) == "1234");
  assert(appfw::truncateCalendarUtf8Value("\xE5\x9C\xB0\xE5\x9D\x80", 4) == "\xE5\x9C\xB0");
  assert(!appfw::normalizeCalendarDateValue("2026-02-30", normalized));

  assert(appfw::normalizeCalendarColorValue("RED") == "red");
  assert(appfw::normalizeCalendarRepeatValue("DAILY") == "daily");
  assert(appfw::normalizeCalendarSourceValue("Outlook#Bad!") == "outlookbad");
  assert(appfw::normalizeCalendarExternalIdValue("  abc  ") == "abc");
  assert(appfw::normalizeCalendarUpdatedAtValue(" 123 ") == "123");
}

void testCalendarIcsCore() {
  int parsed = 0;
  assert(appfw::parseDigits("20260415", 0, 4, parsed) && parsed == 2026);
  assert(!appfw::parseDigits("20A6", 0, 4, parsed));
  assert(appfw::paramsContainDateValue("TZID=Asia/Shanghai;VALUE=DATE"));
  assert(appfw::icsUnescape("Hello\\,World\\nRoom\\;A") == "Hello,World\nRoom;A");

  std::vector<String> lines;
  appfw::appendUnfoldedIcsLines("A:1\r\n B\r\nC:2\r\n", lines);
  assert(lines.size() == 2);
  assert(lines[0] == "A:1B");
  assert(lines[1] == "C:2");

  String name;
  String params;
  String value;
  assert(appfw::splitIcsProperty("DTSTART;VALUE=DATE:20260415", name, params, value));
  assert(name == "DTSTART");
  assert(params == "VALUE=DATE");
  assert(value == "20260415");

  appfw::CalendarRruleCore rrule;
  assert(appfw::parseRruleCore("FREQ=WEEKLY;INTERVAL=2;COUNT=3;BYDAY=MO,WE;UNTIL=20260430T120000Z", rrule));
  assert(rrule.freq == "WEEKLY");
  assert(rrule.interval == 2);
  assert(rrule.count == 3);
  assert(rrule.until_valid);
  assert(rrule.until_epoch > 0);
  assert(rrule.byday_mask == (appfw::weekdayMaskBit(0) | appfw::weekdayMaskBit(2)));

  time_t epoch = 0;
  struct tm tm_value {};
  bool all_day = false;
  assert(appfw::parseIcsDateTime("20260415", "VALUE=DATE", false, epoch, tm_value, all_day));
  assert(all_day);
  assert(tm_value.tm_year == (2026 - 1900));
  assert(tm_value.tm_mon == 3);
  assert(tm_value.tm_mday == 15);

  assert(appfw::parseIcsDateTime("20260415T123000Z", "", false, epoch, tm_value, all_day));
  assert(!all_day);
  assert(appfw::parseIcsDateField("20260415T123000", "TZID=Asia/Shanghai", epoch, tm_value, all_day));

  tm_value = {};
  tm_value.tm_wday = 3;  // Wednesday
  assert(appfw::weekdayMon0FromTm(tm_value) == 2);

  const time_t week_start = appfw::localWeekWindowStart(epoch);
  const time_t window_end = appfw::localWindowEndOneMonth(week_start);
  assert(window_end > week_start);

  assert(appfw::eventOverlapsWindow(100, 200, 150, 250));
  assert(!appfw::eventOverlapsWindow(100, 120, 121, 200));
  assert(appfw::trimDisplayField(" Room\\nA ", 20) == "Room / A");
  assert(appfw::trimDisplayField("\\n", 20) == "");
  assert(appfw::buildImportedTitle("", "Office", "") == "Office");
  assert(appfw::buildImportedTitle("Meeting", "Office", "") == "Meeting @Office");
  assert(appfw::buildImportedTitle("test", "test", "\\n") == "test @test");

  assert(appfw::weekdayMaskBit(0) == 1u);
  assert(appfw::weekdayMaskBit(6) == 64u);
  assert(appfw::weekdayMaskBit(7) == 0u);

  int weekday = -1;
  assert(appfw::parseByDayToken("MO", weekday) && weekday == 0);
  assert(appfw::parseByDayToken("2TH", weekday) && weekday == 3);
  assert(!appfw::parseByDayToken("XX", weekday));

  std::vector<String> vevent_lines = {
      "UID:test-uid",
      "SUMMARY:Weekly Sync",
      "DESCRIPTION:Room\\nA",
      "LOCATION:Office",
      "RRULE:FREQ=WEEKLY;BYDAY=MO,WE",
      "DTSTART:20260415T090000",
      "DTEND:20260415T100000",
      "EXDATE:20260422T090000",
  };
  appfw::ParsedIcsEvent event;
  assert(appfw::parseIcsEventFromLines(vevent_lines, event));
  assert(event.uid == "test-uid");
  assert(event.summary == "Weekly Sync");
  assert(event.start_valid);
  assert(event.end_valid);
  assert(event.rrule == "FREQ=WEEKLY;BYDAY=MO,WE");
  assert(event.exdates.size() == 1);
  const appfw::IcsOccurrenceKey exdate_key =
      appfw::buildOccurrenceKey(event.start_tm, false, event.start_epoch + 7 * 86400);
  assert(appfw::hasOccurrenceKey(event.exdates, event.exdates[0]));
  assert(!appfw::occurrenceKeysEqual(event.recurrence_id, exdate_key));

  const String body =
      "BEGIN:VCALENDAR\r\n"
      "BEGIN:VEVENT\r\n"
      "UID:master-1\r\n"
      "SUMMARY:Standup\r\n"
      "RRULE:FREQ=WEEKLY;BYDAY=WE\r\n"
      "DTSTART:20260415T090000\r\n"
      "DTEND:20260415T093000\r\n"
      "EXDATE:20260422T090000\r\n"
      "END:VEVENT\r\n"
      "BEGIN:VEVENT\r\n"
      "UID:master-1\r\n"
      "RECURRENCE-ID:20260429T090000\r\n"
      "SUMMARY:Standup Override\r\n"
      "DTSTART:20260429T100000\r\n"
      "DTEND:20260429T103000\r\n"
      "END:VEVENT\r\n"
      "END:VCALENDAR\r\n";
  std::vector<appfw::ParsedIcsEvent> masters;
  std::vector<appfw::ParsedIcsEvent> overrides;
  size_t vevent_count = 0;
  appfw::parseIcsBodyIntoEvents(body, masters, overrides, vevent_count);
  assert(vevent_count == 2);
  assert(masters.size() == 1);
  assert(overrides.size() == 1);

  std::vector<appfw::IcsOverride> override_metadata;
  appfw::collectOverrideMetadata(overrides, override_metadata);
  assert(override_metadata.size() == 1);
  assert(override_metadata[0].uid == "master-1");

  appfw::CalendarRruleCore weekly;
  assert(appfw::parseRruleCore(masters[0].rrule, weekly));
  std::vector<appfw::ImportedCalendarEvent> imported;
  const time_t expand_window_start = masters[0].start_epoch;
  const time_t expand_window_end = masters[0].start_epoch + (21 * 86400);
  appfw::expandRecurringEvent(masters[0], weekly, override_metadata, expand_window_start, expand_window_end,
                              imported);
  appfw::appendOverrideEvents(overrides, expand_window_start, expand_window_end, imported);
  appfw::sortImportedEvents(imported);
  assert(imported.size() == 2);
  assert(imported[0].event.title == "Standup");
  assert(imported[1].event.title == "Standup Override");
  assert(imported[0].event.date == "2026-04-15");
  assert(imported[1].event.time_hhmm == "10:00");
}

void testCalendarStore() {
  appfw::CalendarStore store;
  appfw::CalendarEvent event;
  event.id = store.allocateId();
  event.title = "Morning";
  event.date = "2026-04-15";
  event.time_hhmm = "09:00";
  event.end_time_hhmm = "10:00";
  event.color = "blue";
  event.repeat = "once";
  event.weekday = -1;
  event.source = "manual";
  event.external_id = "";
  event.updated_at = "1";
  assert(store.push(event));

  assert(store.count() == 1);
  assert(store.findIndexById(event.id) == 0);

  const String packed = store.serialize();
  assert(packed.indexOf("Morning") >= 0);
  const String json = store.toJson();
  assert(json.indexOf("\"ok\":true") >= 0);

  appfw::CalendarStore restored;
  restored.deserialize(packed);
  assert(restored.count() == 1);
  appfw::CalendarEvent restored_event;
  assert(restored.eventAt(0, restored_event));
  assert(restored_event.title == "Morning");

  assert(store.removeAt(0));
}

}  // namespace

int main() {
  testRefreshPolicy();
  testCalendarLogicMatchesToday();
  testCalendarLogicLanes();
  testCalendarTimelinePeriods();
  testCalendarSettings();
  testCalendarEventNormalize();
  testCalendarIcsCore();
  testCalendarStore();
  return 0;
}
