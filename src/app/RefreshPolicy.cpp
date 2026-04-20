#include "app/RefreshPolicy.h"

namespace appfw {

int32_t dayKeyFromTm(const struct tm &t) {
  return static_cast<int32_t>((t.tm_year + 1900) * 1000 + t.tm_yday);
}

int32_t minuteKeyFromTm(const struct tm &t) {
  return static_cast<int32_t>(((t.tm_year + 1900) * 400 + t.tm_yday) * 1440 +
                              t.tm_hour * 60 + t.tm_min);
}

int32_t refreshBucketKey(int32_t minute_key, uint32_t interval_sec) {
  if (minute_key < 0 || interval_sec == 0u) {
    return -1;
  }
  const uint32_t interval_min = interval_sec / 60u;
  if (interval_min == 0u) {
    return -1;
  }
  return static_cast<int32_t>(minute_key / static_cast<int32_t>(interval_min));
}

bool sameCalendarMinute(const struct tm &a, const struct tm &b) {
  return a.tm_year == b.tm_year &&
         a.tm_yday == b.tm_yday &&
         a.tm_hour == b.tm_hour &&
         a.tm_min == b.tm_min;
}

time_t fallbackClockBaseEpoch() {
  struct tm fallback_tm {};
  fallback_tm.tm_year = 2026 - 1900;
  fallback_tm.tm_mon = 0;
  fallback_tm.tm_mday = 1;
  fallback_tm.tm_hour = 12;
  fallback_tm.tm_min = 0;
  fallback_tm.tm_sec = 0;
  fallback_tm.tm_isdst = -1;
  return mktime(&fallback_tm);
}

}  // namespace appfw
