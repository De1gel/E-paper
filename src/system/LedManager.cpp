#include "system/LedManager.h"

namespace appfw {

void LedManager::begin(uint8_t pin) {
  pin_ = pin;
  ledcSetup(kPwmChannel, kPwmFreqHz, kPwmBits);
  ledcAttachPin(pin_, kPwmChannel);
  writeLevel(0);
  state_on_ = false;
  last_toggle_ms_ = millis();
  blink_active_ = false;
  blink_step_ = 0;
  blink_next_ms_ = last_toggle_ms_;
  breath_active_ = false;
  breath_cycles_ = 0;
  breath_cycles_done_ = 0;
  breath_start_ms_ = last_toggle_ms_;
}

void LedManager::triggerBreath(uint8_t cycles) {
  if (pin_ == 255) {
    return;
  }
  breath_active_ = true;
  blink_active_ = false;
  breath_cycles_ = (cycles == 0) ? 1 : cycles;
  breath_cycles_done_ = 0;
  breath_start_ms_ = millis();
}

void LedManager::triggerDoubleBlink() {
  if (pin_ == 255) {
    return;
  }
  blink_active_ = true;
  blink_step_ = 0;
  blink_next_ms_ = millis();
  breath_active_ = false;
}

void LedManager::update(OperationMode mode, uint32_t now_ms) {
  if (pin_ == 255) {
    return;
  }

  if (mode == OperationMode::ConfigAP || mode == OperationMode::ConfigSTA) {
    blink_active_ = false;
    breath_active_ = false;
    if (!state_on_) {
      state_on_ = true;
      writeLevel(255);
    }
    return;
  }

  if (mode == OperationMode::ConfigWait) {
    blink_active_ = false;
    breath_active_ = false;
    if ((now_ms - last_toggle_ms_) >= kFastBlinkMs) {
      state_on_ = !state_on_;
      writeLevel(state_on_ ? 255 : 0);
      last_toggle_ms_ = now_ms;
    }
    return;
  }

  if (blink_active_) {
    if (now_ms >= blink_next_ms_) {
      switch (blink_step_) {
        case 0:
          writeLevel(255);
          state_on_ = true;
          blink_next_ms_ = now_ms + kDoubleBlinkOnMs;
          blink_step_ = 1;
          break;
        case 1:
          writeLevel(0);
          state_on_ = false;
          blink_next_ms_ = now_ms + kDoubleBlinkOffMs;
          blink_step_ = 2;
          break;
        case 2:
          writeLevel(255);
          state_on_ = true;
          blink_next_ms_ = now_ms + kDoubleBlinkOnMs;
          blink_step_ = 3;
          break;
        default:
          writeLevel(0);
          state_on_ = false;
          blink_active_ = false;
          break;
      }
    }
    return;
  }

  if (breath_active_) {
    const uint32_t elapsed = now_ms - breath_start_ms_;
    const uint32_t phase = elapsed % kBreathPeriodMs;
    const uint32_t half = kBreathPeriodMs / 2;
    uint8_t level = 0;
    if (phase < half) {
      level = static_cast<uint8_t>((phase * 255U) / half);
    } else {
      level = static_cast<uint8_t>(((kBreathPeriodMs - phase) * 255U) / half);
    }
    writeLevel(level);
    const uint32_t done_cycles = elapsed / kBreathPeriodMs;
    if (done_cycles >= breath_cycles_) {
      breath_active_ = false;
      writeLevel(0);
      state_on_ = false;
    }
    return;
  }

  if (state_on_ || breath_active_) {
    breath_active_ = false;
    state_on_ = false;
    writeLevel(0);
  }
}

void LedManager::writeLevel(uint8_t level) {
  ledcWrite(kPwmChannel, level);
}

}  // namespace appfw
