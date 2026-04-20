#ifndef APP_REFRESH_POLICY_H
#define APP_REFRESH_POLICY_H

#include <stdint.h>
#include <time.h>

namespace appfw {

int32_t dayKeyFromTm(const struct tm &t);
int32_t minuteKeyFromTm(const struct tm &t);
int32_t refreshBucketKey(int32_t minute_key, uint32_t interval_sec);
bool sameCalendarMinute(const struct tm &a, const struct tm &b);
time_t fallbackClockBaseEpoch();

}  // namespace appfw

#endif
