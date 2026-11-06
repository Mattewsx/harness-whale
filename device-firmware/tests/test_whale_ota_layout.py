#!/usr/bin/env python3
"""Host tests for the dual-OTA partition layout, asset pack and OTA verifier."""

from __future__ import annotations

import hashlib
import importlib.util
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tools"))


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


PACK = load("make_whale_asset_pack")
VERIFY = load("verify_firmware")
OTA = load("verify_whale_ota_layout")
ASSETS = [ROOT / "main" / n for n in ("poses.rgb", "companion.rgb", "cues.pcm")]
FLASH = 8 * 1024 * 1024


def csv_partitions() -> list[dict[str, object]]:
    rows = []
    for line in (ROOT / "partitions.csv").read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        name, kind, subtype, offset, size = [f.strip() for f in line.split(",")[:5]]
        rows.append({"name": name, "type": kind, "subtype": subtype,
                     "offset": int(offset, 0), "size": int(size, 0)})
    return rows


def table(entries) -> bytes:
    raw = bytearray(b"\xff" * VERIFY.PARTITION_TABLE_SIZE)
    for index, (kind, subtype, offset, size, label) in enumerate(entries):
        VERIFY.ENTRY.pack_into(raw, index * VERIFY.ENTRY.size, 0x50AA, kind, subtype, offset,
                               size, label.encode().ljust(16, b"\0"), 0)
    marker = len(entries) * VERIFY.ENTRY.size
    struct.pack_into("<H", raw, marker, 0xEBEB)
    raw[marker + 16:marker + 32] = hashlib.md5(raw[:marker]).digest()
    return bytes(raw)


def app_image(marker: str | None, version: str = "whale-v1.4.0", chip: int = 5) -> bytes:
    image = bytearray(4096)
    image[0] = 0xE9
    struct.pack_into("<H", image, 12, chip)
    OTA.APP_DESC.pack_into(image, 0x20, OTA.APP_DESC_MAGIC, 0, b"", version.encode(),
                           b"FoloToy-AI-Passport", b"", b"", b"")
    if marker:
        image[1000:1000 + len(marker)] = marker.encode()
    return bytes(image)


class PartitionBudget(unittest.TestCase):
    def test_repository_layout_fits_two_slots_and_the_asset_pack(self):
        rows = csv_partitions()
        by_name = {row["name"]: row for row in rows}
        ordered = sorted(rows, key=lambda r: r["offset"])
        for left, right in zip(ordered, ordered[1:]):
            self.assertLessEqual(left["offset"] + left["size"], right["offset"])
        self.assertLessEqual(ordered[-1]["offset"] + ordered[-1]["size"], FLASH)
        self.assertNotIn("factory", by_name)
        slot0, slot1 = by_name["ota_0"], by_name["ota_1"]
        self.assertEqual(slot0["size"], slot1["size"])
        self.assertEqual(slot0["offset"] % 0x10000, 0)
        self.assertEqual(slot1["offset"] % 0x10000, 0)
        self.assertEqual(by_name["otadata"]["size"], 0x2000)
        # NVS stays where v1.3 had it so the layout keeps the same NVS window.
        self.assertEqual((by_name["nvs"]["offset"], by_name["nvs"]["size"]), (0x9000, 0x6000))
        pack = PACK.build([(p.name, p.read_bytes()) for p in ASSETS])
        self.assertEqual(by_name["assets"]["subtype"], "0x40")
        self.assertLessEqual(len(pack), by_name["assets"]["size"])
        # v1.3.1 application was 4,858,464 bytes with these assets embedded;
        # without them it must still leave real headroom in one slot.
        app_without_assets = 4_858_464 - sum(p.stat().st_size for p in ASSETS)
        self.assertLess(app_without_assets, slot0["size"] * 0.9)


class AssetPack(unittest.TestCase):
    def test_round_trip_alignment_and_header(self):
        pack = PACK.build([("a.bin", b"x" * 5), ("b.bin", b"y" * 3)])
        info = PACK.parse(pack)
        self.assertEqual(info["entries"], {"a.bin": (0, 5), "b.bin": (8, 3)})
        self.assertEqual(info["sha256"], hashlib.sha256(pack[0x100:]).hexdigest())
        header = PACK.c_header(pack)
        self.assertIn(info["sha256"], header)
        self.assertIn("#define WHALE_ASSET_B_BIN_OFFSET 8u", header)

    def test_corruption_is_rejected(self):
        pack = bytearray(PACK.build([("a.bin", b"x" * 64)]))
        pack[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            PACK.parse(bytes(pack))
        pack[-1] ^= 1
        pack[0x40] ^= 1
        with self.assertRaisesRegex(ValueError, "CRC"):
            PACK.parse(bytes(pack))
        with self.assertRaises(ValueError):
            PACK.build([("a-name-that-is-too-long.bin", b"x")])

    def test_header_crc_matches_firmware_crc(self):
        pack = PACK.build([("a.bin", b"x")])
        self.assertEqual(struct.unpack_from("<I", pack, 0xFC)[0], zlib.crc32(pack[:0xFC]))

    def test_tool_respects_partition_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "p.bin"
            src = Path(tmp) / "a.bin"
            src.write_bytes(b"z" * 600)
            self.assertEqual(PACK.main(["--output", str(out), "--max-size", "0x200", str(src)]), 1)
            self.assertEqual(PACK.main(["--output", str(out), "--header", str(Path(tmp) / "h.h"),
                                        str(src)]), 0)
            self.assertTrue((Path(tmp) / "h.h").read_text().startswith("/* Generated"))


class OtaLayoutVerifier(unittest.TestCase):
    ENTRIES = (
        (1, 2, 0x9000, 0x6000, "nvs"),
        (1, 0, 0xF000, 0x2000, "otadata"),
        (1, 1, 0x11000, 0x1000, "phy_init"),
        (0, 0x10, 0x20000, 0x280000, "ota_0"),
        (0, 0x11, 0x2A0000, 0x280000, "ota_1"),
        (1, 0x40, 0x520000, 0x2D0000, "assets"),
        (1, 3, 0x7F0000, 0x10000, "coredump"),
    )

    def build_dir(self, tmp: Path, entries=ENTRIES, marker: str | None = "auto",
                  app: bytes | None = None) -> Path:
        pack = PACK.build([("a.bin", b"x" * 64)])
        sha = PACK.parse(pack)["sha256"]
        if marker == "auto":
            marker = PACK.MARKER_PREFIX + sha
        (tmp / "partition_table").mkdir()
        (tmp / "partition_table/partition-table.bin").write_bytes(table(entries))
        (tmp / "whale-assets.bin").write_bytes(pack)
        (tmp / OTA.APP).write_bytes(app if app is not None else app_image(marker))
        (tmp / "flash_args").write_text(
            "--flash_mode dio --flash_size 8MB\n0x8000 partition_table/partition-table.bin\n"
            f"0x20000 {OTA.APP}\n0x520000 whale-assets.bin\n", encoding="utf-8")
        return tmp

    def test_valid_layout_writes_metadata(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = self.build_dir(Path(tmp))
            out = Path(tmp) / "ota.json"
            self.assertEqual(OTA.main([str(build), "--ota-json", str(out)]), 0)
            text = out.read_text()
            self.assertIn('"version": "whale-v1.4.0"', text)
            self.assertIn('"project": "FoloToy-AI-Passport"', text)

    def test_factory_slot_is_rejected(self):
        entries = self.ENTRIES[:3] + ((0, 0, 0x20000, 0x280000, "factory"),) + self.ENTRIES[4:]
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, "factory"):
                OTA.verify(self.build_dir(Path(tmp), entries))

    def test_unequal_slots_are_rejected(self):
        entries = list(self.ENTRIES)
        entries[4] = (0, 0x11, 0x2A0000, 0x270000, "ota_1")
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, "equally sized"):
                OTA.verify(self.build_dir(Path(tmp), tuple(entries)))

    def test_missing_or_wrong_asset_marker_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, "marker"):
                OTA.verify(self.build_dir(Path(tmp), marker=None))
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, "needs asset pack"):
                OTA.verify(self.build_dir(Path(tmp), marker=PACK.MARKER_PREFIX + "0" * 64))

    def test_wrong_chip_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            pack_sha = PACK.parse(PACK.build([("a.bin", b"x" * 64)]))["sha256"]
            app = app_image(PACK.MARKER_PREFIX + pack_sha, chip=9)
            with self.assertRaisesRegex(ValueError, "ESP32-C3"):
                OTA.verify(self.build_dir(Path(tmp), app=app))


if __name__ == "__main__":
    unittest.main()
