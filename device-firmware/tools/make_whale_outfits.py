"""Build placeholder outfit sets by recolouring the maid dress (RGB565A8).

Each set redraws the ten poses used on the home and companion pages
(OUTFIT_POSES, same indices as the UI sprite table). The dress mask is a
hue/value band that skips the brighter hair, the outline and the frills.
Replace a set with real art by swapping recolour() for source PNGs.
"""

import colorsys
import struct
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUTFIT_POSES = (1, 3, 10, 15, 16, 17, 18, 19, 20, 21)
COMPANION = ("idle", "blink", "hello", "happy", "feed", "sleepy")  # sprites 16..21
# (hue, saturation, value scale) per set: wine red, deep green.
OUTFITS = ((0.975, 0.72, 1.05), (0.42, 0.62, 0.95))


def source(index):
    if index >= 16:
        path = ROOT / "assets/images/companion-v130" / f"{COMPANION[index - 16]}.png"
        return Image.open(path).convert("RGBA")
    atlas = Image.open(ROOT / "assets/images/whale-atlas.png").convert("RGBA")
    x, y = index % 4, index // 4
    box = (round(x * atlas.width / 4), round(y * atlas.height / 4),
           round((x + 1) * atlas.width / 4), round((y + 1) * atlas.height / 4))
    return atlas.crop(box).resize((196, 196), Image.Resampling.LANCZOS)


def dress(r, g, b, a):
    if a < 20:
        return None
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    return (h, s, v) if 0.55 < h < 0.75 and 0.12 < v < 0.5 and s < 0.65 else None


def recolour(image, outfit):
    hue, sat, scale = outfit
    pixels = []
    raw = image.tobytes()
    for r, g, b, a in zip(raw[0::4], raw[1::4], raw[2::4], raw[3::4]):
        hsv = dress(r, g, b, a)
        if hsv:
            _, s, v = hsv
            r, g, b = (round(c * 255) for c in colorsys.hsv_to_rgb(hue, max(s, sat), min(1, v * scale)))
        pixels.append((r, g, b, a))
    out = Image.new("RGBA", image.size)
    out.putdata(pixels)
    return out


def rgb565a8(image):
    rgba = image.tobytes()
    data = b"".join(struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
                    for r, g, b in zip(rgba[0::4], rgba[1::4], rgba[2::4]))
    return data + rgba[3::4]


def build(preview=None):
    blob = bytearray()
    sheet = Image.new("RGBA", (196 * len(OUTFIT_POSES), 196 * (len(OUTFITS) + 1)), (255, 255, 255, 255))
    for i, index in enumerate(OUTFIT_POSES):
        sheet.alpha_composite(source(index), (196 * i, 0))
    for o, outfit in enumerate(OUTFITS):
        for i, index in enumerate(OUTFIT_POSES):
            image = recolour(source(index), outfit)
            blob.extend(rgb565a8(image))
            sheet.alpha_composite(image, (196 * i, 196 * (o + 1)))
    assert len(blob) == len(OUTFITS) * len(OUTFIT_POSES) * 196 * 196 * 3
    (ROOT / "main/outfits.rgb").write_bytes(blob)
    if preview:
        sheet.save(preview)


if __name__ == "__main__":
    import sys
    build(sys.argv[1] if len(sys.argv) > 1 else None)
