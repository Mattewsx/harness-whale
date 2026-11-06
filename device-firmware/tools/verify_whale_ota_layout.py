#!/usr/bin/env python3
"""Verify the Harness Whale dual-OTA layout of one ESP-IDF build directory.

Checks, on top of tools/verify_firmware.py:
  * otadata plus exactly two equally sized OTA app slots (ota_0, ota_1) and
    no factory slot, so a BLE update always has a free slot to write;
  * the application fits one slot (warns below 10 % headroom);
  * the asset pack in flash_args lands in the "assets" data partition, parses,
    and is the pack the application was built for (WHALE-ASSET-PACK marker);
  * the application image is an ESP32-C3 image of this project.

With --ota-json it also writes the metadata BLE OTA tools and releases use.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
from make_whale_asset_pack import MARKER_PREFIX, parse as parse_pack  # noqa: E402
from verify_firmware import parse_flash_args, parse_partition_table  # noqa: E402

APP = "FoloToy-AI-Passport.bin"
PACK = "whale-assets.bin"
TABLE = "partition_table/partition-table.bin"
APP_TYPE, DATA_TYPE = 0, 1
OTA_0, OTA_1, FACTORY = 0x10, 0x11, 0x00
OTADATA_SUBTYPE, ASSET_SUBTYPE = 0x00, 0x40
ESP32C3_CHIP_ID = 5
APP_DESC = struct.Struct("<II8s32s32s16s16s32s")
APP_DESC_MAGIC = 0xABCD5432
HEADROOM_WARNING = 0.10


def app_identity(image: bytes) -> dict[str, str]:
    """Return version/project from the first segment's esp_app_desc_t."""
    if len(image) < 0x20 + APP_DESC.size or image[0] != 0xE9:
        raise ValueError("application is not an ESP image")
    (chip_id,) = struct.unpack_from("<H", image, 12)
    if chip_id != ESP32C3_CHIP_ID:
        raise ValueError(f"application targets chip id {chip_id}, not ESP32-C3")
    magic, _, _, version, project, *_ = APP_DESC.unpack_from(image, 0x20)
    if magic != APP_DESC_MAGIC:
        raise ValueError("application descriptor is missing")
    text = lambda raw: raw.split(b"\0", 1)[0].decode("ascii")
    return {"version": text(version), "project": text(project)}


def required_pack(image: bytes) -> str:
    found = set(re.findall(re.escape(MARKER_PREFIX.encode()) + rb"([0-9a-f]{64})", image))
    if len(found) != 1:
        raise ValueError("application must carry exactly one WHALE-ASSET-PACK marker")
    return found.pop().decode("ascii")


def verify(build_dir: Path) -> dict[str, object]:
    images = parse_flash_args((build_dir / "flash_args").read_text(encoding="utf-8"))
    for name in (APP, PACK, TABLE):
        if name not in images:
            raise ValueError(f"flash_args is missing {name}")
    table = (build_dir / TABLE).read_bytes()
    partitions, _ = parse_partition_table(table, images[TABLE] + 0x1000)

    def find(kind: int, subtype: int) -> list:
        return [p for p in partitions if p.kind == kind and p.subtype == subtype]

    if find(APP_TYPE, FACTORY):
        raise ValueError("a factory app slot defeats OTA rollback; use ota_0/ota_1 only")
    slots = find(APP_TYPE, OTA_0) + find(APP_TYPE, OTA_1)
    if len(slots) != 2 or slots[0].size != slots[1].size:
        raise ValueError("need exactly two equally sized OTA slots")
    if len([p for p in partitions if p.kind == APP_TYPE]) != 2:
        raise ValueError("only the two OTA slots may be app partitions")
    otadata = find(DATA_TYPE, OTADATA_SUBTYPE)
    if len(otadata) != 1 or otadata[0].size != 0x2000:
        raise ValueError("need one 8 KiB otadata partition")
    assets = [p for p in find(DATA_TYPE, ASSET_SUBTYPE) if p.label == "assets"]
    if len(assets) != 1:
        raise ValueError("need one data/0x40 partition labelled 'assets'")
    asset_part = assets[0]

    if images[APP] != slots[0].offset:
        raise ValueError(f"{APP} must be flashed to ota_0 at 0x{slots[0].offset:x}")
    app = (build_dir / APP).read_bytes()
    if len(app) > slots[0].size:
        raise ValueError(f"application {len(app)} bytes exceeds the {slots[0].size}-byte slot")
    headroom = 1 - len(app) / slots[0].size

    pack = (build_dir / PACK).read_bytes()
    if images[PACK] != asset_part.offset or len(pack) > asset_part.size:
        raise ValueError("asset pack must start at and fit inside the assets partition")
    pack_info = parse_pack(pack)
    need = required_pack(app)
    if need != pack_info["sha256"]:
        raise ValueError(f"application needs asset pack {need}, build has {pack_info['sha256']}")
    identity = app_identity(app)

    print(
        f"OTA layout: PASS (app {len(app)} / {slots[0].size} bytes per slot, "
        f"{headroom:.0%} headroom; assets {len(pack)} / {asset_part.size} bytes)"
    )
    if headroom < HEADROOM_WARNING:
        print("WARNING: OTA slot headroom is below 10 %; future updates may need a new layout")
    return {
        "format": 1,
        "project": identity["project"],
        "version": identity["version"],
        "chip": "esp32c3",
        "app_size": len(app),
        "app_sha256": hashlib.sha256(app).hexdigest(),
        "slot_size": slots[0].size,
        "asset_pack_sha256": pack_info["sha256"],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("build_dir", nargs="?", default="build", type=Path)
    parser.add_argument("--ota-json", type=Path, help="write OTA metadata JSON here")
    args = parser.parse_args(argv)
    try:
        info = verify(args.build_dir.resolve())
    except (OSError, UnicodeDecodeError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    if args.ota_json:
        args.ota_json.write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
