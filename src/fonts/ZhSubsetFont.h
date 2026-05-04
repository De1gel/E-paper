#ifndef ZH_SUBSET_FONT_H
#define ZH_SUBSET_FONT_H

#include <Arduino.h>

namespace fonts {

constexpr size_t kZhCommonGlyphCount = 3000;
constexpr size_t kZhWeekdayGlyphCount = 7;
constexpr uint8_t kZhFontPx10 = 10;
constexpr uint8_t kZhFontPx16 = 16;
constexpr uint8_t kZhFontPx26 = 26;
constexpr uint8_t kZhFontPx30 = 30;
constexpr uint8_t kZhFontBitsPerPixel = 4;
constexpr size_t kZhFont10BytesPerGlyph =
    (kZhFontPx10 * kZhFontPx10 * kZhFontBitsPerPixel) / 8u;
constexpr size_t kZhFont16BytesPerGlyph =
    (kZhFontPx16 * kZhFontPx16 * kZhFontBitsPerPixel) / 8u;
constexpr size_t kZhFont26BytesPerGlyph =
    (kZhFontPx26 * kZhFontPx26 * kZhFontBitsPerPixel) / 8u;
constexpr size_t kZhFont30BytesPerGlyph =
    (kZhFontPx30 * kZhFontPx30 * kZhFontBitsPerPixel) / 8u;

extern const uint16_t kZhCommon3000Codepoints[kZhCommonGlyphCount];
extern const uint8_t kZhCommon3000Px10[kZhCommonGlyphCount * kZhFont10BytesPerGlyph];
extern const uint8_t kZhCommon3000Px16[kZhCommonGlyphCount * kZhFont16BytesPerGlyph];
extern const uint16_t kZhWeekday7Codepoints[kZhWeekdayGlyphCount];
extern const uint8_t kZhWeekday7Px26[kZhWeekdayGlyphCount * kZhFont26BytesPerGlyph];
extern const size_t kZhCityGlyphCount;
extern const uint16_t kZhCityCodepoints[];
extern const uint8_t kZhCityPx30[];

extern const uint8_t kZhFont10BoxLeft;
extern const uint8_t kZhFont10BoxTop;
extern const uint8_t kZhFont10BoxWidth;
extern const uint8_t kZhFont10BoxHeight;
extern const uint8_t kZhFont16BoxLeft;
extern const uint8_t kZhFont16BoxTop;
extern const uint8_t kZhFont16BoxWidth;
extern const uint8_t kZhFont16BoxHeight;
extern const uint8_t kZhFont26BoxLeft;
extern const uint8_t kZhFont26BoxTop;
extern const uint8_t kZhFont26BoxWidth;
extern const uint8_t kZhFont26BoxHeight;
extern const uint8_t kZhFont30BoxLeft;
extern const uint8_t kZhFont30BoxTop;
extern const uint8_t kZhFont30BoxWidth;
extern const uint8_t kZhFont30BoxHeight;

bool lookupCommonZhGlyph(uint32_t codepoint, uint8_t px, const uint8_t *&data, uint8_t &width,
                         uint8_t &height, uint8_t &row_bytes, uint8_t &bits_per_pixel);

}  // namespace fonts

#endif
