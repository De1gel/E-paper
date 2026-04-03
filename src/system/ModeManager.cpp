#include "system/ModeManager.h"

namespace appfw {

namespace {
const char *modeName(OperationMode mode) {
  switch (mode) {
    case OperationMode::Normal:
      return "Normal";
    case OperationMode::ConfigWait:
      return "ConfigWait";
    case OperationMode::ConfigAP:
      return "ConfigAP";
    case OperationMode::ConfigSTA:
      return "ConfigSTA";
    default:
      return "Unknown";
  }
}
}  // namespace

void ModeManager::begin(uint32_t now_ms) {
  mode_ = OperationMode::Normal;
  mode_enter_ms_ = now_ms;
  ap_request_ = false;
  sta_request_ = false;
  white_screen_request_ = false;
  stop_wifi_request_ = false;
  Serial.println("[MODE] init -> Normal");
}

void ModeManager::update(uint32_t now_ms) {
  if (mode_ == OperationMode::ConfigWait &&
      (now_ms - mode_enter_ms_) >= kConfigWaitTimeoutMs) {
    Serial.println("[MODE] config timeout(60s) -> Normal");
    setMode(OperationMode::Normal, now_ms);
  }
}

void ModeManager::onInputEvent(InputEvent event, uint32_t now_ms) {
  if (mode_ == OperationMode::Normal) {
    if (event == InputEvent::MidLong) {
      setMode(OperationMode::ConfigWait, now_ms);
    }
    return;
  }

  if (event == InputEvent::MidLong) {
    const bool need_stop_wifi =
        (mode_ == OperationMode::ConfigAP || mode_ == OperationMode::ConfigSTA);
    if (need_stop_wifi) {
      stop_wifi_request_ = true;
      Serial.println("[MODE] long press exit config + stop wifi");
    } else {
      Serial.println("[MODE] long press exit config -> Normal");
    }
    setMode(OperationMode::Normal, now_ms);
    return;
  }

  if (mode_ == OperationMode::ConfigWait) {
    if (event == InputEvent::UpShort) {
      ap_request_ = true;
      Serial.println("[MODE] request AP");
      setMode(OperationMode::ConfigAP, now_ms);
    } else if (event == InputEvent::DownShort) {
      sta_request_ = true;
      Serial.println("[MODE] request STA");
      setMode(OperationMode::ConfigSTA, now_ms);
    } else if (event == InputEvent::MidShort) {
      white_screen_request_ = true;
      Serial.println("[MODE] request white screen -> Normal");
      setMode(OperationMode::Normal, now_ms);
    }
  }
}

OperationMode ModeManager::mode() const {
  return mode_;
}

bool ModeManager::consumeApRequest() {
  const bool v = ap_request_;
  ap_request_ = false;
  return v;
}

bool ModeManager::consumeStaRequest() {
  const bool v = sta_request_;
  sta_request_ = false;
  return v;
}

bool ModeManager::consumeWhiteScreenRequest() {
  const bool v = white_screen_request_;
  white_screen_request_ = false;
  return v;
}

bool ModeManager::consumeStopWifiRequest() {
  const bool v = stop_wifi_request_;
  stop_wifi_request_ = false;
  return v;
}

void ModeManager::setMode(OperationMode next, uint32_t now_ms) {
  if (mode_ != next) {
    Serial.printf("[MODE] %s -> %s\n", modeName(mode_), modeName(next));
  }
  mode_ = next;
  mode_enter_ms_ = now_ms;
}

void ModeManager::forceNormal(uint32_t now_ms, const char *reason) {
  if (mode_ != OperationMode::Normal) {
    Serial.printf("[MODE] force normal (%s)\n", reason ? reason : "no_reason");
    setMode(OperationMode::Normal, now_ms);
  }
}

}  // namespace appfw
