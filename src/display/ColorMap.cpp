#include "display/ColorMap.h"

#include "Display_EPD_W21.h"

namespace color_map {

namespace {
constexpr uint8_t kBayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};
}  // namespace

uint8_t packNibbles(uint8_t high, uint8_t low) {
  return static_cast<uint8_t>((high << 4) | low);
}

uint8_t mapImageByteToNibble(uint8_t color) {
  switch (color) {
    case 0x00:
      return black;
    case 0xFF:
      return white;
    case 0xFC:
      return yellow;
    case 0xE0:
      return red;
    case 0x03:
      return blue;
    case 0x1C:
      return green;
    default:
      return black;
  }
}

uint8_t orangeApproxNibble4x4(uint16_t x, uint16_t y) {
  // ~75% red + 25% yellow
  const uint8_t t = kBayer4x4[y & 0x03u][x & 0x03u];
  return (t < 12U) ? red : yellow;
}

}  // namespace color_map
