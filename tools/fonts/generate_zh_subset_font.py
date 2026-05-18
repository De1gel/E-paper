#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw, ImageFont
from pcffont import PcfFont


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_COMMON = ROOT / "assets" / "charsets" / "common-zh-3000.txt"
DEFAULT_WEEKDAYS = ROOT / "assets" / "charsets" / "weekdays-zh-7.txt"
DEFAULT_CITY = ROOT / "assets" / "charsets" / "city-zh-common.txt"
DEFAULT_OUT = ROOT / "src" / "fonts" / "ZhSubsetFontData.cpp"
DEFAULT_MSYH = ROOT / "MSYH.TTC"
DEFAULT_WQY_DIR = ROOT / "assets" / "fonts" / "wqy-bitmapsong"
DEFAULT_WQY_12 = DEFAULT_WQY_DIR / "wenquanyi_9pt.pcf"
DEFAULT_WQY_16 = DEFAULT_WQY_DIR / "wenquanyi_12pt.pcf"


@dataclass(frozen=True)
class FontProfile:
    key: str
    charset_path: Path
    size_px: int
    font_path: Path
    codepoints_name: str
    data_name: str
    count_name: str | None
    box_prefix: str


PROFILES = (
    FontProfile(
        key="common10",
        charset_path=DEFAULT_COMMON,
        size_px=12,
        font_path=DEFAULT_WQY_12,
        codepoints_name="kZhCommon3000Codepoints",
        data_name="kZhCommon3000Px10",
        count_name=None,
        box_prefix="kZhFont10Box",
    ),
    FontProfile(
        key="common16",
        charset_path=DEFAULT_COMMON,
        size_px=16,
        font_path=DEFAULT_WQY_16,
        codepoints_name="kZhCommon3000Codepoints",
        data_name="kZhCommon3000Px16",
        count_name=None,
        box_prefix="kZhFont16Box",
    ),
    FontProfile(
        key="weekday26",
        charset_path=DEFAULT_WEEKDAYS,
        size_px=26,
        font_path=DEFAULT_MSYH,
        codepoints_name="kZhWeekday7Codepoints",
        data_name="kZhWeekday7Px26",
        count_name=None,
        box_prefix="kZhFont26Box",
    ),
    FontProfile(
        key="city30",
        charset_path=DEFAULT_CITY,
        size_px=30,
        font_path=DEFAULT_MSYH,
        codepoints_name="kZhCityCodepoints",
        data_name="kZhCityPx30",
        count_name="kZhCityGlyphCount",
        box_prefix="kZhFont30Box",
    ),
)


def load_charset(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    chars: list[str] = []
    seen: set[str] = set()
    for ch in text:
        if "\u4e00" <= ch <= "\u9fff" and ch not in seen:
            chars.append(ch)
            seen.add(ch)
    if not chars:
        raise SystemExit(f"no Han chars found in {path}")
    return chars


def pack_4bpp(canvas: Image.Image, px: int) -> bytes:
    packed = bytearray()
    for yy in range(px):
        row = [canvas.getpixel((xx, yy)) for xx in range(px)]
        for start in range(0, px, 2):
            byte = 0
            for offset, value in enumerate(row[start:start + 2]):
                level = min(15, (value * 16) // 256)
                shift = (1 - offset) * 4
                byte |= (level & 0x0F) << shift
            packed.append(byte)
    return bytes(packed)


PCF_CACHE: dict[Path, PcfFont] = {}


def render_pcf_glyph(font_path: Path, glyph: str, px: int) -> tuple[bytes, tuple[int, int, int, int]]:
    font = PCF_CACHE.get(font_path)
    if font is None:
        font = PcfFont.load(font_path)
        PCF_CACHE[font_path] = font

    glyph_index = font.bdf_encodings.data.get(ord(glyph))
    if glyph_index is None:
        raise SystemExit(f"glyph U+{ord(glyph):04X} not found in {font_path}")

    bitmap = font.bitmaps[glyph_index]
    canvas = Image.new("L", (px, px), 0)
    if bitmap:
        height = len(bitmap)
        width = max(len(row) for row in bitmap)
        x0 = max(0, (px - width) // 2)
        y0 = max(0, (px - height) // 2)
        for y, row in enumerate(bitmap[:px]):
            for x, value in enumerate(row[:px]):
                if value:
                    xx = x0 + x
                    yy = y0 + y
                    if 0 <= xx < px and 0 <= yy < px:
                        canvas.putpixel((xx, yy), 255)
    glyph_bbox = canvas.getbbox() or (0, 0, 0, 0)
    return pack_4bpp(canvas, px), glyph_bbox


def render_truetype_glyph(font_path: Path, glyph: str, px: int) -> tuple[bytes, tuple[int, int, int, int]]:
    point_size = max(8, int(round(px * 0.90)))
    font = ImageFont.truetype(str(font_path), point_size, index=0)
    canvas = Image.new("L", (px, px), 0)
    draw = ImageDraw.Draw(canvas)
    bbox = draw.textbbox((0, 0), glyph, font=font)
    width = bbox[2] - bbox[0]
    height = bbox[3] - bbox[1]
    x = (px - width) // 2 - bbox[0]
    y = (px - height) // 2 - bbox[1]
    draw.text((x, y), glyph, fill=255, font=font)
    glyph_bbox = canvas.getbbox() or (0, 0, 0, 0)
    return pack_4bpp(canvas, px), glyph_bbox


def render_glyph(font_path: Path, glyph: str, px: int) -> tuple[bytes, tuple[int, int, int, int]]:
    if font_path.suffix.lower() == ".pcf":
        return render_pcf_glyph(font_path, glyph, px)
    return render_truetype_glyph(font_path, glyph, px)


def union_box(boxes: list[tuple[int, int, int, int]]) -> tuple[int, int, int, int]:
    valid = [box for box in boxes if box[2] > box[0] and box[3] > box[1]]
    if not valid:
        return (0, 0, 0, 0)
    left = min(box[0] for box in valid)
    top = min(box[1] for box in valid)
    right = max(box[2] for box in valid)
    bottom = max(box[3] for box in valid)
    return left, top, right - left, bottom - top


def format_bytes(values: bytes, width: int = 16) -> Iterable[str]:
    for start in range(0, len(values), width):
        chunk = values[start:start + width]
        yield ", ".join(f"0x{value:02X}" for value in chunk)


def format_codepoints(codepoints: list[int]) -> list[str]:
    lines: list[str] = []
    for start in range(0, len(codepoints), 16):
        chunk = codepoints[start:start + 16]
        lines.append("  " + ", ".join(f"0x{cp:04X}" for cp in chunk) + ",")
    return lines


def write_cpp(out_path: Path, rendered: dict[str, dict[str, object]]) -> None:
    lines: list[str] = []
    lines.append("// Auto-generated by tools/fonts/generate_zh_subset_font.py")
    source_fonts = sorted({str(profile.font_path.relative_to(ROOT)) for profile in PROFILES})
    lines.append(f"// Source fonts: {', '.join(source_fonts)}")
    lines.append('#include "fonts/ZhSubsetFont.h"')
    lines.append("")
    lines.append("namespace fonts {")
    lines.append("")

    common_written = False
    for profile in PROFILES:
        data = rendered[profile.key]
        codepoints = data["codepoints"]
        box = data["box"]
        glyph_bytes = data["glyph_bytes"]
        if profile.codepoints_name == "kZhCommon3000Codepoints":
            if not common_written:
                lines.append(
                    f"const uint16_t {profile.codepoints_name}[{len(codepoints)}] = {{"
                )
                lines.extend(format_codepoints(codepoints))
                lines.append("};")
                lines.append("")
                common_written = True
        else:
            if profile.count_name is not None:
                lines.append(f"const size_t {profile.count_name} = {len(codepoints)}u;")
            lines.append(f"const uint16_t {profile.codepoints_name}[{len(codepoints)}] = {{")
            lines.extend(format_codepoints(codepoints))
            lines.append("};")
            lines.append("")

        lines.append(f"const uint8_t {profile.box_prefix}Left = {box[0]};")
        lines.append(f"const uint8_t {profile.box_prefix}Top = {box[1]};")
        lines.append(f"const uint8_t {profile.box_prefix}Width = {box[2]};")
        lines.append(f"const uint8_t {profile.box_prefix}Height = {box[3]};")
        lines.append("")

        lines.append(f"const uint8_t {profile.data_name}[{len(glyph_bytes)}] = {{")
        for line in format_bytes(glyph_bytes):
            lines.append("  " + line + ",")
        lines.append("};")
        lines.append("")

    lines.append("}  // namespace fonts")
    lines.append("")
    out_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    missing_fonts = sorted({profile.font_path for profile in PROFILES if not profile.font_path.exists()})
    if missing_fonts:
        raise SystemExit("font file not found: " + ", ".join(str(path) for path in missing_fonts))

    rendered: dict[str, dict[str, object]] = {}
    for profile in PROFILES:
        chars = load_charset(profile.charset_path)
        glyphs = []
        boxes = []
        for ch in chars:
            glyph_data, glyph_box = render_glyph(profile.font_path, ch, profile.size_px)
            glyphs.append((ord(ch), glyph_data))
            boxes.append(glyph_box)
        glyphs.sort(key=lambda item: item[0])
        rendered[profile.key] = {
            "codepoints": [item[0] for item in glyphs],
            "glyph_bytes": b"".join(item[1] for item in glyphs),
            "box": union_box(boxes),
        }

    write_cpp(args.out, rendered)
    print(f"generated {args.out}")


if __name__ == "__main__":
    main()
