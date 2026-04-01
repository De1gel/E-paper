#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

namespace appfw {

class WifiManager {
 public:
  void begin();
  void update(uint32_t now_ms);
  void startAP();
  void startSTA();

 private:
  enum class State : uint8_t {
    Idle = 0,
    ApRunning,
    StaConnecting,
    StaConnected,
  };

  State state_ = State::Idle;
  uint32_t sta_start_ms_ = 0;
  static constexpr uint32_t kStaConnectTimeoutMs = 15000;
};

}  // namespace appfw

#endif
