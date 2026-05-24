#ifndef SYSTEM_LOG_CONFIG_H
#define SYSTEM_LOG_CONFIG_H

namespace appfw {

#ifndef APP_DEBUG_LOGS
#define APP_DEBUG_LOGS 0
#endif

#ifndef APP_ENABLE_LIGHT_SLEEP
#define APP_ENABLE_LIGHT_SLEEP 1
#endif

#ifndef APP_SLEEP_DIAG_LOGS
#define APP_SLEEP_DIAG_LOGS 0
#endif

#ifndef APP_SLEEP_QUIET_LOGS
#define APP_SLEEP_QUIET_LOGS 0
#endif

static constexpr bool kDebugLogs = APP_DEBUG_LOGS != 0;
static constexpr bool kLightSleepEnabled = APP_ENABLE_LIGHT_SLEEP != 0;
static constexpr bool kSleepDiagLogs = APP_SLEEP_DIAG_LOGS != 0;
static constexpr bool kSleepQuietLogs = APP_SLEEP_QUIET_LOGS != 0;

}  // namespace appfw

#endif
