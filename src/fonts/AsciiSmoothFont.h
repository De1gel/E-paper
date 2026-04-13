#ifndef ASCII_SMOOTH_FONT_H
#define ASCII_SMOOTH_FONT_H

#include <Arduino.h>

namespace fonts {

constexpr uint8_t kAsciiSmoothFontPx = 20;
constexpr uint8_t kAsciiSmoothFontBitsPerPixel = 4;
constexpr size_t kAsciiSmoothGlyphCount = 45;

bool lookupAsciiSmoothGlyph(char c, const uint8_t *&data, uint8_t &width,
                            uint8_t &height, uint8_t &row_bytes,
                            uint8_t &bits_per_pixel);

}  // namespace fonts

#endif
