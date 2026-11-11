"""Validate committed font coverage for the R10 UI and supported dynamic titles."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def inventory(size):
    source = (ROOT / f"main/lv_font_cn_{size}.c").read_text("ascii")
    indices = re.search(r"indices\[\]\s*=\s*\{([^}]+)", source).group(1)
    return {32 + int(value) for value in indices.split(",") if value.strip()}


class WhaleFonts(unittest.TestCase):
    def test_dynamic_question_arrows_and_math_symbols(self):
        symbols = set(map(ord, "←→↔×÷±≤≥≠−"))
        question = set(map(ord, "暴击率 20% → 25%（暴击伤害 2.5 倍），平均单发伤害提升约多少？"))
        labels = set(map(ord, "A.约 5.0% B.约 5.8% C.7.5% D.25% 0.25 × 2.0 + 0.75 × 1.0"))
        for size in (12, 14, 16, 18):
            self.assertFalse(symbols - inventory(size), f"size {size} missing dynamic math glyphs")
        for size in (14, 16, 18):
            self.assertFalse((question | labels) - inventory(size))

    def test_interface_and_title_sizes_cover_supported_characters(self):
        fixed = set()
        for path in (ROOT / "main").glob("whale_*.c"):
            fixed.update(ord(c) for c in path.read_text("utf8") if ord(c) > 127)
        for size in (12, 14, 16, 18):
            self.assertFalse(
                fixed - inventory(size), f"size {size} is missing UI glyphs"
            )
            self.assertNotIn(0x1F600, inventory(size))
        dynamic = set()
        for high in range(0xB0, 0xD8):
            for low in range(0xA1, 0xFF):
                try:
                    dynamic.add(ord(bytes([high, low]).decode("gb2312")))
                except UnicodeDecodeError:
                    pass
        for size in (14, 16, 18):
            self.assertFalse(dynamic - inventory(size))
        self.assertTrue(set(map(ord, "任务列表0123456789:")) <= inventory(21))
        self.assertTrue(set(map(ord, "0123456789:-")) <= inventory(64))

    def test_complete_alpha_pose_inventory(self):
        self.assertEqual((ROOT / "main/poses.rgb").stat().st_size, 16 * 196 * 196 * 3)


if __name__ == "__main__":
    unittest.main()
