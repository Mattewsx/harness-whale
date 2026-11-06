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
        source = json.loads((ROOT / 'docs/release-source.json').read_text())
        self.assertEqual(manifest['version'], source['release'].removeprefix('v'))


    def test_ota_payload_matches_layout_and_asset_pack(self):
        import importlib.util
        manifest = json.loads((ROOT / 'installer/firmware/manifest.json').read_text())
        ota = manifest['ota']
        self.assertNotEqual(ota['file'], 'Harness-Whale-ESP32C3-8MB.bin')
        self.assertIn(manifest['version'], ota['file'])
        spec = importlib.util.spec_from_file_location(
            'make_whale_asset_pack', ROOT / 'device-firmware/tools/make_whale_asset_pack.py')
        pack = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(pack)
        main = ROOT / 'device-firmware/main'
        built = pack.build([(n, (main / n).read_bytes())
                            for n in ('poses.rgb', 'companion.rgb', 'cues.pcm')])
        # The OTA image refuses to install over a different asset pack.
        self.assertEqual(pack.parse(built)['sha256'], ota['asset_pack_sha256'])


if __name__ == '__main__':
    unittest.main()
