#ifndef CALENDAR_TYPES_H
#define CALENDAR_TYPES_H

#include <Arduino.h>

namespace calendar {

struct Rect {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t w = 0;
  uint16_t h = 0;
};

inline Rect makeRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  Rect r;
  r.x = x;
  r.y = y;
  r.w = w;
  r.h = h;
  return r;
}

enum class LayoutMode : uint8_t {
  LandscapeSplit = 0,
  PortraitSplit = 1,
};

}  // namespace calendar

#endif
