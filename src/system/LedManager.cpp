#include "system/LedManager.h"

namespace appfw {

void LedManager::begin(uint8_t pin) {
  pin_ = pin;
  ledcSetup(kPwmChannel, kPwmFreqHz, kPwmBits);
  ledcAttachPin(pin_, kPwmChannel);
  writeLevel(0);
  state_on_ = false;
  sleeping_ = false;
  sta_connected_prev_ = false;
  last_toggle_ms_ = millis();
  blink_mode_ = BlinkMode::None;
  blink_step_ = 0;
  blink_next_ms_ = last_toggle_ms_;
  breath_active_ = false;
  breath_hold_ = false;
  breath_cycles_ = 0;
  breath_cycles_done_ = 0;
  breath_start_ms_ = last_toggle_ms_;
  trace_state_ = TraceState::Off;
  reportState(TraceState::Off, "begin");
}

void LedManager::configure(bool enabled, uint8_t max_level, bool active_low) {
  enabled_ = enabled;
  max_level_ = max_level;
  active_low_ = active_low;
  if (!enabled_) {
    state_on_ = false;
    blink_mode_ = BlinkMode::None;
    breath_active_ = false;
    writeLevel(0);
    reportState(TraceState::Off, "disabled");
  } else {
    writeLevel(state_on_ ? 255 : 0);
  }
}

void LedManager::triggerBreath(uint8_t cycles, const char *reason) {
  if (pin_ == 255) {
    return;
  }
  sleeping_ = false;
  state_on_ = false;
  writeLevel(0);
  breath_active_ = true;
  breath_hold_ = false;
  blink_mode_ = BlinkMode::None;
  breath_cycles_ = (cycles == 0) ? 1 : cycles;
  breath_cycles_done_ = 0;
  breath_start_ms_ = millis();
  reportState(TraceState::Breath, reason ? reason : "trigger_breath");
}

void LedManager::startBreath(const char *reason) {
  if (pin_ == 255) {
    return;
  }
  sleeping_ = false;
  state_on_ = false;
  writeLevel(0);
  breath_active_ = true;
  breath_hold_ = true;
  blink_mode_ = BlinkMode::None;
  breath_cycles_ = 1;
  breath_cycles_done_ = 0;
  breath_start_ms_ = millis();
  reportState(TraceState::Breath, reason ? reason : "start_breath");
}

void LedManager::stopEffects(const char *reason) {
  blink_mode_ = BlinkMode::None;
  if (breath_active_) {
    const uint32_t elapsed = millis() - breath_start_ms_;
    const uint32_t done_cycles = elapsed / kBreathPeriodMs;
    breath_hold_ = false;
    breath_cycles_ = static_cast<uint8_t>(done_cycles + 1U);
    Serial.printf("[LED] state=%s reason=%s\n", currentStateName(),
                  reason ? reason : "stop_effects");
    return;
  }
  breath_active_ = false;
  breath_hold_ = false;
  Serial.printf("[LED] state=%s reason=%s\n", currentStateName(),
                reason ? reason : "stop_effects");
}

void LedManager::triggerDoubleBlink(const char *reason) {
  if (pin_ == 255) {
    return;
  }
  sleeping_ = false;
  blink_mode_ = BlinkMode::Double;
  blink_step_ = 0;
  blink_next_ms_ = millis();
  breath_active_ = false;
  breath_hold_ = false;
  reportState(TraceState::DoubleBlink, reason ? reason : "double_blink");
}

void LedManager::triggerSingleBlink(const char *reason) {
  if (pin_ == 255) {
    return;
  }
  sleeping_ = false;
  blink_mode_ = BlinkMode::Single;
  blink_step_ = 0;
  blink_next_ms_ = millis();
  breath_active_ = false;
  breath_hold_ = false;
  reportState(TraceState::SingleBlink, reason ? reason : "single_blink");
}

void LedManager::setSleeping(bool sleeping, const char *reason) {
  sleeping_ = sleeping;
  if (sleeping_) {
    blink_mode_ = BlinkMode::None;
    breath_active_ = false;
    breath_hold_ = false;
    setStateOn(false);
    reportState(TraceState::SleepOff, reason ? reason : "sleep");
    return;
  }
  reportState(TraceState::Off, reason ? reason : "wake");
}

void LedManager::update(OperationMode mode, uint32_t now_ms, bool sta_connected) {
  if (pin_ == 255) {
    return;
  }

  if (sleeping_) {
    setStateOn(false);
    reportState(TraceState::SleepOff, "sleeping");
    sta_connected_prev_ = sta_connected;
    return;
  }

  if (mode == OperationMode::ConfigSTA && !sta_connected_prev_ && sta_connected) {
    triggerSingleBlink("sta_connected");
  }
  sta_connected_prev_ = sta_connected;

  if (blink_mode_ != BlinkMode::None) {
    if (now_ms < blink_next_ms_) {
      return;
    }
    if (blink_mode_ == BlinkMode::Single) {
      switch (blink_step_) {
        case 0:
          setStateOn(false);
          blink_next_ms_ = now_ms + kSingleBlinkOffMs;
          blink_step_ = 1;
          break;
        case 1:
          setStateOn(true);
          blink_next_ms_ = now_ms + kSingleBlinkOnMs;
          blink_step_ = 2;
          break;
        default:
          blink_mode_ = BlinkMode::None;
          reportState(TraceState::Off, "single_blink_done");
          break;
      }
    } else {
      switch (blink_step_) {
        case 0:
          setStateOn(false);
          blink_next_ms_ = now_ms + kDoubleBlinkOffMs;
          blink_step_ = 1;
          break;
        case 1:
          setStateOn(true);
          blink_next_ms_ = now_ms + kDoubleBlinkOnMs;
          blink_step_ = 2;
          break;
        case 2:
          setStateOn(false);
          blink_next_ms_ = now_ms + kDoubleBlinkOffMs;
          blink_step_ = 3;
          break;
        case 3:
          setStateOn(true);
          blink_next_ms_ = now_ms + kDoubleBlinkOnMs;
          blink_step_ = 4;
          break;
        default:
          blink_mode_ = BlinkMode::None;
          reportState(TraceState::Off, "double_blink_done");
          break;
      }
    }
    return;
  }

  if (breath_active_) {
    const uint32_t elapsed = now_ms - breath_start_ms_;
    const uint32_t phase = elapsed % kBreathPeriodMs;
    const uint32_t half = kBreathPeriodMs / 2;
    uint8_t linear = 0;
    if (phase < half) {
      linear = static_cast<uint8_t>((phase * 255U) / half);
    } else {
      linear = static_cast<uint8_t>(((kBreathPeriodMs - phase) * 255U) / half);
    }
    const uint8_t level =
        static_cast<uint8_t>((static_cast<uint16_t>(linear) * static_cast<uint16_t>(linear)) / 255U);
    writeLevel(level);
    reportState(TraceState::Breath, "breath_active");
    const uint32_t done_cycles = elapsed / kBreathPeriodMs;
    if (!breath_hold_ && done_cycles >= breath_cycles_) {
      breath_active_ = false;
      breath_hold_ = false;
      reportState(TraceState::Off, "breath_done");
    }
    return;
  }

  if (mode == OperationMode::ConfigWait) {
    if ((now_ms - last_toggle_ms_) >= kFastBlinkMs) {
      setStateOn(!state_on_);
      last_toggle_ms_ = now_ms;
    }
    reportState(TraceState::ConfigWaitBlink, "config_wait");
    return;
  }

  if (mode == OperationMode::ConfigAP || mode == OperationMode::ConfigSTA) {
    setStateOn(true);
    reportState(TraceState::ConfigSessionOn, mode == OperationMode::ConfigAP ? "config_ap"
                                                                             : "config_sta");
    return;
  }

  setStateOn(true);
  reportState(TraceState::NormalOn, "awake");
}

const char *LedManager::currentStateName() const {
  return stateName(trace_state_);
}

void LedManager::setStateOn(bool on) {
  state_on_ = on;
  writeLevel(on ? 255 : 0);
}

void LedManager::reportState(TraceState state, const char *reason) {
  if (trace_state_ == state) {
    return;
  }
  trace_state_ = state;
  Serial.printf("[LED] state=%s reason=%s\n", stateName(state),
                reason ? reason : "state_change");
}

const char *LedManager::stateName(TraceState state) const {
  switch (state) {
    case TraceState::Off:
      return "off";
    case TraceState::SleepOff:
      return "sleep_off";
    case TraceState::NormalOn:
      return "normal_on";
    case TraceState::ConfigWaitBlink:
      return "config_wait_fast_blink";
    case TraceState::ConfigSessionOn:
      return "config_session_on";
    case TraceState::Breath:
      return "breath";
    case TraceState::SingleBlink:
      return "single_blink";
    case TraceState::DoubleBlink:
      return "double_blink";
    default:
      return "invalid";
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
