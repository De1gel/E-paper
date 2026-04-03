from __future__ import annotations

from pathlib import Path
from typing import Iterable

from PIL import Image, ImageOps, ImageDraw

W, H = 800, 480

PALETTE = [
    (0, 0, 0),       # black
    (255, 255, 255), # white
    (255, 255, 0),   # yellow
    (255, 0, 0),     # red
    (0, 0, 255),     # blue
    (0, 255, 0),     # green
]

ALGOS = [
    "fs_serpentine",
    "atkinson",
    "jjn",
    "yliluoma",
    "dbs",
]

# low / mid / high
GAMMAS = [0.85, 1.00, 1.20]

# 8x8 Bayer (0..63)
BAYER8 = [
    [0, 48, 12, 60, 3, 51, 15, 63],
    [32, 16, 44, 28, 35, 19, 47, 31],
    [8, 56, 4, 52, 11, 59, 7, 55],
    [40, 24, 36, 20, 43, 27, 39, 23],
    [2, 50, 14, 62, 1, 49, 13, 61],
    [34, 18, 46, 30, 33, 17, 45, 29],
    [10, 58, 6, 54, 9, 57, 5, 53],
    [42, 26, 38, 22, 41, 25, 37, 21],
]

OUT_ROOT = Path(r"d:\MyCode\ESP\E-paper\debug_compare_5algos_3gamma")

INPUTS = {
    "photo": Path(r"d:\MyCode\ESP\E-paper\photo.jpg"),
    "image": Path(r"d:\MyCode\ESP\设计思路资料\正确抖动结果\image.jpg"),
}


def dist2w(a: tuple[float, float, float], b: tuple[int, int, int]) -> float:
    dr = a[0] - b[0]
    dg = a[1] - b[1]
    db = a[2] - b[2]
    return 3 * dr * dr + 6 * dg * dg + db * db


def nearest_idx(rgb: tuple[float, float, float]) -> int:
    best = 1e30
    out = 0
    for i, c in enumerate(PALETTE):
        d = dist2w(rgb, c)
        if d < best:
            best = d
            out = i
    return out


def nearest_two(rgb: tuple[float, float, float]) -> tuple[int, int, float, float]:
    first = 0
    second = 0
    best = 1e30
    nxt = 1e30
    for i, c in enumerate(PALETTE):
        d = dist2w(rgb, c)
        if d < best:
            nxt = best
            second = first
            best = d
            first = i
        elif d < nxt:
            nxt = d
            second = i
    return first, second, best, nxt


def preprocess_like_firmware_fit(img: Image.Image, allow_rotate: bool = True) -> Image.Image:
    img = img.convert("RGBA")
    src_landscape = img.width >= img.height
    frame_landscape = W >= H
    rotate90 = (src_landscape != frame_landscape) if allow_rotate else False
    src_w = img.height if rotate90 else img.width
    src_h = img.width if rotate90 else img.height
    sx = W / src_w
    sy = H / src_h
    scale = min(sx, sy)
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

    rw = max(1, int(round(dh)))
    rh = max(1, int(round(dw)))
    resized = img.resize((rw, rh), Image.Resampling.LANCZOS)
    rotated = resized.rotate(-90, expand=True)
    px = int(round((W - rotated.width) * 0.5))
    py = int(round((H - rotated.height) * 0.5))
    canvas.alpha_composite(rotated, (px, py))
    return canvas


def apply_gamma_rgba(canvas: Image.Image, gamma: float) -> list[float]:
    rgba = canvas.tobytes()
    work = [0.0] * (W * H * 3)
    inv_gamma = 1.0 / gamma
    for i in range(W * H):
        p4 = i * 4
        a = rgba[p4 + 3]
        if a < 8:
            r = g = b = 255.0
        else:
            r = float(rgba[p4 + 0])
            g = float(rgba[p4 + 1])
            b = float(rgba[p4 + 2])
        # simple display gamma correction
        r = 255.0 * ((r / 255.0) ** inv_gamma)
        g = 255.0 * ((g / 255.0) ** inv_gamma)
        b = 255.0 * ((b / 255.0) ** inv_gamma)
        p3 = i * 3
        work[p3 + 0] = r
        work[p3 + 1] = g
        work[p3 + 2] = b
    return work


def quantize_error_diffusion(base: list[float], algo: str) -> Image.Image:
    work = base[:]  # copy
    out = Image.new("RGB", (W, H), (255, 255, 255))
    op = out.load()

    def clamp(v: float) -> float:
        if v < 0:
            return 0.0
        if v > 255:
            return 255.0
        return v

    def add(x: int, y: int, er: float, eg: float, eb: float, w: float) -> None:
        if x < 0 or x >= W or y < 0 or y >= H:
            return
        idx = (y * W + x) * 3
        work[idx + 0] += er * w
        work[idx + 1] += eg * w
        work[idx + 2] += eb * w

    for y in range(H):
        serp = algo == "fs_serpentine"
        rev = serp and (y % 2 == 1)
        x_iter: Iterable[int] = range(W - 1, -1, -1) if rev else range(W)
        for x in x_iter:
            i = (y * W + x) * 3
            rgb = (clamp(work[i + 0]), clamp(work[i + 1]), clamp(work[i + 2]))
            pi = nearest_idx(rgb)
            pr, pg, pb = PALETTE[pi]
            op[x, y] = (pr, pg, pb)
            er = rgb[0] - pr
            eg = rgb[1] - pg
            eb = rgb[2] - pb

            if algo == "fs_serpentine":
                if not rev:
                    add(x + 1, y, er, eg, eb, 7 / 16)
                    add(x - 1, y + 1, er, eg, eb, 3 / 16)
                    add(x, y + 1, er, eg, eb, 5 / 16)
                    add(x + 1, y + 1, er, eg, eb, 1 / 16)
                else:
                    add(x - 1, y, er, eg, eb, 7 / 16)
                    add(x + 1, y + 1, er, eg, eb, 3 / 16)
                    add(x, y + 1, er, eg, eb, 5 / 16)
                    add(x - 1, y + 1, er, eg, eb, 1 / 16)
            elif algo == "atkinson":
                r = 1 / 8
                add(x + 1, y, er, eg, eb, r)
                add(x + 2, y, er, eg, eb, r)
                add(x - 1, y + 1, er, eg, eb, r)
                add(x, y + 1, er, eg, eb, r)
                add(x + 1, y + 1, er, eg, eb, r)
                add(x, y + 2, er, eg, eb, r)
            elif algo == "jjn":
                if not rev:
                    add(x + 1, y, er, eg, eb, 7 / 48)
                    add(x + 2, y, er, eg, eb, 5 / 48)
                    add(x - 2, y + 1, er, eg, eb, 3 / 48)
                    add(x - 1, y + 1, er, eg, eb, 5 / 48)
                    add(x, y + 1, er, eg, eb, 7 / 48)
                    add(x + 1, y + 1, er, eg, eb, 5 / 48)
                    add(x + 2, y + 1, er, eg, eb, 3 / 48)
                    add(x - 2, y + 2, er, eg, eb, 1 / 48)
                    add(x - 1, y + 2, er, eg, eb, 3 / 48)
                    add(x, y + 2, er, eg, eb, 5 / 48)
                    add(x + 1, y + 2, er, eg, eb, 3 / 48)
                    add(x + 2, y + 2, er, eg, eb, 1 / 48)
                else:
                    add(x - 1, y, er, eg, eb, 7 / 48)
                    add(x - 2, y, er, eg, eb, 5 / 48)
                    add(x + 2, y + 1, er, eg, eb, 3 / 48)
                    add(x + 1, y + 1, er, eg, eb, 5 / 48)
                    add(x, y + 1, er, eg, eb, 7 / 48)
                    add(x - 1, y + 1, er, eg, eb, 5 / 48)
                    add(x - 2, y + 1, er, eg, eb, 3 / 48)
                    add(x + 2, y + 2, er, eg, eb, 1 / 48)
                    add(x + 1, y + 2, er, eg, eb, 3 / 48)
                    add(x, y + 2, er, eg, eb, 5 / 48)
                    add(x - 1, y + 2, er, eg, eb, 3 / 48)
                    add(x - 2, y + 2, er, eg, eb, 1 / 48)
    return out


def quantize_yliluoma(base: list[float]) -> Image.Image:
    out = Image.new("RGB", (W, H), (255, 255, 255))
    op = out.load()
    for y in range(H):
        for x in range(W):
            i = (y * W + x) * 3
            rgb = (base[i + 0], base[i + 1], base[i + 2])
            i1, i2, d1, d2 = nearest_two(rgb)
            s = d1 + d2
            if s <= 1e-9:
                idx = i1
            else:
                t = (BAYER8[y & 7][x & 7] + 0.5) / 64.0
                w1 = d2 / s
                idx = i1 if t < w1 else i2
            op[x, y] = PALETTE[idx]
    return out


def quantize_dbs(base: list[float], iters: int = 2) -> Image.Image:
    # DBS-like local search (fast approximation for experiment use).
    idx_map = [0] * (W * H)
    for p in range(W * H):
        i = p * 3
        idx_map[p] = nearest_idx((base[i + 0], base[i + 1], base[i + 2]))

    neigh = [(-1, 0), (1, 0), (0, -1), (0, 1)]
    for _ in range(iters):
        for y in range(H):
            for x in range(W):
                p = y * W + x
                i = p * 3
                src = (base[i + 0], base[i + 1], base[i + 2])
                cur = idx_map[p]
                cur_cost = dist2w(src, PALETTE[cur])
                # local smoothness term (encourage dot structure stability)
                for dx, dy in neigh:
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < W and 0 <= ny < H:
                        nidx = idx_map[ny * W + nx]
                        cur_cost += 0.05 * dist2w(PALETTE[cur], PALETTE[nidx])

                best = cur
                best_cost = cur_cost
                for cand in range(len(PALETTE)):
                    if cand == cur:
                        continue
                    c = dist2w(src, PALETTE[cand])
                    for dx, dy in neigh:
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < W and 0 <= ny < H:
                            nidx = idx_map[ny * W + nx]
                            c += 0.05 * dist2w(PALETTE[cand], PALETTE[nidx])
                    if c < best_cost:
                        best_cost = c
                        best = cand
                idx_map[p] = best

    out = Image.new("RGB", (W, H), (255, 255, 255))
    op = out.load()
    for y in range(H):
        for x in range(W):
            op[x, y] = PALETTE[idx_map[y * W + x]]
    return out


def run_algo(base: list[float], algo: str) -> Image.Image:
    if algo in {"fs_serpentine", "atkinson", "jjn"}:
        return quantize_error_diffusion(base, algo)
    if algo == "yliluoma":
        return quantize_yliluoma(base)
    if algo == "dbs":
        return quantize_dbs(base, iters=2)
    raise ValueError(algo)


def make_sheet(images: list[tuple[str, Image.Image]], title: str, out_path: Path) -> None:
    cols = 3
    rows = 2
    tile_w, tile_h = 420, 252
    sheet = Image.new("RGB", (tile_w * cols, tile_h * rows + 60), (20, 24, 36))
    draw = ImageDraw.Draw(sheet)
    draw.text((16, 12), title, fill=(220, 230, 255))
    for i, (name, img) in enumerate(images):
        thumb = ImageOps.contain(img, (tile_w - 24, tile_h - 40), Image.Resampling.NEAREST)
        x0 = (i % cols) * tile_w + 12
        y0 = (i // cols) * tile_h + 44
        sheet.paste(thumb, (x0, y0))
        draw.text((x0, y0 + tile_h - 28), name, fill=(220, 230, 255))
    sheet.save(out_path)


def main() -> None:
    for case_name, src_path in INPUTS.items():
        case_dir = OUT_ROOT / case_name
        case_dir.mkdir(parents=True, exist_ok=True)

        src = Image.open(src_path).convert("RGBA")
        src.save(case_dir / "00_input_used.png")
        allow_rotate = case_name != "image"
        pre = preprocess_like_firmware_fit(src, allow_rotate=allow_rotate)
        pre.save(case_dir / "01_preprocessed_singlemode.png")

        for gamma in GAMMAS:
            gtag = f"gamma_{gamma:.2f}"
            gdir = case_dir / gtag
            gdir.mkdir(parents=True, exist_ok=True)
            base = apply_gamma_rgba(pre, gamma)

            rendered: list[tuple[str, Image.Image]] = []
            for algo in ALGOS:
                out = run_algo(base, algo)
                out.save(gdir / f"02_{algo}.png")
                rendered.append((algo, out))

            make_sheet(
                rendered,
                title=f"{case_name} | {gtag} | FS-serp / Atkinson / JJN / Yliluoma / DBS",
                out_path=gdir / "03_compare_grid_5algos.png",
            )

    (OUT_ROOT / "README.txt").write_text(
        "5 algorithms x 3 gamma levels\n"
        "Algorithms: fs_serpentine, atkinson, jjn, yliluoma, dbs\n"
        "Gammas: 0.85, 1.00, 1.20\n"
        "Input cases: photo + image\n"
        "Mode: single fit-like firmware preprocess\n"
        "Rotation: photo follows firmware auto-rotate rule; image keeps original orientation (no rotate)\n",
        encoding="utf-8",
    )
    print(str(OUT_ROOT))


if __name__ == "__main__":
    main()
