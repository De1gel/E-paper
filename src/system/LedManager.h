#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Arduino.h>

#include "system/ModeManager.h"

namespace appfw {

class LedManager {
 public:
  void begin(uint8_t pin);
  void configure(bool enabled, uint8_t max_level, bool active_low);
  void triggerBreath(uint8_t cycles = 1);
  void triggerDoubleBlink();
  void update(OperationMode mode, uint32_t now_ms);

 private:
  void writeLevel(uint8_t level);

  uint8_t pin_ = 255;
  bool enabled_ = true;
  bool active_low_ = false;
  uint8_t max_level_ = 255;
  bool state_on_ = false;
  uint32_t last_toggle_ms_ = 0;
  bool blink_active_ = false;
  uint8_t blink_step_ = 0;
  uint32_t blink_next_ms_ = 0;
  bool breath_active_ = false;
  uint8_t breath_cycles_ = 0;
  uint8_t breath_cycles_done_ = 0;
  uint32_t breath_start_ms_ = 0;
  static constexpr uint32_t kFastBlinkMs = 150;
  static constexpr uint32_t kDoubleBlinkOnMs = 90;
  static constexpr uint32_t kDoubleBlinkOffMs = 90;
  static constexpr uint32_t kBreathPeriodMs = 1800;
  static constexpr uint8_t kPwmChannel = 0;
  static constexpr uint16_t kPwmFreqHz = 5000;
  static constexpr uint8_t kPwmBits = 8;
};

}  // namespace appfw

#endif
