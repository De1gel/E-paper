#include "system/CalendarSettings.h"

namespace appfw {

uint16_t normalizeSleepWindowMinute(uint32_t value, uint16_t fallback) {
  if (value < 1440u) {
    return static_cast<uint16_t>(value);
  }
  return fallback;
}

}  // namespace appfw
