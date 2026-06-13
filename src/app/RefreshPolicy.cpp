#include "app/RefreshPolicy.h"

#include <stdlib.h>
#include <string.h>

namespace {

int buildMonthIndex(const char *month) {
  static const char *const kMonths[] = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (int i = 0; i < 12; ++i) {
    if (strncmp(month, kMonths[i], 3) == 0) {
      return i;
    }
  }
  return -1;
}

}  // namespace

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
  const char *date = __DATE__;
  const char *time = __TIME__;
  const int month = buildMonthIndex(date);
  const int day = atoi(date + 4);
  const int year = atoi(date + 7);
  if (month >= 0 && day >= 1 && year >= 2024) {
    fallback_tm.tm_year = year - 1900;
    fallback_tm.tm_mon = month;
    fallback_tm.tm_mday = day;
    fallback_tm.tm_hour = atoi(time);
    fallback_tm.tm_min = atoi(time + 3);
    fallback_tm.tm_sec = atoi(time + 6);
  } else {
    fallback_tm.tm_year = 2026 - 1900;
    fallback_tm.tm_mon = 0;
    fallback_tm.tm_mday = 1;
    fallback_tm.tm_hour = 12;
    fallback_tm.tm_min = 0;
    fallback_tm.tm_sec = 0;
  }
  fallback_tm.tm_isdst = -1;
  return mktime(&fallback_tm);
}

}  // namespace appfw
