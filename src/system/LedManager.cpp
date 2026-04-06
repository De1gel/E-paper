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

void LedManager::configure(bool enabled, uint8_t max_level, bool active_low) {
  enabled_ = enabled;
  max_level_ = max_level;
  active_low_ = active_low;
  if (!enabled_) {
    state_on_ = false;
    blink_active_ = false;
    breath_active_ = false;
    writeLevel(0);
  } else {
    writeLevel(state_on_ ? 255 : 0);
  }
}

void LedManager::triggerBreath(uint8_t cycles) {
  if (pin_ == 255) {
    return;
  }
  state_on_ = false;
  writeLevel(0);
  breath_active_ = true;
  breath_hold_ = false;
  blink_active_ = false;
  breath_cycles_ = (cycles == 0) ? 1 : cycles;
  breath_cycles_done_ = 0;
  breath_start_ms_ = millis();
}

void LedManager::startBreath() {
  if (pin_ == 255) {
    return;
  }
  state_on_ = false;
  writeLevel(0);
  breath_active_ = true;
  breath_hold_ = true;
  blink_active_ = false;
  breath_cycles_ = 1;
  breath_cycles_done_ = 0;
  breath_start_ms_ = millis();
}

void LedManager::stopEffects() {
  blink_active_ = false;
  breath_active_ = false;
  breath_hold_ = false;
  state_on_ = false;
  writeLevel(0);
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

void LedManager::update(OperationMode mode, uint32_t now_ms, bool sta_connected) {
  if (pin_ == 255) {
    return;
  }

  if (mode == OperationMode::ConfigAP) {
    blink_active_ = false;
    breath_active_ = false;
    if (!state_on_) {
      state_on_ = true;
      writeLevel(255);
    }
    return;
  }

  if (mode == OperationMode::ConfigSTA) {
    blink_active_ = false;
    breath_active_ = false;
    if (sta_connected) {
      if (!state_on_) {
        state_on_ = true;
        writeLevel(255);
      }
    } else if ((now_ms - last_toggle_ms_) >= kStaConnectingBlinkMs) {
      state_on_ = !state_on_;
      writeLevel(state_on_ ? 255 : 0);
      last_toggle_ms_ = now_ms;
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
    if (!breath_hold_ && done_cycles >= breath_cycles_) {
      breath_active_ = false;
      breath_hold_ = false;
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
  uint8_t logical = enabled_ ? level : 0;
  const uint16_t scaled16 = (static_cast<uint16_t>(logical) * static_cast<uint16_t>(max_level_) + 127U) / 255U;
  uint8_t scaled = static_cast<uint8_t>(scaled16 & 0xFFU);
  if (active_low_) {
    scaled = static_cast<uint8_t>(255U - scaled);
  }
  ledcWrite(kPwmChannel, scaled);
}

}  // namespace appfw
