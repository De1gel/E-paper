#include "system/CalendarSettings.h"

namespace appfw {

uint32_t normalizeCalendarTimeRefreshSec(uint32_t value) {
  switch (value) {
    case 0u:
    case 600u:
    case 1200u:
    case 1800u:
    case 3600u:
      return value;
    default:
      return 600u;
  }
}

}  // namespace appfw
