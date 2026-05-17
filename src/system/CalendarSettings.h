#ifndef SYSTEM_CALENDAR_SETTINGS_H
#define SYSTEM_CALENDAR_SETTINGS_H

#include <stdint.h>

namespace appfw {

uint32_t normalizeCalendarTimeRefreshSec(uint32_t value);
uint16_t normalizeSleepWindowMinute(uint32_t value, uint16_t fallback);

}  // namespace appfw

#endif
