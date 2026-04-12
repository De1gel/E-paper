#include "fonts/ZhSubsetFont.h"

namespace fonts {
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

namespace {

int findGlyphIndex(uint32_t codepoint) {
  int lo = 0;
  int hi = static_cast<int>(kZhCommonGlyphCount) - 1;
  while (lo <= hi) {
    const int mid = lo + ((hi - lo) / 2);
    const uint16_t value = kZhCommon3000Codepoints[mid];
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

  const int index = findGlyphIndex(codepoint);
  if (index < 0) {
    return false;
  }

  bits_per_pixel = kZhFontBitsPerPixel;
  if (px >= kZhFontPx24) {
    width = kZhFontPx24;
    height = kZhFontPx24;
    row_bytes = static_cast<uint8_t>((kZhFontPx24 * bits_per_pixel) / 8u);
    data = &kZhCommon3000Px24[static_cast<size_t>(index) * kZhFont24BytesPerGlyph];
    return true;
  }

  width = kZhFontPx16;
  height = kZhFontPx16;
  row_bytes = static_cast<uint8_t>((kZhFontPx16 * bits_per_pixel) / 8u);
  data = &kZhCommon3000Px16[static_cast<size_t>(index) * kZhFont16BytesPerGlyph];
  return true;
}

}  // namespace fonts
