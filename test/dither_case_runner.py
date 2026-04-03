from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

W, H = 800, 480

PALETTE_EMPIRICAL = [
    ((0, 0, 0), 0x00, "black"),
    ((255, 255, 255), 0x01, "white"),
    ((251, 246, 0), 0x02, "yellow_emp"),
    ((251, 4, 0), 0x03, "red_emp"),
    ((0, 53, 214), 0x05, "blue_emp"),
    ((12, 108, 13), 0x06, "green_emp"),
]

BAYER_4X4 = [
    [0, 8, 2, 10],
    [12, 4, 14, 6],
    [3, 11, 1, 9],
    [15, 7, 13, 5],
]


def bayer4x4(x: int, y: int) -> int:
    return BAYER_4X4[y & 3][x & 3]


def dist2_weighted(a: tuple[int, int, int], b: tuple[int, int, int]) -> int:
    dr = a[0] - b[0]
    dg = a[1] - b[1]
    db = a[2] - b[2]
    return 3 * dr * dr + 6 * dg * dg + db * db


def preprocess_like_firmware(img: Image.Image, crop_mode: bool) -> Image.Image:
    img = img.convert("RGBA")
    src_landscape = img.width >= img.height
    frame_landscape = W >= H
    rotate90 = src_landscape != frame_landscape

    src_w = img.height if rotate90 else img.width
    src_h = img.width if rotate90 else img.height
    sx = W / src_w
    sy = H / src_h
    scale = max(sx, sy) if crop_mode else min(sx, sy)
    dw = src_w * scale
    dh = src_h * scale

    canvas = Image.new("RGBA", (W, H), (255, 255, 255, 255))
    if not rotate90:
        rw = max(1, int(round(dw)))
        rh = max(1, int(round(dh)))
        resized = img.resize((rw, rh), Image.Resampling.LANCZOS)
        px = int(round((W - dw) * 0.5))
        py = int(round((H - dh) * 0.5))
        canvas.alpha_composite(resized, (px, py))
        return canvas

    # Mirrors canvas branch:
    # translate(W/2,H/2) -> rotate(-90deg) -> drawImage(img, -dh/2, -dw/2, dh, dw)
    rw = max(1, int(round(dh)))
    rh = max(1, int(round(dw)))
    resized = img.resize((rw, rh), Image.Resampling.LANCZOS)
    rotated = resized.rotate(-90, expand=True)
    px = int(round((W - rotated.width) * 0.5))
    py = int(round((H - rotated.height) * 0.5))
    canvas.alpha_composite(rotated, (px, py))
    return canvas


@dataclass
class QuantResult:
    preview: Image.Image
    epd4: bytes


def quantize_with_empirical_dither(canvas: Image.Image) -> QuantResult:
    width, height = canvas.size
    pix = canvas.load()
    preview = Image.new("RGB", (width, height), (255, 255, 255))
    ppix = preview.load()
    if width % 2 != 0:
        raise ValueError(f"width must be even for epd4 packing, got {width}")
    out = bytearray((width * height) // 2)
    oi = 0

    for y in range(height):
        for x in range(0, width, 2):
            nibs: list[int] = []
            for px_x in (x, x + 1):
                r, g, b, a = pix[px_x, y]
                rgb = (255, 255, 255) if a < 8 else (r, g, b)

                first = 0
                second = 0
                best = 10**30
                nxt = 10**30
                for pi, (prgb, _, _) in enumerate(PALETTE_EMPIRICAL):
                    d = dist2_weighted(rgb, prgb)
                    if d < best:
                        nxt = best
                        second = first
                        best = d
                        first = pi
                    elif d < nxt:
                        nxt = d
                        second = pi

                sum_d = best + nxt
                if sum_d <= 0:
                    choose = first
                else:
                    keep_best_x16 = (nxt * 16) // sum_d
                    choose = first if bayer4x4(px_x, y) < keep_best_x16 else second

                nib = PALETTE_EMPIRICAL[choose][1]
                nibs.append(nib)
                ppix[px_x, y] = PALETTE_EMPIRICAL[choose][0]

            out[oi] = ((nibs[0] & 0x0F) << 4) | (nibs[1] & 0x0F)
            oi += 1

    return QuantResult(preview=preview, epd4=bytes(out))


def diff_stats(a: Image.Image, b: Image.Image) -> tuple[int, float]:
    if a.size != b.size:
        raise ValueError(f"size mismatch: {a.size} vs {b.size}")
    a = a.convert("RGB")
    b = b.convert("RGB")
    ap = a.load()
    bp = b.load()
    total = a.width * a.height
    mismatch = 0
    for y in range(a.height):
        for x in range(a.width):
            if ap[x, y] != bp[x, y]:
                mismatch += 1
    return mismatch, mismatch * 100.0 / total


def make_diff_image(a: Image.Image, b: Image.Image) -> Image.Image:
    a = a.convert("RGB")
    b = b.convert("RGB")
    out = Image.new("RGB", a.size, (0, 0, 0))
    ap = a.load()
    bp = b.load()
    op = out.load()
    for y in range(a.height):
        for x in range(a.width):
            if ap[x, y] == bp[x, y]:
                op[x, y] = (32, 32, 32)
            else:
                op[x, y] = (255, 0, 255)
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description="Run firmware-equivalent dither test case.")
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--expected", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--mode", choices=["fit", "crop"], default="fit")
    parser.add_argument(
        "--pipeline",
        choices=["firmware", "source_size"],
        default="firmware",
        help="firmware: preprocess to 800x480 first; source_size: only RGBA decode then quantize",
    )
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    src = Image.open(args.input).convert("RGBA")
    src.save(args.out / "00_input_decoded.png")

    if args.pipeline == "firmware":
        crop_mode = args.mode == "crop"
        canvas = preprocess_like_firmware(src, crop_mode=crop_mode)
    else:
        canvas = src
    canvas.save(args.out / "01_canvas_preprocessed.png")

    q = quantize_with_empirical_dither(canvas)
    q.preview.save(args.out / "02_quantized_current.png")
    (args.out / "03_current_output.epd4").write_bytes(q.epd4)

    expected = Image.open(args.expected).convert("RGB")
    expected.save(args.out / "10_expected.png")

    report = [
        f"mode={args.mode}",
        f"pipeline={args.pipeline}",
        f"input={args.input}",
        f"expected={args.expected}",
        f"size={q.preview.size}",
    ]
    if q.preview.size == expected.size:
        mismatch, mismatch_pct = diff_stats(q.preview, expected)
        diff = make_diff_image(q.preview, expected)
        diff.save(args.out / "11_diff.png")
        report.append(f"pixel_mismatch={mismatch}")
        report.append(f"pixel_mismatch_pct={mismatch_pct:.4f}")
    else:
        report.append(f"size_mismatch={q.preview.size} vs {expected.size}")
        report.append("pixel_compare=skipped")
    (args.out / "report.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    print("\n".join(report))


if __name__ == "__main__":
    main()
