#ifndef COLOR_MAP_H
#define COLOR_MAP_H

#include <Arduino.h>

namespace color_map {
uint8_t packNibbles(uint8_t high, uint8_t low);
uint8_t mapImageByteToNibble(uint8_t color);
uint8_t orangeApproxNibble4x4(uint16_t x, uint16_t y);

}  // namespace color_map

#endif
