#include "system/LightSleepController.h"

#include <driver/gpio.h>
#include <esp32/rom/ets_sys.h>
#include <esp_err.h>
#include <esp_sleep.h>
#include <esp_timer.h>

namespace appfw {
namespace {

constexpr gpio_num_t kWakeUpPin = GPIO_NUM_0;
constexpr gpio_num_t kWakeMidPin = GPIO_NUM_35;
constexpr gpio_num_t kWakeDownPin = GPIO_NUM_34;

bool wakePinActive() {
  return gpio_get_level(kWakeUpPin) == 0 ||
         gpio_get_level(kWakeMidPin) == 0 ||
         gpio_get_level(kWakeDownPin) == 0;
}

void configureGpioWakeSource() {
  const esp_err_t up_err = gpio_wakeup_enable(kWakeUpPin, GPIO_INTR_LOW_LEVEL);
  const esp_err_t mid_err = gpio_wakeup_enable(kWakeMidPin, GPIO_INTR_LOW_LEVEL);
  const esp_err_t down_err = gpio_wakeup_enable(kWakeDownPin, GPIO_INTR_LOW_LEVEL);
  const esp_err_t gpio_err = esp_sleep_enable_gpio_wakeup();
  ets_printf("[SLEEP][CFG] gpio_wakeup_enable up=%d(%s) mid=%d(%s) down=%d(%s) gpio=%d(%s)\n",
             static_cast<int>(up_err), esp_err_to_name(up_err),
             static_cast<int>(mid_err), esp_err_to_name(mid_err),
             static_cast<int>(down_err), esp_err_to_name(down_err),
             static_cast<int>(gpio_err), esp_err_to_name(gpio_err));
}

void traceWakePins(const char *tag) {
  ets_printf("[SLEEP][LL] %s pins up=%d mid=%d down=%d\n",
             tag ? tag : "-",
             gpio_get_level(kWakeUpPin),
             gpio_get_level(kWakeMidPin),
             gpio_get_level(kWakeDownPin));
}

LightSleepWakeReason mapWakeupCause(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return LightSleepWakeReason::Timer;
    case ESP_SLEEP_WAKEUP_GPIO:
      return LightSleepWakeReason::Gpio;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      return LightSleepWakeReason::Unknown;
    default:
      return LightSleepWakeReason::Unknown;
  }
}

}  // namespace

LightSleepResult LightSleepController::sleepUntil(uint32_t now_ms, uint32_t deadline_ms) {
  LightSleepResult result;
  if (deadline_ms <= now_ms) {
    result.wake_reason = LightSleepWakeReason::Skipped;
    return result;
  }

  uint32_t sleep_ms = deadline_ms - now_ms;
  if (sleep_ms < kMinSleepMs) {
    result.wake_reason = LightSleepWakeReason::Skipped;
    return result;
  }
  if (sleep_ms > kMaxSleepMs) {
    sleep_ms = kMaxSleepMs;
  }

  result.requested_ms = sleep_ms;

  if (wakePinActive()) {
    ets_printf("[SLEEP][CFG] skip sleep because wake pin already active\n");
    result.wake_reason = LightSleepWakeReason::Skipped;
    return result;
  }

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  configureGpioWakeSource();
  const esp_err_t timer_err =
      esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(sleep_ms) * 1000ULL);
  ets_printf("[SLEEP][CFG] timer_wakeup=%d(%s) requested_ms=%lu\n",
             static_cast<int>(timer_err), esp_err_to_name(timer_err),
             static_cast<unsigned long>(sleep_ms));
  traceWakePins("before_sleep");

  const uint64_t start_us = esp_timer_get_time();
  const esp_err_t err = esp_light_sleep_start();
  const uint64_t end_us = esp_timer_get_time();
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  ets_printf("[SLEEP][LL] returned err=%d cause=%d slept_ms=%lu\n",
             static_cast<int>(err),
             static_cast<int>(cause),
             static_cast<unsigned long>((end_us - start_us) / 1000ULL));
  traceWakePins("after_wake");

  if (err != ESP_OK) {
    result.wake_reason = LightSleepWakeReason::Error;
    return result;
  }

  result.slept = true;
  result.slept_ms = static_cast<uint32_t>((end_us - start_us) / 1000ULL);
  result.wake_reason = mapWakeupCause(cause);
  return result;
}

const char *LightSleepController::wakeReasonName(LightSleepWakeReason reason) {
  switch (reason) {
    case LightSleepWakeReason::Skipped:
      return "skipped";
    case LightSleepWakeReason::Timer:
      return "timer";
    case LightSleepWakeReason::Gpio:
      return "gpio";
    case LightSleepWakeReason::Unknown:
      return "unknown";
    case LightSleepWakeReason::Error:
      return "error";
    default:
      return "invalid";
  }
}

}  // namespace appfw
