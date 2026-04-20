#ifndef SYSTEM_CALENDAR_SYNC_SERVICE_H
#define SYSTEM_CALENDAR_SYNC_SERVICE_H

#include <vector>

#include "system/CalendarIcsCore.h"
#include "system/CalendarStore.h"

namespace appfw {

struct CalendarSyncMergeStats {
  size_t kept_manual = 0;
  size_t total = 0;
};

class CalendarSyncService {
 public:
  static std::vector<CalendarEvent> normalizeImportedEvents(
      const std::vector<ImportedCalendarEvent> &imported_items, time_t updated_now);
  static CalendarSyncMergeStats mergeImportedEvents(CalendarStore &store,
                                                    const std::vector<CalendarEvent> &imported);
};

}  // namespace appfw

#endif
