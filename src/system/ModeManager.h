#ifndef MODE_MANAGER_H
#define MODE_MANAGER_H

#include <Arduino.h>

#include "system/InputManager.h"

namespace appfw {

enum class OperationMode : uint8_t {
  Normal = 0,
  ConfigWait,
  ConfigAP,
  ConfigSTA,
};

class ModeManager {
 public:
  void begin(uint32_t now_ms);
  void update(uint32_t now_ms);
  void onInputEvent(InputEvent event, uint32_t now_ms);
  void forceNormal(uint32_t now_ms, const char *reason);

  OperationMode mode() const;
  bool consumeApRequest();
  bool consumeStaRequest();
  bool consumeWhiteScreenRequest();
  bool consumeStopWifiRequest();

 private:
  void setMode(OperationMode next, uint32_t now_ms);

  OperationMode mode_ = OperationMode::Normal;
  uint32_t mode_enter_ms_ = 0;

  bool ap_request_ = false;
  bool sta_request_ = false;
  bool white_screen_request_ = false;
  bool stop_wifi_request_ = false;

  static constexpr uint32_t kConfigWaitTimeoutMs = 60000;
};

}  // namespace appfw

#endif
