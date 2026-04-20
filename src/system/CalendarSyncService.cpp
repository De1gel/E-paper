#include "system/CalendarSyncService.h"

#include "system/CalendarEventNormalize.h"

namespace appfw {

std::vector<CalendarEvent> CalendarSyncService::normalizeImportedEvents(
    const std::vector<ImportedCalendarEvent> &imported_items, time_t updated_now) {
  std::vector<CalendarEvent> normalized;
  normalized.reserve(imported_items.size());

  const String updated_at =
      normalizeCalendarUpdatedAtValue((updated_now > 0)
                                          ? String(static_cast<unsigned long>(updated_now))
                                          : String(""));
  for (const ImportedCalendarEvent &item : imported_items) {
    CalendarEvent event = item.event;
    event.title.trim();
    if (event.title.length() == 0) {
      event.title = "Busy";
    } else if (event.title.length() > 32) {
      event.title = event.title.substring(0, 32);
    }
    event.color = normalizeCalendarColorValue(event.color);
    event.repeat = "once";
    event.weekday = -1;
    event.source = "ics";
    event.external_id = normalizeCalendarExternalIdValue(event.external_id);
    event.updated_at = updated_at;
    normalized.push_back(event);
  }

  return normalized;
}

CalendarSyncMergeStats CalendarSyncService::mergeImportedEvents(
    CalendarStore &store, const std::vector<CalendarEvent> &imported) {
  CalendarSyncMergeStats stats;

  CalendarEvent existing_events[kMaxCalendarEvents];
  const size_t existing_count = store.count();
  for (size_t i = 0; i < existing_count; ++i) {
    existing_events[i] = store.data()[i];
  }

  std::vector<CalendarEvent> merged_events;
  merged_events.reserve(kMaxCalendarEvents);

  for (size_t i = 0; i < existing_count; ++i) {
    const CalendarEvent &event = existing_events[i];
    if (event.source != "ics") {
      if (merged_events.size() >= kMaxCalendarEvents) {
        break;
      }
      merged_events.push_back(event);
      ++stats.kept_manual;
    }
  }

  for (const CalendarEvent &raw_event : imported) {
    if (merged_events.size() >= kMaxCalendarEvents) {
      break;
    }
    CalendarEvent event = raw_event;
    const int existing_index = store.findIndexByExternal("ics", event.external_id);
    if (existing_index >= 0) {
      event.id = existing_events[existing_index].id;
    } else {
      event.id = store.allocateId();
    }
    merged_events.push_back(event);
  }

  store.replaceAll(merged_events.data(), merged_events.size());
  stats.total = store.count();
  return stats;
}

}  // namespace appfw
