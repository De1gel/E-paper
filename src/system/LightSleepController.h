#ifndef SYSTEM_LIGHT_SLEEP_CONTROLLER_H
#define SYSTEM_LIGHT_SLEEP_CONTROLLER_H

#include <Arduino.h>

namespace appfw {

enum class LightSleepWakeReason : uint8_t {
  Skipped = 0,
  Timer,
  Gpio,
  Unknown,
  Error,
};

struct LightSleepResult {
  bool slept = false;
  uint32_t requested_ms = 0;
  uint32_t slept_ms = 0;
  LightSleepWakeReason wake_reason = LightSleepWakeReason::Skipped;
};

class LightSleepController {
 public:
  LightSleepResult sleepUntil(uint32_t now_ms, uint32_t deadline_ms);
  static const char *wakeReasonName(LightSleepWakeReason reason);

 private:
  static constexpr uint32_t kMinSleepMs = 200u;
  static constexpr uint32_t kMaxSleepMs = 300000u;
};

}  // namespace appfw

#endif
