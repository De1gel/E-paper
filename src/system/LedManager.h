#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Arduino.h>

#include "system/ModeManager.h"

namespace appfw {

class LedManager {
 public:
  void begin(uint8_t pin);
  void configure(bool enabled, uint8_t max_level, bool active_low);
  void triggerBreath(uint8_t cycles = 1, const char *reason = nullptr);
  void startBreath(const char *reason = nullptr);
  void stopEffects(const char *reason = nullptr);
  void triggerDoubleBlink(const char *reason = nullptr);
  void triggerSingleBlink(const char *reason = nullptr);
  void setSleeping(bool sleeping, const char *reason = nullptr);
  void update(OperationMode mode, uint32_t now_ms, bool sta_connected = false);
  const char *currentStateName() const;

 private:
  enum class BlinkMode : uint8_t {
    None = 0,
    Single,
    Double,
  };
  enum class TraceState : uint8_t {
    Off = 0,
    SleepOff,
    NormalOn,
    ConfigWaitBlink,
    ConfigSessionOn,
    Breath,
    SingleBlink,
    DoubleBlink,
  };

  void setStateOn(bool on);
  void writeLevel(uint8_t level);
  void reportState(TraceState state, const char *reason = nullptr);
  const char *stateName(TraceState state) const;

  uint8_t pin_ = 255;
  bool enabled_ = true;
  bool active_low_ = false;
  uint8_t max_level_ = 255;
  bool sleeping_ = false;
  bool state_on_ = false;
  bool sta_connected_prev_ = false;
  uint32_t last_toggle_ms_ = 0;
  BlinkMode blink_mode_ = BlinkMode::None;
  uint8_t blink_step_ = 0;
  uint32_t blink_next_ms_ = 0;
  bool breath_active_ = false;
  bool breath_hold_ = false;
  uint8_t breath_cycles_ = 0;
  uint8_t breath_cycles_done_ = 0;
  uint32_t breath_start_ms_ = 0;
  TraceState trace_state_ = TraceState::Off;
  static constexpr uint32_t kFastBlinkMs = 150;
  static constexpr uint32_t kDoubleBlinkOnMs = 120;
  static constexpr uint32_t kDoubleBlinkOffMs = 180;
  static constexpr uint32_t kSingleBlinkOnMs = 120;
  static constexpr uint32_t kSingleBlinkOffMs = 120;
  static constexpr uint32_t kBreathPeriodMs = 1200;
  static constexpr uint8_t kPwmChannel = 0;
  static constexpr uint16_t kPwmFreqHz = 5000;
  static constexpr uint8_t kPwmBits = 8;
};

}  // namespace appfw

#endif
