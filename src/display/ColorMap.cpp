#include "display/ColorMap.h"

#include <limits.h>

#include "Display_EPD_W21.h"

namespace color_map {

namespace {
constexpr uint8_t kBayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

struct PaletteEntry {
  Rgb888 rgb;
  uint8_t nibble;
};

constexpr PaletteEntry kPalette[] = {
    // Empirical palette (measured appearance), mapped to device-native 6-color nibbles.
    {{0, 0, 0}, black},        // #000000
    {{255, 255, 255}, white},  // #FFFFFF
    {{251, 246, 0}, yellow},   // #FBF600
    {{251, 4, 0}, red},        // #FB0400
    {{0, 53, 214}, blue},      // #0035D6
    {{12, 108, 13}, green},    // #0C6C0D
};

inline uint32_t colorDistanceWeighted(const Rgb888 &a, const Rgb888 &b) {
  const int32_t dr = static_cast<int32_t>(a.r) - static_cast<int32_t>(b.r);
  const int32_t dg = static_cast<int32_t>(a.g) - static_cast<int32_t>(b.g);
  const int32_t db = static_cast<int32_t>(a.b) - static_cast<int32_t>(b.b);
  return static_cast<uint32_t>(3 * dr * dr + 6 * dg * dg + db * db);
}
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

uint8_t mapRgbToNibble(const Rgb888 &rgb, uint16_t x, uint16_t y, DitherMode mode) {
  uint8_t first_idx = 0;
  uint8_t second_idx = 0;
  uint32_t best_dist = UINT32_MAX;
  uint32_t second_dist = UINT32_MAX;

  for (uint8_t i = 0; i < static_cast<uint8_t>(sizeof(kPalette) / sizeof(kPalette[0])); ++i) {
    const uint32_t d = colorDistanceWeighted(rgb, kPalette[i].rgb);
    if (d < best_dist) {
      second_dist = best_dist;
      second_idx = first_idx;
      best_dist = d;
      first_idx = i;
    } else if (d < second_dist) {
      second_dist = d;
      second_idx = i;
    }
  }

  if (mode == DitherMode::None || second_dist == UINT32_MAX || (best_dist == 0 && second_dist == 0)) {
    return kPalette[first_idx].nibble;
  }

  const uint32_t sum = best_dist + second_dist;
  if (sum == 0) {
    return kPalette[first_idx].nibble;
  }

  const uint8_t threshold = static_cast<uint8_t>(kBayer4x4[y & 0x03u][x & 0x03u]);
  const uint32_t keep_best_x16 = (second_dist * 16U) / sum;
  return (threshold < keep_best_x16) ? kPalette[first_idx].nibble
                                     : kPalette[second_idx].nibble;
}

uint8_t mixTwoColorsBayer4x4(uint8_t color_a, uint8_t color_b, uint8_t a_count_16,
                             uint16_t x, uint16_t y) {
  if (a_count_16 >= 16U) {
    return color_a;
  }
  const uint8_t threshold = static_cast<uint8_t>(kBayer4x4[y & 0x03u][x & 0x03u]);
  return (threshold < a_count_16) ? color_a : color_b;
}

}  // namespace color_map
