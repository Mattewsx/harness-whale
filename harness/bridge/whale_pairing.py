"""Windows numeric comparison for the authenticated Passport service."""
import asyncio
import ctypes
import sys


class PairingDeclined(ConnectionError):
    pass


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
