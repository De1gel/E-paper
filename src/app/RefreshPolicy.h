#ifndef APP_REFRESH_POLICY_H
#define APP_REFRESH_POLICY_H

#include <stdint.h>
#include <time.h>

namespace appfw {

enum class RefreshSyncPlan : uint8_t {
  RenderOnly = 0,
  SyncBeforeRender,
  RenderThenSync,
};

int32_t dayKeyFromTm(const struct tm &t);
int32_t minuteKeyFromTm(const struct tm &t);
int32_t dailySyncCycleKey(const struct tm &local_tm, uint16_t wake_minute);
RefreshSyncPlan selectRefreshSyncPlan(bool calendar_page, bool daily_sync_due,
                                      bool has_sta_credentials);
const char *refreshSyncPlanName(RefreshSyncPlan plan);
int32_t refreshBucketKey(int32_t minute_key, uint32_t interval_sec);
bool sameCalendarMinute(const struct tm &a, const struct tm &b);
time_t fallbackClockBaseEpoch();

}  // namespace appfw

#endif
