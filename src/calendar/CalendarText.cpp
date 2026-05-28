#include "calendar/CalendarText.h"

#include "fonts/AsciiNumericFont.h"
#include "fonts/AsciiSmoothFont.h"
#include "fonts/ZhSubsetFont.h"

namespace calendar {
namespace {

constexpr uint8_t kAsciiGlyphWidth = 5;
constexpr uint8_t kAsciiGlyphHeight = 7;
constexpr uint8_t kAscii6GlyphWidth = 4;
constexpr uint8_t kAscii6GlyphHeight = 6;
constexpr uint8_t kAscii8GlyphWidth = 5;
constexpr uint8_t kAscii8GlyphHeight = 8;
constexpr uint8_t kAscii10GlyphWidth = 5;
constexpr uint8_t kAscii10GlyphHeight = 10;
constexpr uint8_t kDigit10GlyphWidth = 6;
constexpr uint8_t kDigit10GlyphHeight = 10;
constexpr uint8_t kDigit14GlyphWidth = 8;
constexpr uint8_t kDigit14GlyphHeight = 14;
constexpr uint8_t kDigit16GlyphWidth = 9;
constexpr uint8_t kDigit16GlyphHeight = 16;
constexpr uint8_t kDigit26GlyphWidth = 16;
constexpr uint8_t kDigit26GlyphHeight = 26;
constexpr uint8_t kDigit30GlyphWidth = 18;
constexpr uint8_t kDigit30GlyphHeight = 30;
constexpr uint8_t kDigitMaxRowBytes = 3;

struct FontBoxMetrics {
  uint8_t left;
  uint8_t top;
  uint8_t width;
  uint8_t height;
};

struct GlyphRenderMetrics {
  uint8_t left;
  uint8_t top;
  uint8_t width;
  uint8_t height;
  uint8_t base_height;
};

FontBoxMetrics fontBoxMetrics(TextFont font) {
  switch (font) {
    case TextFont::AsciiSmooth:
      return FontBoxMetrics{0, 0, 0, fonts::kAsciiSmoothFontPx};
    case TextFont::Ascii6:
      return FontBoxMetrics{0, 0, kAscii6GlyphWidth, kAscii6GlyphHeight};
    case TextFont::Ascii8:
      return FontBoxMetrics{0, 0, kAscii8GlyphWidth, kAscii8GlyphHeight};
    case TextFont::Ascii10:
      return FontBoxMetrics{0, 0, kAscii10GlyphWidth, kAscii10GlyphHeight};
    case TextFont::AsciiSmooth16:
      return FontBoxMetrics{0, 0, 0, fonts::kAsciiSmooth16FontPx};
    case TextFont::AsciiSmooth14:
      return FontBoxMetrics{0, 0, 0, fonts::kAsciiSmooth14FontPx};
    case TextFont::Digit10:
      return FontBoxMetrics{0, 0, 0, kDigit10GlyphHeight};
    case TextFont::Digit14:
      return FontBoxMetrics{0, 0, 0, kDigit14GlyphHeight};
    case TextFont::Digit16:
      return FontBoxMetrics{0, 0, 0, kDigit16GlyphHeight};
    case TextFont::Digit26:
      return FontBoxMetrics{0, 0, 0, kDigit26GlyphHeight};
    case TextFont::Digit30:
      return FontBoxMetrics{0, 0, 0, kDigit30GlyphHeight};
    case TextFont::Cjk30:
      return FontBoxMetrics{fonts::kZhFont30BoxLeft, fonts::kZhFont30BoxTop,
                            fonts::kZhFont30BoxWidth, fonts::kZhFont30BoxHeight};
    case TextFont::Cjk26:
      return FontBoxMetrics{fonts::kZhFont26BoxLeft, fonts::kZhFont26BoxTop,
                            fonts::kZhFont26BoxWidth, fonts::kZhFont26BoxHeight};
    case TextFont::Cjk16:
      return FontBoxMetrics{fonts::kZhFont16BoxLeft, fonts::kZhFont16BoxTop,
                            fonts::kZhFont16BoxWidth, fonts::kZhFont16BoxHeight};
    case TextFont::Cjk10:
      return FontBoxMetrics{fonts::kZhFont10BoxLeft, fonts::kZhFont10BoxTop,
                            fonts::kZhFont10BoxWidth, fonts::kZhFont10BoxHeight};
    default:
      return FontBoxMetrics{0, 0, kAsciiGlyphWidth, kAsciiGlyphHeight};
  }
}

bool decodeNextUtf8Codepoint(const String &text, size_t &byte_index, uint32_t &codepoint) {
  if (byte_index >= text.length()) {
    return false;
  }

  const uint8_t first = static_cast<uint8_t>(text[byte_index++]);
  if ((first & 0x80u) == 0u) {
    codepoint = first;
    return true;
  }

  uint8_t remaining = 0;
  uint32_t value = 0;
  if ((first & 0xE0u) == 0xC0u) {
    remaining = 1;
    value = static_cast<uint32_t>(first & 0x1Fu);
  } else if ((first & 0xF0u) == 0xE0u) {
    remaining = 2;
    value = static_cast<uint32_t>(first & 0x0Fu);
  } else if ((first & 0xF8u) == 0xF0u) {
    remaining = 3;
    value = static_cast<uint32_t>(first & 0x07u);
  } else {
    codepoint = '?';
    return true;
  }

  for (uint8_t i = 0; i < remaining; ++i) {
    if (byte_index >= text.length()) {
      codepoint = '?';
      return true;
    }
    const uint8_t next = static_cast<uint8_t>(text[byte_index]);
    if ((next & 0xC0u) != 0x80u) {
      codepoint = '?';
      return true;
    }
    ++byte_index;
    value = static_cast<uint32_t>((value << 6) | (next & 0x3Fu));
  }

  codepoint = value;
  return true;
}

uint8_t requestedCjkPx(TextFont font) {
  switch (font) {
    case TextFont::Cjk30:
      return fonts::kZhFontPx30;
    case TextFont::Cjk26:
      return fonts::kZhFontPx26;
    case TextFont::Cjk16:
      return fonts::kZhFontPx16;
    case TextFont::Cjk10:
    default:
      return fonts::kZhFontPx10;
  }
}

uint8_t textBaseHeight(TextFont font) {
  return fontBoxMetrics(font).height;
}

GlyphRenderMetrics glyphRenderMetrics(const GlyphBitmap &glyph, const TextStyle &style) {
  if (glyph.bits_per_pixel > 1u) {
    return GlyphRenderMetrics{style.box_left, style.box_top, style.box_width, style.box_height,
                              style.base_height};
  }
  const bool fixed_bitmap_font =
      style.font == TextFont::Ascii6 || style.font == TextFont::Ascii8 ||
      style.font == TextFont::Ascii10 || style.font == TextFont::Digit10 ||
      style.font == TextFont::Digit14 || style.font == TextFont::Digit16 ||
      style.font == TextFont::Digit26 || style.font == TextFont::Digit30;
  const FontBoxMetrics ascii_box = fontBoxMetrics(fixed_bitmap_font ? style.font : TextFont::Auto);
  return GlyphRenderMetrics{ascii_box.left, ascii_box.top, ascii_box.width, ascii_box.height,
                            ascii_box.height};
}

uint8_t resolveCjkFontForPixelHeight(uint8_t pixel_height) {
  if (pixel_height >= 28u) {
    return fonts::kZhFontPx30;
  }
  if (pixel_height >= 21u) {
    return fonts::kZhFontPx26;
  }
  if (pixel_height >= 13u) {
    return fonts::kZhFontPx16;
  }
  return fonts::kZhFontPx10;
}

uint8_t effectivePixelHeight(uint8_t scale, TextFont font) {
  if (scale == 0) {
    return 0;
  }
  return static_cast<uint8_t>(textBaseHeight(font) * scale);
}

bool isAsciiLetter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isAsciiDigit(char c) {
  return (c >= '0' && c <= '9');
}

bool isSupportedAsciiGlyph(char c) {
  if (isAsciiLetter(c) || isAsciiDigit(c)) {
    return true;
  }

  switch (c) {
    case ' ':
    case '-':
    case '/':
    case ':':
    case '.':
    case '+':
    case '%':
    case '~':
    case '@':
    case '_':
    case ',':
    case '!':
    case '?':
    case '&':
    case '#':
    case '(':
    case ')':
    case '\'':
      return true;
    default:
      return false;
  }
}

bool hasZhGlyphForFont(uint32_t codepoint, TextFont font) {
  const uint8_t *data = nullptr;
  uint8_t width = 0;
  uint8_t height = 0;
  uint8_t row_bytes = 0;
  uint8_t bits_per_pixel = 0;
  return fonts::lookupCommonZhGlyph(codepoint, requestedCjkPx(font), data, width, height,
                                    row_bytes, bits_per_pixel);
}

struct PinyinFallbackEntry {
  uint32_t codepoint;
  const char *pinyin;
};

bool lookupPinyinFallback(uint32_t codepoint, const char *&pinyin) {
  static const PinyinFallbackEntry kEntries[] = {
      {0x4E00u, "YI"},     {0x4E09u, "SAN"},    {0x4E0Au, "SHANG"}, {0x4E0Bu, "XIA"},
      {0x4E8Cu, "ER"},     {0x4E94u, "WU"},     {0x4F1Au, "HUI"},   {0x4F5Cu, "ZUO"},
      {0x4F11u, "XIU"},    {0x516Du, "LIU"},    {0x533Bu, "YI"},    {0x5348u, "WU"},
      {0x5468u, "ZHOU"},   {0x56DBu, "SI"},     {0x591Au, "DUO"},   {0x5B89u, "AN"},
      {0x5BB6u, "JIA"},    {0x5DE5u, "GONG"},   {0x606Fu, "XI"},    {0x6392u, "PAI"},
      {0x65E0u, "WU"},     {0x65E5u, "RI"},     {0x65E9u, "ZAO"},   {0x65F6u, "SHI"},
      {0x66F4u, "GENG"},   {0x671Fu, "QI"},     {0x683Cu, "GE"},    {0x6821u, "XIAO"},
      {0x751Fu, "SHENG"},  {0x7528u, "YONG"},   {0x8A00u, "YAN"},   {0x8BAEu, "YI"},
      {0x8DEFu, "LU"},     {0x95F4u, "JIAN"},   {0x9662u, "YUAN"},  {0x661Fu, "XING"},
      {0x665Au, "WAN"},    {0x6D3Bu, "HUO"},
  };

  for (const auto &entry : kEntries) {
    if (entry.codepoint == codepoint) {
      pinyin = entry.pinyin;
      return true;
    }
  }

  pinyin = nullptr;
  return false;
}

void appendCollapsedSpace(String &out) {
  if (out.length() == 0 || out.charAt(out.length() - 1) == ' ') {
    return;
  }
  out += ' ';
}

String collapseAndTrimSpaces(const String &text) {
  String out;
  out.reserve(text.length());
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text.charAt(i);
    if (c == ' ') {
      appendCollapsedSpace(out);
      continue;
    }
    out += c;
  }
  out.trim();
  return out;
}

}  // namespace

TextStyle resolveTextStyle(uint8_t pixel_height, TextFont font) {
  TextStyle style;
  if (pixel_height == 0) {
    return style;
  }

  style.pixel_height = pixel_height;
  if (font == TextFont::Auto && pixel_height >= 14u) {
    style.font = TextFont::AsciiSmooth;
  } else if (font == TextFont::CjkAuto) {
    const uint8_t resolved_px = resolveCjkFontForPixelHeight(pixel_height);
    switch (resolved_px) {
      case fonts::kZhFontPx30:
        style.font = TextFont::Cjk30;
        break;
      case fonts::kZhFontPx26:
        style.font = TextFont::Cjk26;
        break;
      case fonts::kZhFontPx16:
        style.font = TextFont::Cjk16;
        break;
      default:
        style.font = TextFont::Cjk10;
        break;
    }
  } else {
    style.font = font;
  }
  const FontBoxMetrics box = fontBoxMetrics(style.font);
  style.base_height = textBaseHeight(style.font);
  style.box_left = box.left;
  style.box_top = box.top;
  style.box_width = box.width;
  style.box_height = box.height;
  style.letter_spacing =
      (style.font == TextFont::Digit10 || style.font == TextFont::Digit14 ||
       style.font == TextFont::Digit16 || style.font == TextFont::Digit26 ||
       style.font == TextFont::Digit30)
          ? 0u
          : 1u;
  return style;
}

uint16_t glyphWidthPx(const GlyphBitmap &glyph, const TextStyle &style) {
  const GlyphRenderMetrics metrics = glyphRenderMetrics(glyph, style);
  (void)style;
  return (metrics.width > 0u) ? metrics.width : glyph.width;
}

uint16_t glyphHeightPx(const GlyphBitmap &glyph, const TextStyle &style) {
  const GlyphRenderMetrics metrics = glyphRenderMetrics(glyph, style);
  (void)style;
  return (metrics.height > 0u) ? metrics.height : glyph.height;
}

uint8_t glyphLetterSpacingPx(const GlyphBitmap &glyph, const TextStyle &style) {
  (void)glyph;
  return style.letter_spacing;
}

uint16_t textWidthPx(const String &text, uint8_t pixel_height, TextFont font) {
  if (text.length() == 0 || pixel_height == 0) {
    return 0;
  }

  const TextStyle style = resolveTextStyle(pixel_height, font);
  uint16_t total = 0;
  size_t byte_index = 0;
  bool first = true;
  GlyphBitmap glyph;
  while (nextTextGlyph(text, byte_index, glyph, style.font)) {
    if (glyph.width == 0 || glyph.height == 0 || glyph.rows == nullptr) {
      continue;
    }
    if (!first) {
      total = static_cast<uint16_t>(total + glyphLetterSpacingPx(glyph, style));
    }
    total = static_cast<uint16_t>(total + glyphWidthPx(glyph, style));
    first = false;
  }
  return total;
}

uint16_t textHeightPx(const String &text, uint8_t pixel_height, TextFont font) {
  if (text.length() == 0 || pixel_height == 0) {
    return 0;
  }

  const TextStyle style = resolveTextStyle(pixel_height, font);
  uint8_t max_height = 0;
  size_t byte_index = 0;
  GlyphBitmap glyph;
  while (nextTextGlyph(text, byte_index, glyph, style.font)) {
    if (glyph.rows == nullptr || glyph.width == 0 || glyph.height == 0) {
      continue;
    }
    const uint8_t glyph_height = static_cast<uint8_t>(glyphHeightPx(glyph, style));
    if (glyph_height > max_height) {
      max_height = glyph_height;
    }
  }
  return max_height;
}

bool buildTextCoverageMap(const String &text, uint8_t pixel_height, TextFont font, TextCoverageMap &map) {
  freeTextCoverageMap(map);
  if (text.length() == 0 || pixel_height == 0) {
    return false;
  }

  const TextStyle style = resolveTextStyle(pixel_height, font);
  if (style.pixel_height == 0 || style.base_height == 0) {
    return false;
  }

  map.width = textWidthPx(text, pixel_height, font);
  map.height = textHeightPx(text, pixel_height, font);
  if (map.width == 0 || map.height == 0) {
    return false;
  }

  const size_t pixels = static_cast<size_t>(map.width) * map.height;
  map.alpha = static_cast<uint8_t *>(malloc(pixels));
  if (map.alpha == nullptr) {
    map.width = 0;
    map.height = 0;
    return false;
  }
  memset(map.alpha, 0, pixels);

  uint16_t pen_x = 0;
  size_t byte_index = 0;
  GlyphBitmap glyph;
  while (nextTextGlyph(text, byte_index, glyph, style.font)) {
    if (glyph.rows == nullptr || glyph.width == 0 || glyph.height == 0) {
      continue;
    }
    const uint16_t draw_w = glyphWidthPx(glyph, style);
    const uint16_t draw_h = glyphHeightPx(glyph, style);
    const GlyphRenderMetrics metrics = glyphRenderMetrics(glyph, style);
    const uint8_t src_top = metrics.top;
    const uint8_t src_left = metrics.left;
    const uint8_t src_h = (metrics.height > 0u) ? metrics.height : glyph.height;
    const uint8_t src_w = (metrics.width > 0u) ? metrics.width : glyph.width;

    for (uint16_t dy = 0; dy < draw_h; ++dy) {
      const uint8_t src_row =
          static_cast<uint8_t>(src_top + ((static_cast<uint32_t>(dy) * src_h) / draw_h));
      for (uint16_t dx = 0; dx < draw_w; ++dx) {
        const uint8_t src_col =
            static_cast<uint8_t>(src_left + ((static_cast<uint32_t>(dx) * src_w) / draw_w));
        const uint8_t coverage = glyphCoverage(glyph, src_row, src_col);
        if (coverage == 0u) {
          continue;
        }
        uint8_t alpha = 0;
        if (glyph.bits_per_pixel >= 4u) {
          alpha = static_cast<uint8_t>(coverage * 17u);
        } else if (glyph.bits_per_pixel == 2u) {
          alpha = static_cast<uint8_t>(coverage * 85u);
        } else {
          alpha = 255u;
        }
        const uint32_t dst = static_cast<uint32_t>(dy) * map.width + (pen_x + dx);
        if (dst < pixels && alpha > map.alpha[dst]) {
          map.alpha[dst] = alpha;
        }
      }
    }
    pen_x = static_cast<uint16_t>(pen_x + draw_w + glyphLetterSpacingPx(glyph, style));
  }

  return true;
}

void freeTextCoverageMap(TextCoverageMap &map) {
  if (map.alpha != nullptr) {
    free(map.alpha);
  }
  map.width = 0;
  map.height = 0;
  map.alpha = nullptr;
}

uint16_t textWidth3x5(const String &text, uint8_t scale, TextFont font) {
  return textWidthPx(text, effectivePixelHeight(scale, font), font);
}

uint16_t textHeight3x5(const String &text, uint8_t scale, TextFont font) {
  return textHeightPx(text, effectivePixelHeight(scale, font), font);
}

const uint8_t *glyph3x5(char c) {
  static const uint8_t kSpace[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t kDash[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
  static const uint8_t kSlash[7] = {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10};
  static const uint8_t kColon[7] = {0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00};
  static const uint8_t kDot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04};
  static const uint8_t kPlus[7] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
  static const uint8_t kPercent[7] = {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13};
  static const uint8_t kTilde[7] = {0x00, 0x00, 0x09, 0x16, 0x00, 0x00, 0x00};
  static const uint8_t kAt[7] = {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E};
  static const uint8_t kUnderscore[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
  static const uint8_t kComma[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08};
  static const uint8_t kBang[7] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04};
  static const uint8_t kQuestion[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
  static const uint8_t kAmp[7] = {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D};
  static const uint8_t kHash[7] = {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A};
  static const uint8_t kLParen[7] = {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02};
  static const uint8_t kRParen[7] = {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08};
  static const uint8_t kApostrophe[7] = {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t kUnknown[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
  static const uint8_t kDigits[10][7] = {
      {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},  // 0
      {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  // 1
      {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},  // 2
      {0x1E, 0x01, 0x01, 0x06, 0x01, 0x01, 0x1E},  // 3
      {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},  // 4
      {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},  // 5
      {0x07, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},  // 6
      {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},  // 7
      {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},  // 8
      {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x1C},  // 9
  };
  static const uint8_t kUpper[26][7] = {
      {0x04, 0x0A, 0x11, 0x11, 0x1F, 0x11, 0x11},  // A
      {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},  // B
      {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},  // C
      {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C},  // D
      {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},  // E
      {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},  // F
      {0x0E, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0E},  // G
      {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},  // H
      {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},  // I
      {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E},  // J
      {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},  // K
      {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},  // L
      {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},  // M
      {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11},  // N
      {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},  // O
      {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},  // P
      {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},  // Q
      {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},  // R
      {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},  // S
      {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},  // T
      {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},  // U
      {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},  // V
      {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A},  // W
      {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},  // X
      {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},  // Y
      {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},  // Z
  };
  if (c >= '0' && c <= '9') {
    return kDigits[c - '0'];
  }
  if (c >= 'a' && c <= 'z') {
    return kUpper[c - 'a'];
  }
  if (c >= 'A' && c <= 'Z') {
    return kUpper[c - 'A'];
  }
  if (c == '-') {
    return kDash;
  }
  if (c == '/') {
    return kSlash;
  }
  if (c == ':') {
    return kColon;
  }
  if (c == '.') {
    return kDot;
  }
  if (c == '+') {
    return kPlus;
  }
  if (c == '%') {
    return kPercent;
  }
  if (c == '~') {
    return kTilde;
  }
  if (c == '@') {
    return kAt;
  }
  if (c == '_') {
    return kUnderscore;
  }
  if (c == ',') {
    return kComma;
  }
  if (c == '!') {
    return kBang;
  }
  if (c == '?') {
    return kQuestion;
  }
  if (c == '&') {
    return kAmp;
  }
  if (c == '#') {
    return kHash;
  }
  if (c == '(') {
    return kLParen;
  }
  if (c == ')') {
    return kRParen;
  }
  if (c == '\'') {
    return kApostrophe;
  }
  if (c == ' ') {
    return kSpace;
  }
  return kUnknown;
}

const uint8_t *glyph4x6(char c) {
  static const uint8_t kSpace[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t kDash[6] = {0x00, 0x00, 0x00, 0xE0, 0x00, 0x00};
  static const uint8_t kSlash[6] = {0x10, 0x10, 0x20, 0x40, 0x80, 0x80};
  static const uint8_t kColon[6] = {0x00, 0x40, 0x00, 0x00, 0x40, 0x00};
  static const uint8_t kDot[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x40};
  static const uint8_t kPlus[6] = {0x00, 0x40, 0xE0, 0x40, 0x00, 0x00};
  static const uint8_t kPercent[6] = {0x90, 0x20, 0x40, 0x80, 0x90, 0x00};
  static const uint8_t kTilde[6] = {0x00, 0x00, 0x50, 0xA0, 0x00, 0x00};
  static const uint8_t kAt[6] = {0x60, 0x90, 0xB0, 0xB0, 0x80, 0x70};
  static const uint8_t kUnderscore[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xF0};
  static const uint8_t kComma[6] = {0x00, 0x00, 0x00, 0x00, 0x40, 0x80};
  static const uint8_t kBang[6] = {0x40, 0x40, 0x40, 0x40, 0x00, 0x40};
  static const uint8_t kQuestion[6] = {0x60, 0x90, 0x20, 0x40, 0x00, 0x40};
  static const uint8_t kAmp[6] = {0x60, 0x90, 0x40, 0xA0, 0x90, 0x60};
  static const uint8_t kHash[6] = {0x50, 0xF0, 0x50, 0xF0, 0x50, 0x00};
  static const uint8_t kLParen[6] = {0x20, 0x40, 0x80, 0x80, 0x40, 0x20};
  static const uint8_t kRParen[6] = {0x80, 0x40, 0x20, 0x20, 0x40, 0x80};
  static const uint8_t kApostrophe[6] = {0x40, 0x40, 0x80, 0x00, 0x00, 0x00};
  static const uint8_t kUnknown[6] = {0x60, 0x90, 0x20, 0x40, 0x00, 0x40};
  static const uint8_t kDigits[10][6] = {
      {0x60, 0x90, 0xB0, 0xD0, 0x90, 0x60}, {0x40, 0xC0, 0x40, 0x40, 0x40, 0xE0},
      {0x60, 0x90, 0x10, 0x20, 0x40, 0xF0}, {0xE0, 0x10, 0x60, 0x10, 0x10, 0xE0},
      {0x20, 0x60, 0xA0, 0xF0, 0x20, 0x20}, {0xF0, 0x80, 0xE0, 0x10, 0x10, 0xE0},
      {0x60, 0x80, 0xE0, 0x90, 0x90, 0x60}, {0xF0, 0x10, 0x20, 0x40, 0x40, 0x40},
      {0x60, 0x90, 0x60, 0x90, 0x90, 0x60}, {0x60, 0x90, 0x90, 0x70, 0x10, 0x60},
  };
  static const uint8_t kUpper[26][6] = {
      {0x60, 0x90, 0x90, 0xF0, 0x90, 0x90}, {0xE0, 0x90, 0xE0, 0x90, 0x90, 0xE0},
      {0x70, 0x80, 0x80, 0x80, 0x80, 0x70}, {0xE0, 0x90, 0x90, 0x90, 0x90, 0xE0},
      {0xF0, 0x80, 0xE0, 0x80, 0x80, 0xF0}, {0xF0, 0x80, 0xE0, 0x80, 0x80, 0x80},
      {0x70, 0x80, 0xB0, 0x90, 0x90, 0x70}, {0x90, 0x90, 0xF0, 0x90, 0x90, 0x90},
      {0xE0, 0x40, 0x40, 0x40, 0x40, 0xE0}, {0x10, 0x10, 0x10, 0x10, 0x90, 0x60},
      {0x90, 0xA0, 0xC0, 0xA0, 0x90, 0x90}, {0x80, 0x80, 0x80, 0x80, 0x80, 0xF0},
      {0x90, 0xF0, 0xF0, 0x90, 0x90, 0x90}, {0x90, 0xD0, 0xB0, 0x90, 0x90, 0x90},
      {0x60, 0x90, 0x90, 0x90, 0x90, 0x60}, {0xE0, 0x90, 0x90, 0xE0, 0x80, 0x80},
      {0x60, 0x90, 0x90, 0x90, 0xA0, 0x50}, {0xE0, 0x90, 0x90, 0xE0, 0xA0, 0x90},
      {0x70, 0x80, 0x60, 0x10, 0x10, 0xE0}, {0xF0, 0x40, 0x40, 0x40, 0x40, 0x40},
      {0x90, 0x90, 0x90, 0x90, 0x90, 0x60}, {0x90, 0x90, 0x90, 0x90, 0x60, 0x60},
      {0x90, 0x90, 0x90, 0xF0, 0xF0, 0x90}, {0x90, 0x90, 0x60, 0x60, 0x90, 0x90},
      {0x90, 0x90, 0x60, 0x40, 0x40, 0x40}, {0xF0, 0x10, 0x20, 0x40, 0x80, 0xF0},
  };
  if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  if (c >= 'A' && c <= 'Z') return kUpper[c - 'A'];
  if (c >= '0' && c <= '9') return kDigits[c - '0'];
  if (c == ' ') return kSpace;
  if (c == '-') return kDash;
  if (c == '/') return kSlash;
  if (c == ':') return kColon;
  if (c == '.') return kDot;
  if (c == '+') return kPlus;
  if (c == '%') return kPercent;
  if (c == '~') return kTilde;
  if (c == '@') return kAt;
  if (c == '_') return kUnderscore;
  if (c == ',') return kComma;
  if (c == '!') return kBang;
  if (c == '?') return kQuestion;
  if (c == '&') return kAmp;
  if (c == '#') return kHash;
  if (c == '(') return kLParen;
  if (c == ')') return kRParen;
  if (c == '\'') return kApostrophe;
  return kUnknown;
}

const uint8_t *glyph5x10(char c) {
  static uint8_t out[kAscii10GlyphHeight] = {};
  const uint8_t *base = glyph3x5(c);
  out[0] = 0x00;
  out[1] = 0x00;
  for (uint8_t row = 0; row < kAsciiGlyphHeight; ++row) {
    out[row + 2u] = base[row];
  }
  out[9] = 0x00;
  return out;
}

const uint8_t *glyph5x8(char c) {
  static uint8_t out[kAscii8GlyphHeight] = {};
  const uint8_t *base = glyph3x5(c);
  for (uint8_t row = 0; row < kAsciiGlyphHeight; ++row) {
    out[row] = base[row];
  }
  out[7] = 0x00;
  return out;
}

uint8_t digitGlyphHeight(TextFont font) {
  switch (font) {
    case TextFont::Digit30:
      return kDigit30GlyphHeight;
    case TextFont::Digit26:
      return kDigit26GlyphHeight;
    case TextFont::Digit16:
      return kDigit16GlyphHeight;
    case TextFont::Digit14:
      return kDigit14GlyphHeight;
    case TextFont::Digit10:
    default:
      return kDigit10GlyphHeight;
  }
}

uint8_t digitGlyphWidth(TextFont font, char c) {
  if (c == ':') {
    switch (font) {
      case TextFont::Digit30:
        return 6u;
      case TextFont::Digit26:
        return 5u;
      case TextFont::Digit16:
        return 4u;
      case TextFont::Digit14:
        return 4u;
      case TextFont::Digit10:
      default:
        return 3u;
    }
  }
  switch (font) {
    case TextFont::Digit30:
      return kDigit30GlyphWidth;
    case TextFont::Digit26:
      return kDigit26GlyphWidth;
    case TextFont::Digit16:
      return kDigit16GlyphWidth;
    case TextFont::Digit14:
      return kDigit14GlyphWidth;
    case TextFont::Digit10:
    default:
      return kDigit10GlyphWidth;
  }
}

uint8_t digitGlyphRowBytes(TextFont font, char c) {
  return static_cast<uint8_t>((digitGlyphWidth(font, c) + 7u) / 8u);
}

void setDigitGlyphPixel(uint8_t *rows, uint8_t row_bytes, uint8_t x, uint8_t y) {
  const uint16_t index = static_cast<uint16_t>(y) * row_bytes + (x >> 3);
  rows[index] = static_cast<uint8_t>(rows[index] | (1u << (7u - (x & 0x07u))));
}

void fillDigitGlyphRect(uint8_t *rows, uint8_t row_bytes, uint8_t glyph_w, uint8_t glyph_h,
                        int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) {
    return;
  }
  int x0 = x;
  int y0 = y;
  int x1 = x + w;
  int y1 = y + h;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > glyph_w) x1 = glyph_w;
  if (y1 > glyph_h) y1 = glyph_h;
  for (int yy = y0; yy < y1; ++yy) {
    for (int xx = x0; xx < x1; ++xx) {
      setDigitGlyphPixel(rows, row_bytes, static_cast<uint8_t>(xx), static_cast<uint8_t>(yy));
    }
  }
}

uint8_t digitSegments(char c) {
  switch (c) {
    case '0':
      return 0x3Fu;
    case '1':
      return 0x06u;
    case '2':
      return 0x5Bu;
    case '3':
      return 0x4Fu;
    case '4':
      return 0x66u;
    case '5':
      return 0x6Du;
    case '6':
      return 0x7Du;
    case '7':
      return 0x07u;
    case '8':
      return 0x7Fu;
    case '9':
      return 0x6Fu;
    default:
      return 0u;
  }
}

const uint8_t *digitGlyph(char c, TextFont font, uint8_t &width, uint8_t &height,
                          uint8_t &row_bytes) {
  static uint8_t out[kDigit30GlyphHeight * kDigitMaxRowBytes] = {};
  width = digitGlyphWidth(font, c);
  height = digitGlyphHeight(font);
  row_bytes = digitGlyphRowBytes(font, c);
  memset(out, 0, sizeof(out));

  if (c == ' ') {
    return out;
  }
  const uint8_t t = static_cast<uint8_t>(std::max<uint8_t>(1u, height / 7u));
  if (c == ':') {
    const uint8_t dot = static_cast<uint8_t>(std::max<uint8_t>(1u, t));
    const uint8_t dot_x = (width > dot) ? static_cast<uint8_t>((width - dot) / 2u) : 0u;
    fillDigitGlyphRect(out, row_bytes, width, height, dot_x, static_cast<int>(height / 3u), dot,
                       dot);
    fillDigitGlyphRect(out, row_bytes, width, height, dot_x,
                       static_cast<int>((height * 2u) / 3u), dot, dot);
    return out;
  }

  const uint8_t segments = digitSegments(c);
  const uint8_t mid_y = static_cast<uint8_t>((height - t) / 2u);
  const uint8_t right_x = (width > t) ? static_cast<uint8_t>(width - t) : 0u;
  const uint8_t inner_w = (width > 2u * t) ? static_cast<uint8_t>(width - 2u * t) : width;
  const uint8_t upper_h = (mid_y > t) ? static_cast<uint8_t>(mid_y - t) : 1u;
  const uint8_t lower_y = static_cast<uint8_t>(mid_y + t);
  const uint8_t lower_h = (height > lower_y + t) ? static_cast<uint8_t>(height - lower_y - t) : 1u;
  if (segments & 0x01u) fillDigitGlyphRect(out, row_bytes, width, height, t, 0, inner_w, t);
  if (segments & 0x02u) fillDigitGlyphRect(out, row_bytes, width, height, right_x, t, t, upper_h);
  if (segments & 0x04u) fillDigitGlyphRect(out, row_bytes, width, height, right_x, lower_y, t, lower_h);
  if (segments & 0x08u) fillDigitGlyphRect(out, row_bytes, width, height, t, height - t, inner_w, t);
  if (segments & 0x10u) fillDigitGlyphRect(out, row_bytes, width, height, 0, lower_y, t, lower_h);
  if (segments & 0x20u) fillDigitGlyphRect(out, row_bytes, width, height, 0, t, t, upper_h);
  if (segments & 0x40u) fillDigitGlyphRect(out, row_bytes, width, height, t, mid_y, inner_w, t);
  return out;
}

uint8_t glyphCoverage(const GlyphBitmap &glyph, uint8_t row, uint8_t col) {
  if (glyph.rows == nullptr || row >= glyph.height || col >= glyph.width) {
    return 0;
  }
  if (glyph.bits_per_pixel == 4) {
    const uint8_t *src = glyph.rows + static_cast<uint16_t>(row) * glyph.row_bytes + (col >> 1);
    const uint8_t shift = static_cast<uint8_t>((1u - (col & 0x01u)) * 4u);
    return static_cast<uint8_t>((*src >> shift) & 0x0Fu);
  }
  if (glyph.bits_per_pixel == 2) {
    const uint8_t *src = glyph.rows + static_cast<uint16_t>(row) * glyph.row_bytes + (col >> 2);
    const uint8_t shift = static_cast<uint8_t>((3u - (col & 0x03u)) * 2u);
    return static_cast<uint8_t>((*src >> shift) & 0x03u);
  }
  if (glyph.row_bytes == 1u) {
    const uint8_t bits = glyph.rows[row];
    if (glyph.width == kAscii6GlyphWidth && glyph.height == kAscii6GlyphHeight) {
      return (bits & (1u << (7u - col))) ? 3u : 0u;
    }
    return (bits & (1u << (glyph.width - 1u - col))) ? 3u : 0u;
  }
  const uint8_t *src = glyph.rows + static_cast<uint16_t>(row) * glyph.row_bytes + (col >> 3);
  const uint8_t bit = static_cast<uint8_t>(7u - (col & 0x07u));
  return ((*src) & (1u << bit)) ? 3u : 0u;
}

bool nextTextGlyph(const String &text, size_t &byte_index, GlyphBitmap &glyph, TextFont font) {
  glyph = GlyphBitmap{};
  if (byte_index >= text.length()) {
    return false;
  }

  uint32_t codepoint = 0;
  if (!decodeNextUtf8Codepoint(text, byte_index, codepoint)) {
    return false;
  }

  if (codepoint < 0x80u) {
    if (font == TextFont::Digit10 || font == TextFont::Digit14 || font == TextFont::Digit16 ||
        font == TextFont::Digit26 || font == TextFont::Digit30) {
      const uint8_t numeric_px =
          (font == TextFont::Digit30)
              ? kDigit30GlyphHeight
              : (font == TextFont::Digit26
                     ? kDigit26GlyphHeight
                     : (font == TextFont::Digit16
                            ? kDigit16GlyphHeight
                            : (font == TextFont::Digit14 ? kDigit14GlyphHeight
                                                          : kDigit10GlyphHeight)));
      if (fonts::lookupAsciiNumericGlyph(static_cast<char>(codepoint), numeric_px, glyph.rows,
                                         glyph.width, glyph.height, glyph.row_bytes,
                                         glyph.bits_per_pixel)) {
        return true;
      }
    }
    if (font == TextFont::Ascii6) {
      glyph.rows = glyph4x6(static_cast<char>(codepoint));
      glyph.width = kAscii6GlyphWidth;
      glyph.height = kAscii6GlyphHeight;
      glyph.row_bytes = 1;
      glyph.bits_per_pixel = 1;
      return true;
    }
    if (font == TextFont::Ascii8) {
      glyph.rows = glyph5x8(static_cast<char>(codepoint));
      glyph.width = kAscii8GlyphWidth;
      glyph.height = kAscii8GlyphHeight;
      glyph.row_bytes = 1;
      glyph.bits_per_pixel = 1;
      return true;
    }
    if (font == TextFont::Ascii10) {
      glyph.rows = glyph5x10(static_cast<char>(codepoint));
      glyph.width = kAscii10GlyphWidth;
      glyph.height = kAscii10GlyphHeight;
      glyph.row_bytes = 1;
      glyph.bits_per_pixel = 1;
      return true;
    }
    if (font == TextFont::AsciiSmooth) {
      if (fonts::lookupAsciiSmoothGlyph(static_cast<char>(codepoint), glyph.rows, glyph.width,
                                        glyph.height, glyph.row_bytes, glyph.bits_per_pixel)) {
        return true;
      }
    }
    if (font == TextFont::AsciiSmooth16) {
      if (fonts::lookupAsciiSmooth16Glyph(static_cast<char>(codepoint), glyph.rows, glyph.width,
                                          glyph.height, glyph.row_bytes, glyph.bits_per_pixel)) {
        return true;
      }
    }
    if (font == TextFont::AsciiSmooth14) {
      if (fonts::lookupAsciiSmooth14Glyph(static_cast<char>(codepoint), glyph.rows, glyph.width,
                                          glyph.height, glyph.row_bytes, glyph.bits_per_pixel)) {
        return true;
      }
    }
    glyph.rows = glyph3x5(static_cast<char>(codepoint));
    glyph.width = kAsciiGlyphWidth;
    glyph.height = kAsciiGlyphHeight;
    glyph.row_bytes = 1;
    glyph.bits_per_pixel = 1;
    return true;
  }

  const uint8_t *zh = nullptr;
  uint8_t width = 0;
  uint8_t height = 0;
  uint8_t row_bytes = 0;
  uint8_t bits_per_pixel = 0;
  if (fonts::lookupCommonZhGlyph(codepoint, requestedCjkPx(font), zh, width, height, row_bytes,
                                 bits_per_pixel)) {
    glyph.rows = zh;
    glyph.width = width;
    glyph.height = height;
    glyph.row_bytes = row_bytes;
    glyph.bits_per_pixel = bits_per_pixel;
    return true;
  }

  if (font == TextFont::AsciiSmooth) {
    if (fonts::lookupAsciiSmoothGlyph('?', glyph.rows, glyph.width, glyph.height, glyph.row_bytes,
                                      glyph.bits_per_pixel)) {
      return true;
    }
  }
  glyph.rows = glyph3x5('?');
  glyph.width = kAsciiGlyphWidth;
  glyph.height = kAsciiGlyphHeight;
  glyph.row_bytes = 1;
  glyph.bits_per_pixel = 1;
  return true;
}

String fallbackMissingGlyphs(const String &text, TextFont font, const char *fallback) {
  String out;
  out.reserve(text.length() + 16);

  size_t byte_index = 0;
  while (byte_index < text.length()) {
    const size_t start = byte_index;
    uint32_t codepoint = 0;
    if (!decodeNextUtf8Codepoint(text, byte_index, codepoint)) {
      break;
    }

    if (codepoint < 0x80u) {
      const char c = static_cast<char>(codepoint);
      if (c == '\r' || c == '\n' || c == '\t') {
        appendCollapsedSpace(out);
        continue;
      }
      if (isAsciiLetter(c)) {
        out += static_cast<char>(toupper(static_cast<unsigned char>(c)));
        continue;
      }
      if (isSupportedAsciiGlyph(c)) {
        if (c == ' ') {
          appendCollapsedSpace(out);
        } else {
          out += c;
        }
        continue;
      }
      out += '?';
      continue;
    }

    if (hasZhGlyphForFont(codepoint, font)) {
      out += text.substring(start, byte_index);
      continue;
    }

    const char *romanized = nullptr;
    if (lookupPinyinFallback(codepoint, romanized)) {
      appendCollapsedSpace(out);
      out += romanized;
      appendCollapsedSpace(out);
      continue;
    }

    appendCollapsedSpace(out);
    out += '?';
    appendCollapsedSpace(out);
  }

  out = collapseAndTrimSpaces(out);
  if (out.length() == 0 && fallback != nullptr && fallback[0] != '\0') {
    out = fallback;
  }
  return out;
}

String sanitizeDisplayText(const String &text, const char *fallback) {
  String out;
  out.reserve(text.length());
  size_t byte_index = 0;
  uint32_t codepoint = 0;
  while (decodeNextUtf8Codepoint(text, byte_index, codepoint)) {
    if (codepoint >= 0x80u) {
      continue;
    }

    const char c = static_cast<char>(codepoint);
    if (isAsciiLetter(c)) {
      out += static_cast<char>(toupper(static_cast<unsigned char>(c)));
    } else if (isSupportedAsciiGlyph(c)) {
      if (c == ' ') {
        appendCollapsedSpace(out);
      } else {
        out += c;
      }
    }
  }

  out = collapseAndTrimSpaces(out);
  if (out.length() == 0 && fallback != nullptr && fallback[0] != '\0') {
    out = fallback;
  }
  return out;
}

}  // namespace calendar
