#include <assert.h>
#include <stdint.h>
#include <time.h>
#include <vector>

#include "app/RefreshPolicy.h"
#include "app/CalendarRefreshPlanner.h"
#include "calendar/CalendarLogic.h"
#include "display/PartialRefreshGeometry.h"
#include "system/CalendarStore.h"
#include "system/CalendarSyncService.h"
#include "system/CalendarSettings.h"
#include "system/CalendarEventNormalize.h"
#include "system/CalendarIcsCore.h"

namespace {

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

  const time_t fallback = appfw::fallbackClockBaseEpoch();
  assert(fallback > 0);
}

void testCalendarRefreshPlanner() {
  appfw::CalendarRefreshInputs inputs;
  inputs.force_full_refresh = true;
  inputs.full_screen_rect = calendar::makeRect(0, 0, 800, 480);
  inputs.header_time_rect = calendar::makeRect(8, 32, 80, 32);
  appfw::CalendarRefreshPlan plan = appfw::planCalendarRefresh(inputs);
  assert(plan.mode == appfw::CalendarRefreshMode::Full);
  assert(plan.reason == appfw::CalendarRefreshReason::ForcedFull);
  assert(plan.dirty.full_screen);
  assert(plan.dirty.count == 1);
  assert(plan.dirty.kinds[0] == appfw::CalendarDirtyRegionKind::FullScreen);

  inputs = appfw::CalendarRefreshInputs{};
  inputs.full_screen_rect = calendar::makeRect(0, 0, 800, 480);
  inputs.partial_refresh_count = 7;
  inputs.partial_before_full = 7;
  plan = appfw::planCalendarRefresh(inputs);
  assert(plan.mode == appfw::CalendarRefreshMode::Full);
  assert(plan.reason == appfw::CalendarRefreshReason::PartialBudgetExceeded);

  inputs = appfw::CalendarRefreshInputs{};
  inputs.time_valid = true;
  inputs.minute_key = 600;
  inputs.last_render_minute_key = 590;
  inputs.time_refresh_sec = 600u;
  inputs.partial_refresh_count = 1;
  inputs.partial_before_full = 7;
  inputs.full_screen_rect = calendar::makeRect(0, 0, 800, 480);
  inputs.header_time_rect = calendar::makeRect(8, 32, 80, 32);
  inputs.header_time_changed = true;
  plan = appfw::planCalendarRefresh(inputs);
  assert(plan.mode == appfw::CalendarRefreshMode::Partial);
  assert(plan.reason == appfw::CalendarRefreshReason::TimeTick);
  assert(plan.time_only_refresh);
  assert(plan.dirty.count == 1);
  assert(plan.dirty.rects[0].x == 8);
  assert(plan.dirty.kinds[0] == appfw::CalendarDirtyRegionKind::HeaderTime);

  inputs = appfw::CalendarRefreshInputs{};
  inputs.full_screen_rect = calendar::makeRect(0, 0, 800, 480);
  inputs.header_weather_rect = calendar::makeRect(420, 8, 180, 32);
  inputs.header_sensors_rect = calendar::makeRect(420, 40, 180, 32);
  inputs.header_weather_changed = true;
  inputs.header_sensors_changed = true;
  plan = appfw::planCalendarRefresh(inputs);
  assert(plan.mode == appfw::CalendarRefreshMode::Partial);
  assert(plan.reason == appfw::CalendarRefreshReason::PartialCompatibilityPath);
  assert(!plan.time_only_refresh);
  assert(plan.dirty.count == 1);
  assert(plan.dirty.kinds[0] == appfw::CalendarDirtyRegionKind::FullScreen);

  inputs = appfw::CalendarRefreshInputs{};
  inputs.time_valid = true;
  inputs.minute_key = 601;
  inputs.last_render_minute_key = 600;
  inputs.time_refresh_sec = 600u;
  inputs.partial_refresh_count = 1;
  inputs.partial_before_full = 7;
  inputs.full_screen_rect = calendar::makeRect(0, 0, 800, 480);
  plan = appfw::planCalendarRefresh(inputs);
  assert(plan.mode == appfw::CalendarRefreshMode::Partial);
  assert(plan.reason == appfw::CalendarRefreshReason::PartialCompatibilityPath);
  assert(plan.dirty.full_screen);
}

void testPartialRefreshGeometry() {
  uint16_t x = 5;
  uint16_t width = 7;
  partial_refresh::normalizePartialWindow(800, x, width);
  assert(x == 4);
  assert(width == 8);

  x = 799;
  width = 8;
  partial_refresh::normalizePartialWindow(800, x, width);
  assert(x == 796);
  assert(width == 4);

  x = 800;
  width = 8;
  partial_refresh::normalizePartialWindow(800, x, width);
  assert(width == 0);
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

void testCalendarSettings() {
  assert(appfw::normalizeCalendarTimeRefreshSec(0u) == 0u);
  assert(appfw::normalizeCalendarTimeRefreshSec(600u) == 600u);
  assert(appfw::normalizeCalendarTimeRefreshSec(1200u) == 1200u);
  assert(appfw::normalizeCalendarTimeRefreshSec(61u) == 600u);
}

void testCalendarEventNormalize() {
  String normalized;
  assert(appfw::normalizeCalendarTimeValue("09:30", normalized));
  assert(normalized == "09:30");
  assert(!appfw::normalizeCalendarTimeValue("9:30", normalized));

  assert(appfw::normalizeCalendarDateValue("2026-04-15", normalized));
  assert(normalized == "2026-04-15");
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
  assert(appfw::buildImportedTitle("", "Office", "") == "Office");
  assert(appfw::buildImportedTitle("Meeting", "Office", "") == "Meeting @Office");

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

  appfw::CalendarEvent imported = event;
  imported.id = store.allocateId();
  imported.source = "ics";
  imported.external_id = "uid#2026-04-15T09:00";
  imported.title = "Imported";
  assert(store.push(imported));

  assert(store.count() == 2);
  assert(store.findIndexById(event.id) == 0);
  assert(store.findIndexByExternal("ics", "uid#2026-04-15T09:00") == 1);

  const String packed = store.serialize();
  assert(packed.indexOf("Morning") >= 0);
  const String json = store.toJson();
  assert(json.indexOf("\"ok\":true") >= 0);
  assert(json.indexOf("Imported") >= 0);

  appfw::CalendarStore restored;
  restored.deserialize(packed);
  assert(restored.count() == 2);
  appfw::CalendarEvent restored_event;
  assert(restored.eventAt(1, restored_event));
  assert(restored_event.title == "Imported");

  assert(store.removeAt(0));
  assert(store.count() == 1);
  appfw::CalendarEvent remaining;
  assert(store.eventAt(0, remaining));
  assert(remaining.title == "Imported");
}

void testCalendarSyncMerge() {
  appfw::CalendarStore store;
  appfw::CalendarEvent manual;
  manual.id = store.allocateId();
  manual.title = "Manual";
  manual.source = "manual";
  assert(store.push(manual));

  appfw::CalendarEvent old_ics;
  old_ics.id = store.allocateId();
  old_ics.title = "Old ICS";
  old_ics.source = "ics";
  old_ics.external_id = "uid#1";
  assert(store.push(old_ics));

  std::vector<appfw::CalendarEvent> imported;
  appfw::CalendarEvent new_ics = old_ics;
  new_ics.title = "Updated ICS";
  imported.push_back(new_ics);

  appfw::CalendarEvent fresh_ics;
  fresh_ics.title = "Fresh ICS";
  fresh_ics.source = "ics";
  fresh_ics.external_id = "uid#2";
  imported.push_back(fresh_ics);

  const appfw::CalendarSyncMergeStats stats =
      appfw::CalendarSyncService::mergeImportedEvents(store, imported);
  assert(stats.kept_manual == 1);
  assert(stats.total == 3);

  appfw::CalendarEvent event0;
  appfw::CalendarEvent event1;
  appfw::CalendarEvent event2;
  assert(store.eventAt(0, event0));
  assert(store.eventAt(1, event1));
  assert(store.eventAt(2, event2));
  assert(event0.title == "Manual");
  assert(event1.title == "Updated ICS");
  assert(event1.id == old_ics.id);
  assert(event2.title == "Fresh ICS");
  assert(event2.id != 0);
}

void testCalendarSyncNormalizeImported() {
  std::vector<appfw::ImportedCalendarEvent> imported;

  appfw::ImportedCalendarEvent a;
  a.event.title = "  ";
  a.event.color = "RED";
  a.event.external_id = "  uid#1  ";
  imported.push_back(a);

  appfw::ImportedCalendarEvent b;
  b.event.title = "This title is intentionally longer than thirty two chars";
  b.event.color = "bad";
  b.event.external_id = "uid#2";
  imported.push_back(b);

  const std::vector<appfw::CalendarEvent> normalized =
      appfw::CalendarSyncService::normalizeImportedEvents(imported, 1710000000);
  assert(normalized.size() == 2);
  assert(normalized[0].title == "Busy");
  assert(normalized[0].color == "red");
  assert(normalized[0].repeat == "once");
  assert(normalized[0].weekday == -1);
  assert(normalized[0].source == "ics");
  assert(normalized[0].external_id == "uid#1");
  assert(normalized[0].updated_at == "1710000000");
  assert(normalized[1].title.length() == 32);
  assert(normalized[1].color == "blue");
}

}  // namespace

int main() {
  testRefreshPolicy();
  testCalendarRefreshPlanner();
  testPartialRefreshGeometry();
  testCalendarLogicMatchesToday();
  testCalendarLogicLanes();
  testCalendarSettings();
  testCalendarEventNormalize();
  testCalendarIcsCore();
  testCalendarStore();
  testCalendarSyncNormalizeImported();
  testCalendarSyncMerge();
  return 0;
}
