"""Check the recorded build inputs and the released installer's image mode."""
import hashlib
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReleaseContractTests(unittest.TestCase):
    def test_firmware_source_matches_recorded_build_inputs(self):
        manifest = json.loads((ROOT / 'docs/release-source.json').read_text())
        self.assertIn('main/whale_reply_cache.c', manifest['sha256'])
        self.assertIn('partitions.csv', manifest['sha256'])
        for name, expected in manifest['sha256'].items():
            with self.subTest(path=name):
                actual = hashlib.sha256((ROOT / 'device-firmware' / name).read_bytes()).hexdigest()
                self.assertEqual(actual, expected)

    def test_full_image_installer_does_not_accept_application_image(self):
        manifest = json.loads((ROOT / 'installer/firmware/manifest.json').read_text())
        self.assertEqual(manifest['offset'], '0x0')
        self.assertEqual(manifest['chip'], 'esp32c3')
        self.assertEqual(set(manifest['sha256']), {'Harness-Whale-ESP32C3-8MB.bin'})
        self.assertEqual(manifest['version'], '1.2.0')


if __name__ == '__main__':
    unittest.main()
