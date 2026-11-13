"""Windows numeric comparison for the authenticated Passport service."""
import asyncio
import ctypes
import sys
from types import SimpleNamespace


class PairingDeclined(ConnectionError):
    pass


async def paired_passport_device(name, address=None):
    """Resolve an already-paired Windows peer without requiring advertisements.

    Windows can retain a GATT connection after a client disconnects. Such a
    peripheral stops advertising, so scanning by name (or passing a bare address
    to Bleak, which also scans) cannot reconnect. No pairing record is changed.
    """
    if sys.platform != "win32":
        return None
    from bleak.backends.device import BLEDevice
    from bleak.backends.winrt.scanner import RawAdvData
    from winrt.windows.devices.bluetooth import BluetoothLEDevice
    from winrt.windows.devices.enumeration import DeviceInformation

    device = None
    try:
        if address:
            number = int(address.replace(":", "").replace("-", ""), 16)
            device = await BluetoothLEDevice.from_bluetooth_address_async(number)
        else:
            selector = BluetoothLEDevice.get_device_selector_from_pairing_state(True)
            peers = await DeviceInformation.find_all_async_aqs_filter(selector)
            matches = [p for p in peers if p.name.casefold() == name.casefold()]
            if len(matches) > 1:
                raise ValueError("Multiple paired Passports have this name; select --address")
            if not matches:
                return None
            device = await BluetoothLEDevice.from_id_async(matches[0].id)
        if device is None or not device.device_information.pairing.is_paired:
            return None
        number = device.bluetooth_address
        formatted = ":".join(f"{number:012X}"[i:i + 2] for i in range(0, 12, 2))
        # Bleak 1.1.1's WinRT backend consumes .adv.bluetooth_address from its
        # platform details. Snapshot that identity, not a live WinRT object that
        # would retain a GATT connection. Keep this adapter covered by a backend
        # constructor test when changing the pinned Bleak dependency.
        details = RawAdvData(adv=SimpleNamespace(bluetooth_address=number), scan=None)
        return BLEDevice(formatted, device.name or name, details)
    finally:
        if device is not None:
            device.close()


def confirm_match(pin):
    # This UI runs only for a new binding. Default to No; no code goes to logs.
    question = f"请核对 AI Passport 屏幕上的配对码是否为 {pin}。\n相同时选择“是”，并在设备上短按确认。"
    result = ctypes.windll.user32.MessageBoxTimeoutW(
        None, question, "AI Passport 蓝牙绑定", 0x00040000 | 0x100 | 0x24, 0, 25000
    )
    return result == 6


async def pair_passport(client):
    if sys.platform != "win32":
        await client.pair()
        return
    from winrt.windows.devices.bluetooth import BluetoothLEDevice
    from winrt.windows.devices.enumeration import (
        DevicePairingKinds as Kind, DevicePairingProtectionLevel as Level,
        DevicePairingResultStatus as Status,
    )
    device = await BluetoothLEDevice.from_bluetooth_address_async(int(client.address.replace(":", ""), 16))
    if device is None:
        raise ConnectionError("Windows could not resolve the connected device")
    try:
        info = device.device_information.pairing
        if info.is_paired:
            return
        loop = asyncio.get_running_loop()
        custom = info.custom

        async def ask(args, deferral):
            try:
                if await asyncio.to_thread(confirm_match, args.pin):
                    args.accept()
            finally:
                deferral.complete()

        def requested(_, args):
            if args.pairing_kind == Kind.CONFIRM_PIN_MATCH:
                task = ask(args, args.get_deferral())
                asyncio.run_coroutine_threadsafe(task, loop)

        token = custom.add_pairing_requested(requested)
        try:
            result = await custom.pair_with_protection_level_async(
                Kind.CONFIRM_PIN_MATCH, Level.ENCRYPTION_AND_AUTHENTICATION
            )
            if result.status not in (Status.PAIRED, Status.ALREADY_PAIRED):
                raise PairingDeclined(f"Windows numeric comparison: {result.status.name}")
        finally:
            custom.remove_pairing_requested(token)
    finally:
        device.close()
