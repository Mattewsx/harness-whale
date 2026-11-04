from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "device-firmware"
if not FIRMWARE.exists():
    FIRMWARE = ROOT.parent / "firmware"
APP_SOURCE = FIRMWARE / "components" / "app" / "src" / "app.c"


class HarnessFirmwareContractTests(unittest.TestCase):
    def test_dedicated_harness_mode_cannot_close_to_blank_home(self) -> None:
        dedicated = FIRMWARE / "main" / "whale_app.c"
        if dedicated.exists():
            source = dedicated.read_text(encoding="utf-8")
            entry = (FIRMWARE / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
            self.assertIn('"whale_app.c"', entry)
            self.assertIn("void app_main(void)", source)
            self.assertNotIn("ui_harness_close", source)
            self.assertNotIn('"main.c"', entry)
            return
        source = APP_SOURCE.read_text(encoding="utf-8")
        harness_branch = source.split("if (ui_harness_is_active()) {", 1)[1]
        harness_branch = harness_branch.split("if (ui_pet_is_active()) {", 1)[0]

        guard = harness_branch.index("#if !PRODUCT_HARNESS_ONLY")
        close = harness_branch.index("ui_harness_close();")
        end = harness_branch.index("#endif", guard)

        self.assertLess(guard, close)
        self.assertLess(close, end)


if __name__ == "__main__":
    unittest.main()
