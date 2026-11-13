#!/usr/bin/env python3
"""Pack assets/images/accessories-v180.png into main/accessories.rgb and main/whale_accessories.h."""

import argparse
from pathlib import Path
import struct
from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[1]
ATLAS_PATH = ROOT / "assets/images/accessories-v180.png"
PLUSH_PATH = ROOT / "assets/images/accessories-v180-plush.png"
SIZE = 196
SKINS = 4
SETS = 4
POSE_IDS = (1, 3, 10, 15, 16, 17, 18, 19, 20, 21)
POSES = len(POSE_IDS)

# Per-(skin, set) crop subregions so each wearable splits into tight anatomical patches:
#   set 0 (W1): [glasses, collar brooch]
#   set 1 (W2): [lily hairpin, pearl communicator]
#   set 2 (W3): [tail ribbon + star charm]
#   set 3 (W4): [lap baby whale plushie]
SUBREGIONS: dict[tuple[int, int], list[tuple[int, int, int, int]]] = {
    (0, 0): [(0, 0, 196, 108), (0, 108, 196, 196)],
    (0, 1): [(108, 0, 196, 72), (108, 72, 196, 196)],
    (0, 2): [(0, 0, 196, 196)],
    (0, 3): [(0, 0, 196, 196)],
    (1, 0): [(0, 0, 196, 108), (0, 108, 196, 196)],
    (1, 1): [(0, 0, 96, 196), (96, 0, 196, 196)],
    (1, 2): [(0, 0, 196, 196)],
    (1, 3): [(0, 0, 196, 196)],
    (2, 0): [(0, 0, 196, 108), (0, 108, 196, 196)],
    (2, 1): [(0, 0, 96, 196), (96, 0, 196, 196)],
    (2, 2): [(0, 0, 196, 196)],
    (2, 3): [(0, 0, 196, 196)],
    (3, 0): [(0, 0, 196, 108), (0, 108, 196, 196)],
    (3, 1): [(108, 0, 196, 75), (108, 75, 196, 196)],
    (3, 2): [(0, 0, 196, 196)],
    (3, 3): [(0, 0, 196, 196)],
}


def build_pose_placements(
    named: dict[str, tuple[int, int, int, int, int]]
) -> dict[tuple[int, int, int], list[tuple[str, int, int]]]:
    """Return (patch_key, dx, dy) list for every (skin 0..3, set 0..3, pose_id)."""
    plan: dict[tuple[int, int, int], list[tuple[str, int, int]]] = {}

    # Skin 0 (Maid): p16..p21 use companion.rgb; p1, p3, p10, p15 use poses.rgb
    for pid in (16, 17, 18):
        plan[(0, 0, pid)] = [("G0", 0, 0), ("B0", 0, 0)]
        plan[(0, 1, pid)] = [("H0", 0, 0), ("M0", 0, 0)]
        plan[(0, 2, pid)] = [("T0", 0, 0)]
        plan[(0, 3, pid)] = [("P0", 0, 0)] if pid in (16, 17) else []
    for pid in (19, 20, 21):
        plan[(0, 0, pid)] = [("G0", 0, 0)]
        plan[(0, 1, pid)] = [("H0", 0, 0), ("M0", 0, 0)]
        plan[(0, 2, pid)] = [("T0", 0, 0)]
        plan[(0, 3, pid)] = []
    # S0 p3 (typing on laptop) & p10 (chin prop with hourglass)
    plan[(0, 0, 3)] = [("G0", 5, -2)]
    plan[(0, 1, 3)] = [("H0", 5, -2), ("M0", 6, -2)]
    plan[(0, 2, 3)] = [("T1", 19, -6)]
    plan[(0, 3, 3)] = []

    plan[(0, 0, 10)] = [("G0", -1, 10)]
    plan[(0, 1, 10)] = [("H0", -1, 10), ("M0", 3, 7)]
    plan[(0, 2, 10)] = [("T1", 20, -4)]
    plan[(0, 3, 10)] = []

    # S0 p1 (resting head on arms) & p15 (sleeping under blanket)
    plan[(0, 0, 1)] = []
    plan[(0, 1, 1)] = [("H0", -4, 12)]
    plan[(0, 2, 1)] = [("T1", 20, -6)]
    plan[(0, 3, 1)] = []

    plan[(0, 0, 15)] = []
    plan[(0, 1, 15)] = [("H0", -5, 6)]
    plan[(0, 2, 15)] = [("T1", 16, -6)]
    plan[(0, 3, 15)] = []

    # Skins 1..3 (Apprentice Witch, Pajamas, Loungewear)
    head_shifts: dict[tuple[int, int], tuple[int, int]] = {
        (1, 1): (-1, 0), (1, 3): (0, -1),  (1, 10): (-1, 0), (1, 16): (0, 0),
        (1, 17): (0, 0), (1, 18): (0, 0),  (1, 19): (0, 0),  (1, 20): (-1, 0), (1, 21): (4, 0),
        (2, 1): (1, 1),  (2, 3): (-4, 0),  (2, 10): (-6, 0), (2, 16): (0, 0),
        (2, 17): (0, 0), (2, 18): (0, 0),  (2, 19): (0, 0),  (2, 20): (0, 0),  (2, 21): (-2, -3),
        (3, 1): (0, 0),  (3, 3): (0, 0),   (3, 10): (0, 0),  (3, 16): (0, 0),
        (3, 17): (0, 0), (3, 18): (0, 0),  (3, 19): (0, 0),  (3, 20): (0, 0),  (3, 21): (-1, -2),
    }
    tail_shifts: dict[tuple[int, int], tuple[int, int]] = {
        (1, 21): (5, 1),
        (1, 15): (4, -2),
        (2, 15): (8, -6),
        (3, 15): (8, -6),
    }
    nap_hairpin_shifts: dict[int, tuple[int, int]] = {
        1: (-2, 2),
        2: (-12, 8),
        3: (-5, 6),
    }

    for s in (1, 2, 3):
        g, b, h, m, t, p = f"G{s}", f"B{s}", f"H{s}", f"M{s}", f"T{s}", f"P{s}"
        for pid in POSE_IDS:
            tdx, tdy = tail_shifts.get((s, pid), (0, 0))
            if pid == 15:
                nhx, nhy = nap_hairpin_shifts[s]
                plan[(s, 0, 15)] = []
                plan[(s, 1, 15)] = [(h, nhx, nhy)]
                plan[(s, 2, 15)] = [(t, tdx, tdy)]
                plan[(s, 3, 15)] = []
                continue
            hdx, hdy = head_shifts[(s, pid)]
            if pid in (1, 16, 17, 18):
                bdx = -1 if (s == 1 and pid == 1) else 0
                bdy = -1 if (s == 1 and pid == 1) else (1 if (s == 1 and pid == 18) else 0)
                plan[(s, 0, pid)] = [(g, hdx, hdy), (b, bdx, bdy)]
            else:
                plan[(s, 0, pid)] = [(g, hdx, hdy)]
            plan[(s, 1, pid)] = [(h, hdx, hdy), (m, hdx, hdy)]
            plan[(s, 2, pid)] = [(t, tdx, tdy)]
            plan[(s, 3, pid)] = [(p, 0, 0)] if pid in (1, 16, 17) else []

    # Keep a held plush where its hands match the base art; when the hands are
    # busy, put the standalone plush beside the character instead of hiding it
    # or painting an extra pair of hands over the activity.
    for s in range(SKINS):
        for pid in POSE_IDS:
            if not plan[(s, 3, pid)]:
                plan[(s, 3, pid)] = [("Q", 0, 0)]
    _ = named
    return plan


def pack_patch(cell: Image.Image, box: tuple[int, int, int, int]) -> bytes:
    x0, y0, x1, y1 = box
    cropped = cell.crop((x0, y0, x1, y1)).convert("RGBA")
    rgb = bytearray()
    alpha = bytearray()
    for r, g, b, a in cropped.getdata():
        if a == 0:
            r = g = b = 0
        value = ((r * 31 + 127) // 255) << 11
        value |= ((g * 63 + 127) // 255) << 5
        value |= (b * 31 + 127) // 255
        rgb += struct.pack("<H", value)
        alpha.append(a)
    data = bytes(rgb + alpha)
    if len(data) % 4:
        data += b"\x00" * (4 - len(data) % 4)
    return data


def verify_pack(
    blob: bytes,
    patches: list[tuple[int, int, int, int, int]],
    draws: list[tuple[int, int, int]],
) -> None:
    expected_off = 0
    for idx, (off, pw, ph, _bx, _by) in enumerate(patches):
        if off != expected_off or off % 4 != 0:
            raise AssertionError(f"Patch {idx} offset {off} invalid (expected {expected_off})")
        raw_len = pw * ph * 3
        padded_len = (raw_len + 3) & ~3
        if off + padded_len > len(blob):
            raise AssertionError(f"Patch {idx} exceeds blob length {len(blob)}")
        expected_off += padded_len
    if expected_off != len(blob) or len(blob) % 4 != 0:
        raise AssertionError(f"Blob length {len(blob)} != expected {expected_off}")
    for d_idx, (p_idx, px, py) in enumerate(draws):
        _off, pw, ph, _bx, _by = patches[p_idx]
        if px < 0 or py < 0 or px + pw > SIZE or py + ph > SIZE:
            raise AssertionError(f"Draw {d_idx} out of {SIZE}x{SIZE} bounds")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--atlas", type=Path, default=ATLAS_PATH)
    parser.add_argument("--plush", type=Path, default=PLUSH_PATH)
    parser.add_argument("--out-rgb", type=Path, default=ROOT / "main/accessories.rgb")
    parser.add_argument("--out-header", type=Path, default=ROOT / "main/whale_accessories.h")
    parser.add_argument("--check", action="store_true", help="Verify existing outputs without writing")
    args = parser.parse_args()

    atlas = Image.open(args.atlas).convert("RGBA")
    if atlas.size != (SETS * SIZE, SKINS * SIZE):
        raise ValueError(f"Expected {SETS * SIZE}x{SKINS * SIZE} atlas, got {atlas.size}")

    blob = bytearray()
    # (offset, width, height, base_x, base_y)
    patches: list[tuple[int, int, int, int, int]] = []
    named: dict[str, tuple[int, int, int, int, int]] = {}
    role_names = [("G", "B"), ("H", "M"), ("T",), ("P",)]

    for s in range(SKINS):
        for w in range(SETS):
            cell = atlas.crop((w * SIZE, s * SIZE, (w + 1) * SIZE, (s + 1) * SIZE))
            subs = SUBREGIONS[(s, w)]
            roles = role_names[w]
            if len(subs) != len(roles):
                raise ValueError(f"Mismatched subregion roles for skin={s}, set={w}")
            for role, (rx0, ry0, rx1, ry1) in zip(roles, subs):
                sub = cell.crop((rx0, ry0, rx1, ry1))
                bb = sub.getbbox()
                if not bb:
                    raise ValueError(f"Empty patch {role}{s} in skin={s}, set={w + 1}")
                bx0, by0, bx1, by1 = rx0 + bb[0], ry0 + bb[1], rx0 + bb[2], ry0 + bb[3]
                pw, ph = bx1 - bx0, by1 - by0
                offset = len(blob)
                if offset % 4 != 0:
                    raise AssertionError("Unaligned patch offset")
                raw = pack_patch(cell, (bx0, by0, bx1, by1))
                if offset + pw * ph * 3 > offset + len(raw):
                    raise AssertionError("Patch byte length mismatch")
                blob += raw
                idx = len(patches)
                patches.append((offset, pw, ph, bx0, by0))
                named[f"{role}{s}"] = (idx, pw, ph, bx0, by0)

    standalone = Image.open(args.plush).convert("RGBA")
    bounds = standalone.getchannel("A").point(lambda a: 255 if a >= 192 else 0).getbbox()
    if not bounds:
        raise ValueError("Standalone plush must have a visible alpha silhouette")
    standalone = ImageOps.contain(standalone.crop(bounds), (42, 34), Image.Resampling.LANCZOS)
    pw, ph = standalone.size
    idx = len(patches)
    named["Q"] = (idx, pw, ph, 7, 158)
    patches.append((len(blob), pw, ph, 7, 158))
    blob += pack_patch(standalone, (0, 0, pw, ph))

    placements = build_pose_placements(named)
    draws: list[tuple[int, int, int]] = []
    draw_cache: dict[tuple[tuple[int, int, int], ...], tuple[int, int]] = {}
    pose_map: list[list[list[tuple[int, int]]]] = []

    for s in range(SKINS):
        s_rows: list[list[tuple[int, int]]] = []
        for w in range(SETS):
            w_rows: list[tuple[int, int]] = []
            for pid in POSE_IDS:
                seq: list[tuple[int, int, int]] = []
                for key, dx, dy in placements[(s, w, pid)]:
                    p_idx, pw, ph, bx0, by0 = named[key]
                    px, py = bx0 + dx, by0 + dy
                    if px < 0 or py < 0 or px + pw > SIZE or py + ph > SIZE:
                        raise ValueError(
                            f"Out-of-bounds draw {key} on skin={s} set={w + 1} pose={pid}: "
                            f"({px},{py})+({pw},{ph}) exceeds {SIZE}x{SIZE}"
                        )
                    seq.append((p_idx, px, py))
                t_seq = tuple(seq)
                if not t_seq:
                    w_rows.append((0, 0))
                elif t_seq in draw_cache:
                    w_rows.append(draw_cache[t_seq])
                else:
                    first = len(draws)
                    draws.extend(t_seq)
                    entry = (first, len(t_seq))
                    draw_cache[t_seq] = entry
                    w_rows.append(entry)
            s_rows.append(w_rows)
        pose_map.append(s_rows)

    verify_pack(bytes(blob), patches, draws)

    patch_lines = ",\n".join(
        f"    {{{off},{pw},{ph}}}" for off, pw, ph, _bx, _by in patches
    )
    draw_lines = ",\n".join(
        f"    {{{p_idx},{px},{py}}}" for p_idx, px, py in draws
    )
    map_lines = []
    for s in range(SKINS):
        set_strs = []
        for w in range(SETS):
            pose_strs = ", ".join(f"{{{f},{c}}}" for f, c in pose_map[s][w])
            set_strs.append(f"        {{{pose_strs}}}")
        map_lines.append("    {\n" + ",\n".join(set_strs) + "\n    }")

    header = (
        "/* Generated by tools/make_whale_accessories.py. */\n"
        "#pragma once\n"
        "#include <stdint.h>\n"
        f"#define WHALE_ACC_SKINS {SKINS}\n"
        f"#define WHALE_ACC_SETS {SETS}\n"
        f"#define WHALE_ACC_POSES {POSES}\n"
        f"#define WHALE_ACC_PATCH_COUNT {len(patches)}\n"
        f"#define WHALE_ACC_DRAW_COUNT {len(draws)}\n"
        f"#define WHALE_ACC_BYTES {len(blob)}\n"
        "typedef struct { uint32_t offset; uint16_t width, height; } whale_acc_patch_t;\n"
        "typedef struct { uint8_t patch, x, y; } whale_acc_draw_t;\n"
        "typedef struct { uint8_t first, count; } whale_acc_entry_t;\n"
        "static const whale_acc_patch_t whale_acc_patches[WHALE_ACC_PATCH_COUNT] = {\n"
        f"{patch_lines},\n"
        "};\n"
        "static const whale_acc_draw_t whale_acc_draws[WHALE_ACC_DRAW_COUNT] = {\n"
        f"{draw_lines},\n"
        "};\n"
        "static const whale_acc_entry_t whale_acc_map[WHALE_ACC_SKINS][WHALE_ACC_SETS][WHALE_ACC_POSES] = {\n"
        + ",\n".join(map_lines)
        + "\n};\n"
        "#define WHALE_ACC_DSC(ptr, pw, ph) \\\n"
        "    {.header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8, \\\n"
        "                .w = (pw), .h = (ph), .stride = (pw) * 2}, \\\n"
        "     .data_size = (uint32_t)(pw) * (ph) * 3u, .data = (ptr)}\n"
        "#define WHALE_ACC_SPRITES_INIT(base) { \\\n"
        + ", \\\n".join(f"    WHALE_ACC_DSC((base) + {off}u, {pw}, {ph})" for off, pw, ph, _bx, _by in patches)
        + " \\\n}\n"
    )
    if args.check:
        if args.out_rgb.read_bytes() != bytes(blob):
            raise SystemExit(f"Mismatch in {args.out_rgb}")
        if args.out_header.read_text(encoding="utf-8") != header:
            raise SystemExit(f"Mismatch in {args.out_header}")
        print(
            f"Verified {len(patches)} patches, {len(draws)} deduplicated draws, "
            f"{len(blob)} bytes (4-byte aligned, {SIZE}x{SIZE} bounded)"
        )
        return

    args.out_rgb.write_bytes(blob)
    args.out_header.write_text(header, encoding="utf-8", newline="\n")
    print(
        f"Wrote {len(patches)} patches, {len(draws)} deduplicated draws, "
        f"{len(blob)} bytes ({len(blob) / 1024:.1f} KB)"
    )


if __name__ == "__main__":
    main()
