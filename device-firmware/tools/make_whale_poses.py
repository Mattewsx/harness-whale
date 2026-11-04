"""Convert the approved 4x4 sprite sheet to LVGL RGB565A8 without a frame buffer."""

import struct
from pathlib import Path

from PIL import Image


def build():
    root = Path(__file__).resolve().parents[1]
    atlas = Image.open(root / "assets/images/whale-atlas.png").convert("RGBA")
    output = bytearray()
    for index in range(16):
        x, y = index % 4, index // 4
        box = (
            round(x * atlas.width / 4),
            round(y * atlas.height / 4),
            round((x + 1) * atlas.width / 4),
            round((y + 1) * atlas.height / 4),
        )
        sprite = atlas.crop(box).resize((196, 196), Image.Resampling.LANCZOS)
        rgba = sprite.tobytes()
        output.extend(
            b"".join(
                struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
                for r, g, b in zip(rgba[0::4], rgba[1::4], rgba[2::4])
            )
        )
        output.extend(rgba[3::4])
    assert len(output) == 16 * 196 * 196 * 3
    (root / "main/poses.rgb").write_bytes(output)


if __name__ == "__main__":
    build()
