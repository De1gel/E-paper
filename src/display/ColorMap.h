#ifndef COLOR_MAP_H
#define COLOR_MAP_H

#include <Arduino.h>

namespace color_map {
enum class DitherMode : uint8_t {
  None = 0,
  Bayer4x4 = 1,
};

struct Rgb888 {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

uint8_t packNibbles(uint8_t high, uint8_t low);
uint8_t mapImageByteToNibble(uint8_t color);
uint8_t mapRgbToNibble(const Rgb888 &rgb, uint16_t x, uint16_t y, DitherMode mode);
uint8_t mixTwoColorsBayer4x4(uint8_t color_a, uint8_t color_b, uint8_t a_count_16,
                             uint16_t x, uint16_t y);

}  // namespace color_map

#endif
