#include "fonts/ZhSubsetFont.h"

namespace fonts {
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

namespace {

int findGlyphIndex(const uint16_t *codepoints, size_t glyph_count, uint32_t codepoint) {
  int lo = 0;
  int hi = static_cast<int>(glyph_count) - 1;
  while (lo <= hi) {
    const int mid = lo + ((hi - lo) / 2);
    const uint16_t value = codepoints[mid];
    if (value == codepoint) {
      return mid;
    }
    if (value < codepoint) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return -1;
}

}  // namespace

bool lookupCommonZhGlyph(uint32_t codepoint, uint8_t px, const uint8_t *&data, uint8_t &width,
                         uint8_t &height, uint8_t &row_bytes, uint8_t &bits_per_pixel) {
  data = nullptr;
  width = 0;
  height = 0;
  row_bytes = 0;
  bits_per_pixel = 0;

  bits_per_pixel = kZhFontBitsPerPixel;
  if (px >= kZhFontPx30) {
    const int index = findGlyphIndex(kZhCityCodepoints, kZhCityGlyphCount, codepoint);
    if (index < 0) {
      return false;
    }
    width = kZhFontPx30;
    height = kZhFontPx30;
    row_bytes = static_cast<uint8_t>((kZhFontPx30 * bits_per_pixel) / 8u);
    data = &kZhCityPx30[static_cast<size_t>(index) * kZhFont30BytesPerGlyph];
    return true;
  }

  if (px >= kZhFontPx26) {
    const int index = findGlyphIndex(kZhWeekday7Codepoints, kZhWeekdayGlyphCount, codepoint);
    if (index < 0) {
      return false;
    }
    width = kZhFontPx26;
    height = kZhFontPx26;
    row_bytes = static_cast<uint8_t>((kZhFontPx26 * bits_per_pixel) / 8u);
    data = &kZhWeekday7Px26[static_cast<size_t>(index) * kZhFont26BytesPerGlyph];
    return true;
  }

  const int index = findGlyphIndex(kZhCommon3000Codepoints, kZhCommonGlyphCount, codepoint);
  if (index < 0) {
    return false;
  }

  if (px >= kZhFontPx16) {
    width = kZhFontPx16;
    height = kZhFontPx16;
    row_bytes = static_cast<uint8_t>((kZhFontPx16 * bits_per_pixel) / 8u);
    data = &kZhCommon3000Px16[static_cast<size_t>(index) * kZhFont16BytesPerGlyph];
    return true;
  }

  width = kZhFontPx10;
  height = kZhFontPx10;
  row_bytes = static_cast<uint8_t>((kZhFontPx10 * bits_per_pixel) / 8u);
  data = &kZhCommon3000Px10[static_cast<size_t>(index) * kZhFont10BytesPerGlyph];
  return true;
}

}  // namespace fonts
