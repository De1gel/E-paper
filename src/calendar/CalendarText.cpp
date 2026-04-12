#include "calendar/CalendarText.h"

#include "fonts/ZhSubsetFont.h"

namespace calendar {
namespace {

struct FontBoxMetrics {
  uint8_t left;
  uint8_t top;
  uint8_t width;
  uint8_t height;
};

FontBoxMetrics fontBoxMetrics(TextFont font) {
  switch (font) {
    case TextFont::Cjk24:
      return FontBoxMetrics{fonts::kZhFont24BoxLeft, fonts::kZhFont24BoxTop,
                            fonts::kZhFont24BoxWidth, fonts::kZhFont24BoxHeight};
    case TextFont::Cjk16:
      return FontBoxMetrics{fonts::kZhFont16BoxLeft, fonts::kZhFont16BoxTop,
                            fonts::kZhFont16BoxWidth, fonts::kZhFont16BoxHeight};
    default:
      return FontBoxMetrics{0, 0, 3, 5};
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
  return (font == TextFont::Cjk24) ? fonts::kZhFontPx24 : fonts::kZhFontPx16;
}

uint8_t textBaseHeight(TextFont font) {
  return fontBoxMetrics(font).height;
}

uint8_t resolveCjkFontForPixelHeight(uint8_t pixel_height) {
  const FontBoxMetrics box16 = fontBoxMetrics(TextFont::Cjk16);
  const FontBoxMetrics box24 = fontBoxMetrics(TextFont::Cjk24);
  const int dist16 = abs(static_cast<int>(pixel_height) - static_cast<int>(box16.height));
  const int dist24 = abs(static_cast<int>(pixel_height) - static_cast<int>(box24.height));
  return (dist16 <= dist24) ? fonts::kZhFontPx16 : fonts::kZhFontPx24;
}

uint8_t scaledDimension(uint8_t source_dim, const TextStyle &style) {
  if (source_dim == 0 || style.pixel_height == 0 || style.base_height == 0) {
    return 0;
  }
  const uint16_t scaled =
      static_cast<uint16_t>((static_cast<uint32_t>(source_dim) * style.pixel_height +
                             (style.base_height / 2u)) /
                            style.base_height);
  return (scaled == 0u) ? 1u : static_cast<uint8_t>(scaled);
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
    case '~':
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
  if (font == TextFont::CjkAuto) {
    const uint8_t resolved_px = resolveCjkFontForPixelHeight(pixel_height);
    style.font = (resolved_px >= fonts::kZhFontPx24) ? TextFont::Cjk24 : TextFont::Cjk16;
  } else {
    style.font = font;
  }
  const FontBoxMetrics box = fontBoxMetrics(style.font);
  style.base_height = textBaseHeight(style.font);
  style.box_left = box.left;
  style.box_top = box.top;
  style.box_width = box.width;
  style.box_height = box.height;

  const uint16_t spacing =
      static_cast<uint16_t>((style.pixel_height + (style.base_height / 2u)) / style.base_height);
  style.letter_spacing = static_cast<uint8_t>((spacing == 0u) ? 1u : spacing);
  return style;
}

uint16_t glyphWidthPx(const GlyphBitmap &glyph, const TextStyle &style) {
  const uint8_t source_width =
      (glyph.bits_per_pixel > 1u && style.box_width > 0u) ? style.box_width : glyph.width;
  return scaledDimension(source_width, style);
}

uint16_t glyphHeightPx(const GlyphBitmap &glyph, const TextStyle &style) {
  const uint8_t source_height =
      (glyph.bits_per_pixel > 1u && style.box_height > 0u) ? style.box_height : glyph.height;
  return scaledDimension(source_height, style);
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
      total = static_cast<uint16_t>(total + style.letter_spacing);
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
    const uint8_t src_top = (glyph.bits_per_pixel > 1u) ? style.box_top : 0u;
    const uint8_t src_left = (glyph.bits_per_pixel > 1u) ? style.box_left : 0u;
    const uint8_t src_h =
        (glyph.bits_per_pixel > 1u && style.box_height > 0u) ? style.box_height : glyph.height;
    const uint8_t src_w =
        (glyph.bits_per_pixel > 1u && style.box_width > 0u) ? style.box_width : glyph.width;

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
    pen_x = static_cast<uint16_t>(pen_x + draw_w + style.letter_spacing);
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
  static const uint8_t kSpace[5] = {0x0, 0x0, 0x0, 0x0, 0x0};
  static const uint8_t kDash[5] = {0x0, 0x0, 0x7, 0x0, 0x0};
  static const uint8_t kSlash[5] = {0x1, 0x1, 0x2, 0x4, 0x4};
  static const uint8_t kColon[5] = {0x0, 0x2, 0x0, 0x2, 0x0};
  static const uint8_t kDot[5] = {0x0, 0x0, 0x0, 0x0, 0x2};
  static const uint8_t kPlus[5] = {0x0, 0x2, 0x7, 0x2, 0x0};
  static const uint8_t kTilde[5] = {0x0, 0x3, 0x6, 0x0, 0x0};
  static const uint8_t kUnknown[5] = {0x7, 0x1, 0x2, 0x0, 0x2};
  static const uint8_t kDigits[10][5] = {
      {0x7, 0x5, 0x5, 0x5, 0x7},  // 0
      {0x2, 0x6, 0x2, 0x2, 0x7},  // 1
      {0x7, 0x1, 0x7, 0x4, 0x7},  // 2
      {0x7, 0x1, 0x7, 0x1, 0x7},  // 3
      {0x5, 0x5, 0x7, 0x1, 0x1},  // 4
      {0x7, 0x4, 0x7, 0x1, 0x7},  // 5
      {0x7, 0x4, 0x7, 0x5, 0x7},  // 6
      {0x7, 0x1, 0x1, 0x1, 0x1},  // 7
      {0x7, 0x5, 0x7, 0x5, 0x7},  // 8
      {0x7, 0x5, 0x7, 0x1, 0x7},  // 9
  };
  static const uint8_t kUpper[26][5] = {
      {0x2, 0x5, 0x7, 0x5, 0x5},  // A
      {0x6, 0x5, 0x6, 0x5, 0x6},  // B
      {0x3, 0x4, 0x4, 0x4, 0x3},  // C
      {0x6, 0x5, 0x5, 0x5, 0x6},  // D
      {0x7, 0x4, 0x6, 0x4, 0x7},  // E
      {0x7, 0x4, 0x6, 0x4, 0x4},  // F
      {0x3, 0x4, 0x5, 0x5, 0x3},  // G
      {0x5, 0x5, 0x7, 0x5, 0x5},  // H
      {0x7, 0x2, 0x2, 0x2, 0x7},  // I
      {0x1, 0x1, 0x1, 0x5, 0x2},  // J
      {0x5, 0x5, 0x6, 0x5, 0x5},  // K
      {0x4, 0x4, 0x4, 0x4, 0x7},  // L
      {0x5, 0x7, 0x7, 0x5, 0x5},  // M
      {0x5, 0x7, 0x7, 0x7, 0x5},  // N
      {0x2, 0x5, 0x5, 0x5, 0x2},  // O
      {0x6, 0x5, 0x6, 0x4, 0x4},  // P
      {0x2, 0x5, 0x5, 0x3, 0x1},  // Q
      {0x6, 0x5, 0x6, 0x5, 0x5},  // R
      {0x3, 0x4, 0x2, 0x1, 0x6},  // S
      {0x7, 0x2, 0x2, 0x2, 0x2},  // T
      {0x5, 0x5, 0x5, 0x5, 0x7},  // U
      {0x5, 0x5, 0x5, 0x5, 0x2},  // V
      {0x5, 0x5, 0x7, 0x7, 0x5},  // W
      {0x5, 0x5, 0x2, 0x5, 0x5},  // X
      {0x5, 0x5, 0x2, 0x2, 0x2},  // Y
      {0x7, 0x1, 0x2, 0x4, 0x7},  // Z
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
  if (c == '~') {
    return kTilde;
  }
  if (c == ' ') {
    return kSpace;
  }
  return kUnknown;
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
  const uint8_t bits = glyph.rows[row];
  return (bits & (1u << (glyph.width - 1u - col))) ? 3u : 0u;
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
    glyph.rows = glyph3x5(static_cast<char>(codepoint));
    glyph.width = 3;
    glyph.height = 5;
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

  glyph.rows = glyph3x5('?');
  glyph.width = 3;
  glyph.height = 5;
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
