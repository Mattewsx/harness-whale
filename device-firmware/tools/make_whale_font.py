"""Generate a 2-bpp LVGL font from an OFL Noto Sans CJK SC OTF (Pillow)."""

import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

# Questions arrive at runtime, so scanning fixed UI strings cannot discover
# their arrows and arithmetic operators. Keep these in every body-sized font.
QUESTION_SYMBOLS = "←→↔×÷±≤≥≠−"


def build(otf, folder, size):
    # BASIC layout gives whole-pixel advances, matching the committed fonts
    # whether or not Pillow was built with libraqm.
    font = ImageFont.truetype(otf, size, layout_engine=ImageFont.Layout.BASIC)
    chars = set(chr(i) for i in range(32, 127))
    for hi in range(0xB0, 0xD8 if size in (14, 16, 18) else 0xB0):
        for lo in range(0xA1, 0xFF):
            try:
                chars.add(bytes([hi, lo]).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    chars.update("，。？！（）《》、：；“”‘’—…￥¥≈")
    chars.update(QUESTION_SYMBOLS)
    for f in folder.glob("whale_*.c"):
        if not f.name.startswith("lv_font_cn_"):
            chars.update(c for c in f.read_text("utf8") if ord(c) > 127)
    if size == 21:
        chars = set("任务列表0123456789:")
    if size == 64:  # desk display clock digits only
        chars = set("0123456789:-")
    codes = sorted(ord(c) for c in chars)
    bitmap = bytearray()
    desc = ["{0}"]
    for cp in codes:
        ch = chr(cp)
        l, t, r, b = font.getbbox(ch, anchor="ls")
        adv = round(font.getlength(ch) * 16)
        if r > l and b > t:
            img = Image.new("L", (r - l, b - t))
            ImageDraw.Draw(img).text((-l, -t), ch, font=font, fill=255, anchor="ls")
            box = img.getbbox()
        else:
            box = None
        start = len(bitmap)
        if box:
            img = img.crop(box)
            w, h = img.size
            ox = l + box[0]
            oy = -(t + box[3])
            vals = [min(3, (v + 42) // 85) for v in img.tobytes()]
            vals += [0] * ((-len(vals)) % 4)
            bitmap.extend(
                (a << 6) | (b << 4) | (c << 2) | d
                for a, b, c, d in zip(vals[::4], vals[1::4], vals[2::4], vals[3::4])
            )
        else:
            w = h = ox = oy = 0
        desc.append(
            f"{{.bitmap_index={start},.adv_w={adv},.box_w={w},.box_h={h},.ofs_x={ox},.ofs_y={oy}}}"
        )
    with (folder / f"lv_font_cn_{size}.c").open("w", encoding="ascii") as out:
        out.write(
            '/* Generated from Noto Sans CJK SC, see FONT-LICENSE.txt. */\n#include "lvgl.h"\nstatic const uint8_t data[]={\n'
        )
        for i in range(0, len(bitmap), 32):
            out.write(",".join(map(str, bitmap[i : i + 32])) + ",\n")
        out.write(
            "};\nstatic const lv_font_fmt_txt_glyph_dsc_t glyphs[]={\n"
            + ",\n".join(desc)
            + "};\n"
        )
        out.write(
            "static const uint16_t indices[]={"
            + ",".join(str(v - 32) for v in codes)
            + "};\n"
        )
        out.write(
            "static const lv_font_fmt_txt_cmap_t maps[]={{.range_start=32,.range_length="
            + str(codes[-1] - 31)
            + ",.glyph_id_start=1,.unicode_list=indices,.list_length="
            + str(len(codes))
            + ",.type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}};\n"
        )
        out.write(
            "static const lv_font_fmt_txt_dsc_t cfg={.glyph_bitmap=data,.glyph_dsc=glyphs,.cmaps=maps,.cmap_num=1,.bpp=2};\nconst lv_font_t lv_font_cn_16={.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height=20,.base_line=4,.dsc=&cfg};\n"
        )
    dst = folder / f"lv_font_cn_{size}.c"
    contents = (
        dst.read_text("ascii")
        .replace("lv_font_cn_16 =", "lv_font_cn_" + str(size) + " =")
        .replace("lv_font_cn_16=", f"lv_font_cn_{size}=")
        .replace(".line_height=20", f".line_height={size+3}")
    )
    if size == 16:
        contents += "\nunsigned whale_glyph_advance(unsigned cp){unsigned lo=0,hi=sizeof(indices)/sizeof(indices[0]);while(lo<hi){unsigned mid=(lo+hi)/2;unsigned v=indices[mid]+32;if(cp==v)return (glyphs[mid+1].adv_w+8)/16;if(cp<v)hi=mid;else lo=mid+1;}return 0;}\n"
    dst.write_text(contents, encoding="ascii", newline="\n")
    print(len(codes), "glyphs;", len(bitmap), "bitmap bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("otf")
    args = parser.parse_args()
    for size in (12, 14, 16, 18, 21, 64):
        build(args.otf, Path(__file__).resolve().parents[1] / "main", size)
