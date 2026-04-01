#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <Arduino.h>

namespace appfw {

enum class InputEvent : uint8_t {
  None = 0,
  UpShort,
  DownShort,
  MidShort,
  MidLong,
};

class InputManager {
 public:
  void begin(uint8_t up_pin, uint8_t mid_pin, uint8_t down_pin);
  void update(uint32_t now_ms);
  bool pollEvent(InputEvent &event);

 private:
  struct KeyState {
    uint8_t pin = 0;
    bool raw_pressed = false;
    bool stable_pressed = false;
    uint32_t last_raw_change_ms = 0;
    uint32_t pressed_at_ms = 0;
  };

  void pushEvent(InputEvent event);
  void updateKey(KeyState &key, uint32_t now_ms, InputEvent short_evt);
  bool isPressed(int level) const;

  static constexpr uint8_t kQueueSize = 8;
  InputEvent queue_[kQueueSize] = {};
  uint8_t q_head_ = 0;
  uint8_t q_tail_ = 0;

  KeyState up_;
  KeyState mid_;
  KeyState down_;

  static constexpr uint32_t kDebounceMs = 35;
  static constexpr uint32_t kLongPressMs = 600;
};

}  // namespace appfw

#endif
