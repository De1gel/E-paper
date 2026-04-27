#ifndef ZH_SUBSET_FONT_H
#define ZH_SUBSET_FONT_H

#include <Arduino.h>

namespace fonts {

constexpr size_t kZhCommonGlyphCount = 3000;
constexpr uint8_t kZhFontPx16 = 16;
constexpr uint8_t kZhFontPx24 = 24;
constexpr uint8_t kZhFontBitsPerPixel = 4;
constexpr size_t kZhFont16BytesPerGlyph =
    (kZhFontPx16 * kZhFontPx16 * kZhFontBitsPerPixel) / 8u;
constexpr size_t kZhFont24BytesPerGlyph =
    (kZhFontPx24 * kZhFontPx24 * kZhFontBitsPerPixel) / 8u;

extern const uint16_t kZhCommon3000Codepoints[kZhCommonGlyphCount];
extern const uint8_t kZhCommon3000Px16[kZhCommonGlyphCount * kZhFont16BytesPerGlyph];
extern const uint8_t kZhCommon3000Px24[kZhCommonGlyphCount * kZhFont24BytesPerGlyph];
extern const uint8_t kZhFont16BoxLeft;
extern const uint8_t kZhFont16BoxTop;
extern const uint8_t kZhFont16BoxWidth;
extern const uint8_t kZhFont16BoxHeight;
extern const uint8_t kZhFont24BoxLeft;
extern const uint8_t kZhFont24BoxTop;
extern const uint8_t kZhFont24BoxWidth;
extern const uint8_t kZhFont24BoxHeight;

bool lookupCommonZhGlyph(uint32_t codepoint, uint8_t px, const uint8_t *&data, uint8_t &width,
                         uint8_t &height, uint8_t &row_bytes, uint8_t &bits_per_pixel);

}  // namespace fonts

#endif
