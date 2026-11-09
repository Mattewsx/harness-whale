"""Create four 20px wardrobe alpha masks using Pillow, preserving other icons."""
from pathlib import Path
import re

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
ICONS = ("maid", "witch", "pajamas", "lounge")
SCALE = 4


def mask(name):
    image = Image.new("L", (24 * SCALE, 24 * SCALE), 0)
    draw = ImageDraw.Draw(image)

    def line(points, width=1.6):
        draw.line([(round(x * SCALE), round(y * SCALE)) for x, y in points],
                  fill=255, width=round(width * SCALE), joint="curve")

    def shape(points):
        draw.polygon([(round(x * SCALE), round(y * SCALE)) for x, y in points], fill=65)
        line(points + [points[0]])

    def dot(x, y):
        draw.ellipse(((x - .65) * SCALE, (y - .65) * SCALE,
                      (x + .65) * SCALE, (y + .65) * SCALE), fill=255)

    if name == "maid":
        shape([(8, 3), (4, 5), (3, 9), (7, 10), (8, 11), (4, 21),
               (20, 21), (16, 11), (17, 10), (21, 9), (20, 5), (16, 3),
               (14, 6), (10, 6)])
        line([(8, 10), (16, 10)])
        line([(10, 11), (8, 18), (16, 18), (14, 11)])
        line([(8, 20), (8, 21), (12, 20), (16, 21), (16, 20)], 1)
    elif name == "witch":
        shape([(13, 1), (9, 8), (3, 10), (7, 11), (12, 10),
               (17, 11), (21, 10), (16, 8)])
        line([(10, 6), (15, 6)], 1)
        shape([(8, 12), (5, 14), (3, 18), (7, 18), (6, 22),
               (18, 22), (17, 18), (21, 18), (19, 14), (16, 12)])
        line([(10, 12), (12, 14), (14, 12)], 1)
        line([(8, 17), (16, 17)], 1)
    elif name == "pajamas":
        shape([(8, 3), (4, 5), (2, 16), (6, 17), (8, 10), (7, 21),
               (17, 21), (16, 10), (18, 17), (22, 16), (20, 5), (16, 3)])
        line([(8, 3), (12, 9), (16, 3)])
        line([(12, 9), (12, 21)], 1)
        for y in (12, 15, 18):
            dot(13.8, y)
        line([(16, 8), (18, 8), (18, 11), (16, 11)], 1)
        draw.arc((6 * SCALE, .5 * SCALE, 18 * SCALE, 10.5 * SCALE),
                 180, 360, fill=255, width=round(1.5 * SCALE))
        line([(7, 6), (17, 6)], 1)
        dot(9, 3.5)
        dot(15, 3.5)

    else:
        shape([(8, 2), (6, 5), (6, 8), (3, 10), (2, 16), (6, 17),
               (7, 14), (6, 22), (11, 22), (12, 17), (13, 22),
               (18, 22), (17, 14), (18, 17), (22, 16), (21, 10),
               (18, 8), (18, 5), (16, 2)])
        line([(8, 4), (9, 8), (12, 10), (15, 8), (16, 4)])
        line([(12, 10), (12, 16)], 1)
        line([(8, 12), (8, 15), (10, 16)], 1)
        line([(16, 12), (16, 15), (14, 16)], 1)
    return image.resize((20, 20), Image.Resampling.LANCZOS)


def build():
    path = ROOT / "main/room_icons.h"
    source = path.read_text("utf8")
    for name in ICONS + ("hoodie",):
        source = re.sub(r"    \[ICON_skin_" + name + r"\] = \{.*?\n    \},\n", "", source, flags=re.S)
    enum = re.search(r"enum room_icon \{ (.*?) \};", source)
    members = [m.strip() for m in enum[1].split(",") if not m.strip().startswith("ICON_skin_")]
    members += ["ICON_skin_" + name for name in ICONS]
    source = source[:enum.start()] + "enum room_icon { " + ", ".join(members) + " };" + source[enum.end():]
    end = source.rfind("};")
    blocks = ""
    for name in ICONS:
        pixels = list(mask(name).getdata())
        blocks += f"    [ICON_skin_{name}] = {{\n"
        blocks += "\n".join("        " + ",".join(map(str, pixels[y * 20:(y + 1) * 20])) + ","
                            for y in range(20))
        blocks += "\n    },\n"
    source = source[:end] + blocks + source[end:]
    path.write_text(source, "utf8", newline="\n")
    print("Wardrobe masks generated: maid dress, witch uniform, button pajamas, hooded onesie")


if __name__ == "__main__":
    build()
