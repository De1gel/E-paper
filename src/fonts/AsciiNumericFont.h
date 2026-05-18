#ifndef ASCII_NUMERIC_FONT_H
#define ASCII_NUMERIC_FONT_H

#include <Arduino.h>

namespace fonts {

bool lookupAsciiNumericGlyph(char c, uint8_t px, const uint8_t *&data, uint8_t &width,
                             uint8_t &height, uint8_t &row_bytes,
                             uint8_t &bits_per_pixel);

}  // namespace fonts

#endif
