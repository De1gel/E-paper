from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw


WIDTH = 800
HEIGHT = 480
COLS = 4
ROWS = 4
CELL_W = WIDTH // COLS
CELL_H = HEIGHT // ROWS

GREEN = 0x06
BLUE = 0x05
WHITE = 0x01
BLACK = 0x00

BAYER_4X4 = (
    (0, 8, 2, 10),
    (12, 4, 14, 6),
    (3, 11, 1, 9),
    (15, 7, 13, 5),
)


def tile_nibble(blue_count: int, x: int, y: int) -> int:
    """Mix (16-blue_count) green dots with blue_count blue dots."""
    return BLUE if BAYER_4X4[y & 3][x & 3] < blue_count else GREEN


def generate(output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    nibbles = bytearray(WIDTH * HEIGHT)
    preview = Image.new("RGB", (WIDTH, HEIGHT), "white")
    pixels = preview.load()

    for y in range(HEIGHT):
        row = y // CELL_H
        for x in range(WIDTH):
            col = x // CELL_W
            blue_count = row * COLS + col
            nibble = tile_nibble(blue_count, x, y)
            nibbles[y * WIDTH + x] = nibble
            pixels[x, y] = (0, 0, 255) if nibble == BLUE else (0, 255, 0)

    # Labels are deliberately small so almost all of each cell remains available
    # for judging the physical ink mixture. B00 means 0/16 blue + 16/16 green.
    draw = ImageDraw.Draw(preview)
    for blue_count in range(COLS * ROWS):
        row, col = divmod(blue_count, COLS)
        x0 = col * CELL_W
        y0 = row * CELL_H
        draw.rectangle((x0, y0, x0 + 47, y0 + 17), fill=(255, 255, 255))
        draw.text((x0 + 4, y0 + 2), f"B{blue_count:02d}", fill=(0, 0, 0))

        # Mirror the preview label into the native frame using a fixed 5x7 font
        # rendered by Pillow, then map its black/white pixels back to nibbles.
        label = preview.crop((x0, y0, x0 + 48, y0 + 18)).convert("RGB")
        label_pixels = label.load()
        for ly in range(label.height):
            for lx in range(label.width):
                r, g, b = label_pixels[lx, ly]
                nibbles[(y0 + ly) * WIDTH + x0 + lx] = BLACK if (r + g + b) < 384 else WHITE

    packed = bytearray(WIDTH * HEIGHT // 2)
    out_index = 0
    for i in range(0, len(nibbles), 2):
        packed[out_index] = ((nibbles[i] & 0x0F) << 4) | (nibbles[i + 1] & 0x0F)
        out_index += 1

    epd_path = output_dir / "green_blue_calibration.epd4"
    preview_path = output_dir / "green_blue_calibration_preview.png"
    guide_path = output_dir / "README.txt"
    epd_path.write_bytes(packed)
    preview.save(preview_path)
    guide_path.write_text(
        "Green ink compensation card, 800x480, native six-color EPD format.\n"
        "Copy green_blue_calibration.epd4 to SD:/pic and display it.\n"
        "Cells are numbered left-to-right, top-to-bottom.\n"
        "B00 = 16 green + 0 blue dots per Bayer 4x4 block (native solid green).\n"
        "B01 = 15 green + 1 blue; ...; B15 = 1 green + 15 blue.\n"
        "Report the Bxx cell that looks closest to a neutral, normal green.\n",
        encoding="utf-8",
    )

    if len(packed) != 192000:
        raise RuntimeError(f"invalid EPD4 size: {len(packed)}")


if __name__ == "__main__":
    generate(Path(__file__).resolve().parent / "generated" / "green_calibration")
