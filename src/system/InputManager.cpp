#include "system/InputManager.h"

namespace appfw {

void InputManager::begin(uint8_t up_pin, uint8_t mid_pin, uint8_t down_pin) {
  up_.pin = up_pin;
  mid_.pin = mid_pin;
  down_.pin = down_pin;

  pinMode(up_.pin, INPUT);
  pinMode(mid_.pin, INPUT);
  pinMode(down_.pin, INPUT);

  up_.raw_pressed = isPressed(digitalRead(up_.pin));
  mid_.raw_pressed = isPressed(digitalRead(mid_.pin));
  down_.raw_pressed = isPressed(digitalRead(down_.pin));
  up_.stable_pressed = up_.raw_pressed;
  mid_.stable_pressed = mid_.raw_pressed;
  down_.stable_pressed = down_.raw_pressed;

  const uint32_t now_ms = millis();
  up_.last_raw_change_ms = now_ms;
  mid_.last_raw_change_ms = now_ms;
  down_.last_raw_change_ms = now_ms;
}

void InputManager::update(uint32_t now_ms) {
  updateKey(up_, now_ms, InputEvent::UpShort);
  updateKey(down_, now_ms, InputEvent::DownShort);

  const bool mid_raw = isPressed(digitalRead(mid_.pin));
  if (mid_raw != mid_.raw_pressed) {
    mid_.raw_pressed = mid_raw;
    mid_.last_raw_change_ms = now_ms;
  }
  if ((now_ms - mid_.last_raw_change_ms) >= kDebounceMs &&
      mid_.stable_pressed != mid_.raw_pressed) {
    mid_.stable_pressed = mid_.raw_pressed;
    if (mid_.stable_pressed) {
      mid_.pressed_at_ms = now_ms;
    } else {
      const uint32_t press_ms = now_ms - mid_.pressed_at_ms;
      if (press_ms >= kLongPressMs) {
        pushEvent(InputEvent::MidLong);
      } else {
        pushEvent(InputEvent::MidShort);
      }
    }
  }
}

bool InputManager::pollEvent(InputEvent &event) {
  if (q_head_ == q_tail_) {
    event = InputEvent::None;
    return false;
  }
  event = queue_[q_head_];
  q_head_ = static_cast<uint8_t>((q_head_ + 1) % kQueueSize);
  return true;
}

void InputManager::pushEvent(InputEvent event) {
  const uint8_t next_tail = static_cast<uint8_t>((q_tail_ + 1) % kQueueSize);
  if (next_tail == q_head_) {
    return;
  }
  queue_[q_tail_] = event;
  q_tail_ = next_tail;
}

void InputManager::updateKey(KeyState &key, uint32_t now_ms, InputEvent short_evt) {
  const bool raw = isPressed(digitalRead(key.pin));
  if (raw != key.raw_pressed) {
    key.raw_pressed = raw;
    key.last_raw_change_ms = now_ms;
  }
  if ((now_ms - key.last_raw_change_ms) < kDebounceMs) {
    return;
  }
  if (key.stable_pressed == key.raw_pressed) {
    return;
  }

  key.stable_pressed = key.raw_pressed;
  if (key.stable_pressed) {
    key.pressed_at_ms = now_ms;
  } else {
    pushEvent(short_evt);
  }
}

bool InputManager::isPressed(int level) const {
  return level == LOW;
}

}  // namespace appfw
