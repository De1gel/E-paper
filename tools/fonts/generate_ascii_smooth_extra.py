#!/usr/bin/env python3
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_FONT = ROOT / "MSYH.TTC"
DEFAULT_OUT = ROOT / "src" / "fonts" / "AsciiSmoothFontExtra.cpp"
GLYPH_CHARS = " ?%+-./0123456789:@ABCDEFGHIJKLMNOPQRSTUVWXYZ~"


@dataclass(frozen=True)
class Glyph:
  width: int
  row_bytes: int
  offset: int
  data: bytes


def pack_4bpp(canvas: Image.Image, width: int, height: int) -> bytes:
  out = bytearray()
  for y in range(height):
    for x in range(0, width, 2):
      hi = min(15, (canvas.getpixel((x, y)) * 16) // 256)
      lo = 0
      if x + 1 < width:
        lo = min(15, (canvas.getpixel((x + 1, y)) * 16) // 256)
      out.append(((hi & 0x0F) << 4) | (lo & 0x0F))
  return bytes(out)


def render_glyph(font: ImageFont.FreeTypeFont, ch: str, px: int) -> Glyph:
  if ch == " ":
    width = max(4, round(px * 0.42))
    canvas = Image.new("L", (width, px), 0)
    return Glyph(width=width, row_bytes=(width + 1) // 2, offset=0,
                 data=pack_4bpp(canvas, width, px))

  probe = Image.new("L", (px * 3, px * 2), 0)
  draw = ImageDraw.Draw(probe)
  bbox = draw.textbbox((0, 0), ch, font=font)
  glyph_w = max(1, bbox[2] - bbox[0])
  glyph_h = max(1, bbox[3] - bbox[1])
  pad_x = max(1, round(px * 0.08))
  width = max(2, glyph_w + pad_x * 2)
  canvas = Image.new("L", (width, px), 0)
  draw = ImageDraw.Draw(canvas)
  x = pad_x - bbox[0]
  y = (px - glyph_h) // 2 - bbox[1]
  draw.text((x, y), ch, fill=255, font=font)
  return Glyph(width=width, row_bytes=(width + 1) // 2, offset=0,
               data=pack_4bpp(canvas, width, px))


def glyphs_for_size(px: int) -> list[Glyph]:
  # Microsoft YaHei renders smaller than its point size; a slight oversize keeps
  # the 14 px face visually aligned. The 16 px face is rasterized at its exact
  # target size so its strokes come from the vector outline rather than scaling.
  font_size = px if px == 16 else max(8, round(px * 1.06))
  font = ImageFont.truetype(str(DEFAULT_FONT), font_size, index=0)
  glyphs = [render_glyph(font, ch, px) for ch in GLYPH_CHARS]
  offset = 0
  with_offsets: list[Glyph] = []
  for glyph in glyphs:
    with_offsets.append(Glyph(glyph.width, glyph.row_bytes, offset, glyph.data))
    offset += len(glyph.data)
  return with_offsets


def format_array(values: list[int], indent: str = "  ", width: int = 16) -> str:
  lines: list[str] = []
  for i in range(0, len(values), width):
    chunk = values[i:i + width]
    lines.append(indent + ", ".join(str(v) for v in chunk))
  return ",\n".join(lines)


def format_hex(values: bytes, indent: str = "  ", width: int = 16) -> str:
  lines: list[str] = []
  for i in range(0, len(values), width):
    chunk = values[i:i + width]
    lines.append(indent + ", ".join(f"0x{v:02X}" for v in chunk))
  return ",\n".join(lines)


def emit_size(px: int) -> str:
  glyphs = glyphs_for_size(px)
  data = b"".join(g.data for g in glyphs)
  widths = [g.width for g in glyphs]
  row_bytes = [g.row_bytes for g in glyphs]
  offsets = [g.offset for g in glyphs]
  return f"""
constexpr uint8_t kGlyphWidths{px}[kAsciiSmoothExtraGlyphCount] = {{
{format_array(widths)}
}};
constexpr uint8_t kGlyphRowBytes{px}[kAsciiSmoothExtraGlyphCount] = {{
{format_array(row_bytes)}
}};
constexpr uint16_t kGlyphOffsets{px}[kAsciiSmoothExtraGlyphCount] = {{
{format_array(offsets)}
}};
constexpr uint8_t kGlyphData{px}[{len(data)}] = {{
{format_hex(data)}
}};
"""


def main() -> None:
  body = f"""#include "fonts/AsciiSmoothFont.h"

namespace fonts {{
namespace {{

constexpr size_t kAsciiSmoothExtraGlyphCount = {len(GLYPH_CHARS)}u;
constexpr char kGlyphChars[kAsciiSmoothExtraGlyphCount + 1] = "{GLYPH_CHARS}";
{emit_size(16)}
{emit_size(14)}
int glyphIndexForCharExtra(char c) {{
  if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  for (size_t i = 0; i < kAsciiSmoothExtraGlyphCount; ++i) {{
    if (kGlyphChars[i] == c) return static_cast<int>(i);
  }}
  return -1;
}}

bool lookupAsciiSmoothGlyphSized(const uint8_t *data, const uint8_t *widths,
                                 const uint8_t *row_bytes, const uint16_t *offsets,
                                 uint8_t px, char c, const uint8_t *&glyph_data,
                                 uint8_t &width, uint8_t &height,
                                 uint8_t &glyph_row_bytes, uint8_t &bits_per_pixel) {{
  glyph_data = nullptr;
  width = 0;
  height = 0;
  glyph_row_bytes = 0;
  bits_per_pixel = 0;
  int index = glyphIndexForCharExtra(c);
  if (index < 0) index = glyphIndexForCharExtra('?');
  if (index < 0) return false;
  width = widths[index];
  height = px;
  glyph_row_bytes = row_bytes[index];
  bits_per_pixel = kAsciiSmoothFontBitsPerPixel;
  glyph_data = &data[offsets[index]];
  return true;
}}

}}  // namespace

bool lookupAsciiSmooth16Glyph(char c, const uint8_t *&data, uint8_t &width,
                              uint8_t &height, uint8_t &row_bytes,
                              uint8_t &bits_per_pixel) {{
  return lookupAsciiSmoothGlyphSized(kGlyphData16, kGlyphWidths16, kGlyphRowBytes16,
                                     kGlyphOffsets16, kAsciiSmooth16FontPx, c, data,
                                     width, height, row_bytes, bits_per_pixel);
}}

bool lookupAsciiSmooth14Glyph(char c, const uint8_t *&data, uint8_t &width,
                              uint8_t &height, uint8_t &row_bytes,
                              uint8_t &bits_per_pixel) {{
  return lookupAsciiSmoothGlyphSized(kGlyphData14, kGlyphWidths14, kGlyphRowBytes14,
                                     kGlyphOffsets14, kAsciiSmooth14FontPx, c, data,
                                     width, height, row_bytes, bits_per_pixel);
}}

}}  // namespace fonts
"""
  DEFAULT_OUT.write_text(body, encoding="utf-8")


if __name__ == "__main__":
  main()
