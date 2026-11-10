"""Run synthetic plugin approval regressions in the normal host test suite."""
from pathlib import Path
import shutil
import subprocess
import unittest


class ApprovalSafety(unittest.TestCase):
    def test_computer_answerer_registration_order(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("Node with TypeScript stripping required")
        result = subprocess.run([node, str(Path(__file__).with_name("replay_approval_order.mjs"))],
                                capture_output=True, text=True, encoding="utf-8", timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_complete_scope_and_cleanup_races(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("Node with TypeScript stripping required")
        result = subprocess.run([node, str(Path(__file__).with_name("replay_approval_safety.mjs"))],
                                capture_output=True, text=True, encoding="utf-8", timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
