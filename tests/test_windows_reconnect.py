"""Paired-peer lookup is read-only, unambiguous and independent of advertising."""
import asyncio
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness/bridge"))
import whale_pairing as pairing


class PairedPeerTests(unittest.IsolatedAsyncioTestCase):
    def fixtures(self, *, paired=True, peers=None):
        self.closed = []
        self.opened = []
        device = SimpleNamespace(name="HARNESS-WHALE", bluetooth_address=0x123456789ABC,
                                 device_information=SimpleNamespace(pairing=SimpleNamespace(is_paired=paired)),
                                 close=lambda: self.closed.append(True))

        async def from_id(value):
            self.opened.append(value)
            return device

        async def from_address(value):
            self.opened.append(value)
            return device

        async def find(selector):
            self.assertEqual(selector, "paired-only")
            return peers if peers is not None else [SimpleNamespace(name="HARNESS-WHALE", id="fixture")]

        return {
            "winrt.windows.devices.bluetooth": SimpleNamespace(BluetoothLEDevice=SimpleNamespace(
                get_device_selector_from_pairing_state=lambda flag: "paired-only" if flag else "unpaired",
                from_id_async=from_id, from_bluetooth_address_async=from_address)),
            "winrt.windows.devices.enumeration": SimpleNamespace(DeviceInformation=SimpleNamespace(
                find_all_async_aqs_filter=find)),
            "bleak.backends.device": SimpleNamespace(BLEDevice=lambda address, name, details: SimpleNamespace(
                address=address, name=name, details=details)),
            "bleak.backends.winrt.scanner": SimpleNamespace(RawAdvData=lambda **kw: SimpleNamespace(**kw)),
        }

    async def test_paired_identity_snapshot_does_not_hold_native_gatt_object(self):
        modules = self.fixtures()
        with patch.dict(sys.modules, modules), patch.object(pairing.sys, "platform", "win32"):
            peer = await pairing.paired_passport_device("HARNESS-WHALE")
        self.assertEqual(peer.address, "12:34:56:78:9A:BC")
        self.assertEqual(peer.details.adv.bluetooth_address, 0x123456789ABC)
        self.assertEqual(self.closed, [True])

    async def test_multiple_paired_names_never_choose_an_arbitrary_device(self):
        modules = self.fixtures(peers=[SimpleNamespace(name="HARNESS-WHALE", id=i) for i in (1, 2)])
        with patch.dict(sys.modules, modules), patch.object(pairing.sys, "platform", "win32"):
            with self.assertRaisesRegex(ValueError, "--address"):
                await pairing.paired_passport_device("HARNESS-WHALE")
        self.assertEqual(self.opened, [])

    async def test_unrelated_paired_device_is_ignored(self):
        modules = self.fixtures(peers=[SimpleNamespace(name="another peripheral", id="other")])
        with patch.dict(sys.modules, modules), patch.object(pairing.sys, "platform", "win32"):
            self.assertIsNone(await pairing.paired_passport_device("HARNESS-WHALE"))
        self.assertEqual(self.opened, [])

    async def test_explicit_address_still_requires_an_existing_pair(self):
        modules = self.fixtures(paired=False)
        with patch.dict(sys.modules, modules), patch.object(pairing.sys, "platform", "win32"):
            self.assertIsNone(await pairing.paired_passport_device("HARNESS-WHALE", "12:34:56:78:9A:BC"))
        self.assertEqual(self.opened, [0x123456789ABC])
        self.assertEqual(self.closed, [True])

    async def test_non_windows_does_not_load_windows_or_bleak_modules(self):
        with patch.object(pairing.sys, "platform", "linux"):
            self.assertIsNone(await pairing.paired_passport_device("fixture"))

    async def test_pinned_winrt_backend_accepts_cached_identity_without_scanning(self):
        if sys.platform != "win32":
            self.skipTest("WinRT backend requires Windows")
        try:
            from bleak.backends.winrt.client import BleakClientWinRT
        except ImportError:
            self.skipTest("Bleak WinRT backend not installed")
        modules = self.fixtures()
        modules = {k: v for k, v in modules.items() if k.startswith("winrt.")}
        with patch.dict(sys.modules, modules):
            peer = await pairing.paired_passport_device("HARNESS-WHALE")
        client = BleakClientWinRT(peer, winrt={})
        self.assertEqual(client._device_info, 0x123456789ABC)
        self.assertEqual(self.closed, [True])


if __name__ == "__main__":
    unittest.main()
