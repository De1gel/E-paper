#ifndef CALENDAR_TEXT_H
#define CALENDAR_TEXT_H

#include <Arduino.h>

namespace calendar {

enum class TextFont : uint8_t {
  Auto = 0,
  CjkAuto = 1,
  AsciiSmooth = 2,
  Cjk16 = 16,
  Cjk24 = 24,
};

enum class TextAAMode : uint8_t {
  Threshold = 0,
  Burkes = 1,
};

struct GlyphBitmap {
  const uint8_t *rows = nullptr;
  uint8_t width = 0;
  uint8_t height = 0;
  uint8_t row_bytes = 0;
  uint8_t bits_per_pixel = 1;
};

struct TextStyle {
  TextFont font = TextFont::Auto;
  uint8_t pixel_height = 0;
  uint8_t base_height = 0;
  uint8_t box_left = 0;
  uint8_t box_top = 0;
  uint8_t box_width = 0;
  uint8_t box_height = 0;
  uint8_t letter_spacing = 1;
};

struct TextCoverageMap {
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t *alpha = nullptr;
};

TextStyle resolveTextStyle(uint8_t pixel_height, TextFont font = TextFont::Auto);
uint16_t glyphWidthPx(const GlyphBitmap &glyph, const TextStyle &style);
uint16_t glyphHeightPx(const GlyphBitmap &glyph, const TextStyle &style);
uint8_t glyphLetterSpacingPx(const GlyphBitmap &glyph, const TextStyle &style);
uint16_t textWidthPx(const String &text, uint8_t pixel_height, TextFont font = TextFont::Auto);
uint16_t textHeightPx(const String &text, uint8_t pixel_height, TextFont font = TextFont::Auto);
bool buildTextCoverageMap(const String &text, uint8_t pixel_height, TextFont font,
                          TextCoverageMap &map);
void freeTextCoverageMap(TextCoverageMap &map);
uint16_t textWidth3x5(const String &text, uint8_t scale, TextFont font = TextFont::Auto);
uint16_t textHeight3x5(const String &text, uint8_t scale, TextFont font = TextFont::Auto);
const uint8_t *glyph3x5(char c);
uint8_t glyphCoverage(const GlyphBitmap &glyph, uint8_t row, uint8_t col);
bool nextTextGlyph(const String &text, size_t &byte_index, GlyphBitmap &glyph,
                   TextFont font = TextFont::Auto);
String fallbackMissingGlyphs(const String &text, TextFont font = TextFont::Auto,
                             const char *fallback = "");
String sanitizeDisplayText(const String &text, const char *fallback = "");

}  // namespace calendar

#endif
