#include "system/InputManager.h"

namespace appfw {

void InputManager::begin(uint8_t up_pin, uint8_t mid_pin, uint8_t down_pin) {
  up_.pin = up_pin;
  mid_.pin = mid_pin;
  down_.pin = down_pin;

  pinMode(up_.pin, INPUT_PULLUP);
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
      mid_.long_sent = false;
    } else {
      if (!mid_.long_sent) {
        pushEvent(InputEvent::MidShort);
      }
    }
  }
  if (mid_.stable_pressed && !mid_.long_sent &&
      (now_ms - mid_.pressed_at_ms) >= kLongPressMs) {
    mid_.long_sent = true;
    pushEvent(InputEvent::MidLong);
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

void InputManager::recoverWakePress(bool up_pressed, bool mid_pressed, bool down_pressed,
                                    uint32_t now_ms) {
  recoverKeyWakePress(up_, up_pressed, now_ms);
  recoverKeyWakePress(mid_, mid_pressed, now_ms);
  recoverKeyWakePress(down_, down_pressed, now_ms);
}

void InputManager::prepareForSleep(uint32_t now_ms) {
  q_head_ = 0;
  q_tail_ = 0;
  resetKeyState(up_, now_ms);
  resetKeyState(mid_, now_ms);
  resetKeyState(down_, now_ms);
}

void InputManager::pushEvent(InputEvent event) {
  const uint8_t next_tail = static_cast<uint8_t>((q_tail_ + 1) % kQueueSize);
  if (next_tail == q_head_) {
    return;
  }
  queue_[q_tail_] = event;
  q_tail_ = next_tail;
}

void InputManager::recoverKeyWakePress(KeyState &key, bool pressed, uint32_t now_ms) {
  if (!pressed) {
    return;
  }
  key.raw_pressed = true;
  key.stable_pressed = true;
  key.long_sent = false;
  key.pressed_at_ms = now_ms;
  key.last_raw_change_ms = now_ms;
}

void InputManager::resetKeyState(KeyState &key, uint32_t now_ms) {
  const bool pressed = isPressed(digitalRead(key.pin));
  key.raw_pressed = pressed;
  key.stable_pressed = pressed;
  key.long_sent = false;
  key.pressed_at_ms = pressed ? now_ms : 0;
  key.last_raw_change_ms = now_ms;
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
    if (key.stable_pressed && !key.long_sent &&
        (now_ms - key.pressed_at_ms) >= kLongPressMs) {
      key.long_sent = true;
    }
    return;
  }

  key.stable_pressed = key.raw_pressed;
  if (key.stable_pressed) {
    key.pressed_at_ms = now_ms;
    key.long_sent = false;
  } else {
    if (!key.long_sent) {
      pushEvent(short_evt);
    }
  }

}

bool InputManager::isPressed(int level) const {
  return level == LOW;
}

}  // namespace appfw
