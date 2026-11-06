"""BLE OTA client against a simulated v1.4 device (protocol in whale_ota_core.h)."""

import asyncio
import hashlib
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "harness" / "bridge"))
import whale_ota as ota  # noqa: E402

PACK = "1ae15825e940e88be812ee31219c984bb03e26536d11804d4e2376c319c250bf"


def app_image(size=20000, version="whale-v1.4.0", pack=PACK, chip=5, project=b"FoloToy-AI-Passport"):
    image = bytearray((i * 7) & 0xFF for i in range(size))
    image[0] = 0xE9
    struct.pack_into("<H", image, 12, chip)
    struct.pack_into("<II8s32s32s", image, 0x20, ota.APP_DESC_MAGIC, 0, b"",
                     version.encode(), project)
    if pack:
        marker = b"WHALE-ASSET-PACK:" + pack.encode()
        image[4000:4000 + len(marker)] = marker
    return bytes(image)


class FakeDevice:
    """Python mirror of whale_ota_core.c driven through a bleak-like client."""

    def __init__(self, answer=True, drop=(), pack=PACK, max_size=0x280000, finish=0):
        self.answer, self.drop, self.max_size, self.finish = answer, set(drop), max_size, finish
        self.asset = bytes.fromhex(pack)[:8]
        self.state, self.offset, self.acked, self.nacked = ota.IDLE, 0, 0, False
        self.written = bytearray()
        self.callback = None
        self.mtu_size = 247
        self.writes = 0

    async def start_notify(self, uuid, callback):
        assert uuid == ota.OTA_UUID
        self.callback = callback

    async def stop_notify(self, uuid):
        self.callback = None

    def notify(self, payload):
        self.callback(None, bytearray(payload))

    def status(self, code=0):
        self.notify(struct.pack("<BBBI", ota.STATUS, self.state, code, self.offset))

    async def write_gatt_char(self, uuid, packet, response=True):
        await asyncio.sleep(0)
        self.writes += 1
        op = packet[0]
        if op == ota.INFO:
            v = b"whale-v1.3.9"
            self.notify(bytes([ota.INFO_REPLY, 1, ota.INFO_ASSETS | ota.INFO_ROLLBACK])
                        + struct.pack("<I", self.max_size) + self.asset + bytes([len(v)]) + v
                        + b"\x05older")
        elif op == ota.BEGIN:
            self.size = struct.unpack_from("<I", packet, 2)[0]
            self.sha = packet[6:38]
            self.state = ota.CONFIRM
            self.status()
            self.state = ota.RECEIVING if self.answer else ota.CANCELLED
            self.status(0 if self.answer else 4)
        elif op == ota.DATA:
            offset = struct.unpack_from("<I", packet, 1)[0]
            if self.state != ota.RECEIVING:
                return
            if offset in self.drop:  # write without response lost in a full queue
                self.drop.discard(offset)
                return
            if offset != self.offset:
                if not self.nacked:
                    self.nacked = True
                    self.status(8)
                return
            self.nacked = False
            self.written += packet[5:]
            self.offset += len(packet) - 5
            if self.offset - self.acked >= ota.ACK_BYTES or self.offset == self.size:
                self.acked = self.offset
                self.status()
        elif op == ota.END:
            ok = hashlib.sha256(self.written).digest() == self.sha and not self.finish
            self.state = ota.DONE if ok else ota.FAILED
            self.status(0 if ok else (self.finish or 6))
        elif op == ota.ABORT:
            self.state = ota.CANCELLED
            self.status()
        elif op == ota.ROLLBACK:
            self.state = ota.ROLLBACK_CONFIRM
            self.status()
            self.state = ota.DONE if self.answer else ota.CANCELLED
            self.status(0 if self.answer else 4)


def run(coro):
    return asyncio.run(coro)


async def push(device, image, phases=None):
    async with ota.Session(device) as session:
        return await session.push(ota.parse_image(image),
                                  lambda p, d, t: phases.append((p, d)) if phases is not None else None)


class ImageTests(unittest.TestCase):
    def test_application_image_is_parsed(self):
        image = ota.parse_image(app_image())
        self.assertEqual(image.version, "whale-v1.4.0")
        self.assertEqual(image.asset_pack, PACK)
        self.assertEqual(image.asset_id, bytes.fromhex(PACK)[:8])

    def test_full_image_wrong_chip_project_and_old_firmware_are_refused(self):
        with self.assertRaisesRegex(ota.OtaError, "USB-only"):
            ota.parse_image(b"\xff" * 0x9000)
        with self.assertRaisesRegex(ota.OtaError, "ESP32-C3"):
            ota.parse_image(app_image(chip=9))
        with self.assertRaisesRegex(ota.OtaError, "not a"):
            ota.parse_image(app_image(project=b"other"))
        with self.assertRaisesRegex(ota.OtaError, "older than v1.4"):
            ota.parse_image(app_image(pack=None))

    def test_packets_match_firmware_layout(self):
        image = ota.parse_image(app_image())
        begin = ota.begin_packet(image)
        self.assertEqual(len(begin), 47 + len("whale-v1.4.0"))
        self.assertEqual(begin[46], len("whale-v1.4.0"))
        self.assertEqual(begin[38:46], image.asset_id)
        self.assertEqual(len(ota.data_packet(0, b"x" * ota.MAX_CHUNK)), 241)  # fits MTU 247


class TransferTests(unittest.TestCase):
    def test_full_transfer(self):
        image = app_image(30001)
        device, phases = FakeDevice(), []
        info = run(push(device, image, phases))
        self.assertEqual(bytes(device.written), image)
        self.assertEqual(device.state, ota.DONE)
        self.assertEqual(info.version, "whale-v1.3.9")
        self.assertEqual(info.other_version, "older")
        self.assertEqual(phases[0], ("confirm", 0))
        self.assertEqual(phases[-1], ("done", 30001))

    def test_dropped_chunk_mid_window_is_resent(self):
        image = app_image(20000)
        device = FakeDevice(drop={236 * 3})
        run(push(device, image))
        self.assertEqual(bytes(device.written), image)

    def test_dropped_window_tail_recovers_after_timeout(self):
        image = app_image(500)  # 3 chunks; the last one is lost and nothing follows
        device = FakeDevice(drop={472})
        original = ota.Session.status

        async def quick(self, timeout):
            return await original(self, min(timeout, 0.2))

        ota.Session.status = quick
        try:
            run(push(device, image))
        finally:
            ota.Session.status = original
        self.assertEqual(bytes(device.written), image)

    def test_declined_on_device(self):
        with self.assertRaisesRegex(ota.OtaError, "declined"):
            run(push(FakeDevice(answer=False), app_image()))

    def test_asset_pack_mismatch_is_refused_before_begin(self):
        device = FakeDevice(pack="00" * 32)
        with self.assertRaisesRegex(ota.OtaError, "asset pack"):
            run(push(device, app_image()))
        self.assertEqual(device.state, ota.IDLE)

    def test_too_large_for_slot(self):
        with self.assertRaisesRegex(ota.OtaError, "slot holds"):
            run(push(FakeDevice(max_size=1000), app_image()))

    def test_device_side_rejection_is_reported(self):
        with self.assertRaisesRegex(ota.OtaError, "image rejected"):
            run(push(FakeDevice(finish=7), app_image()))

    def test_rollback(self):
        async def go(device):
            async with ota.Session(device) as session:
                await session.rollback()
        run(go(FakeDevice()))
        with self.assertRaisesRegex(ota.OtaError, "declined"):
            run(go(FakeDevice(answer=False)))


class BridgeRequestTests(unittest.TestCase):
    def test_ota_request_is_queued_once_and_loopback_only(self):
        import json
        import harness_ble_bridge as bridge

        async def go():
            model = bridge.StatusModel()
            proto = bridge.HarnessDatagram(model)
            sent = []

            class Transport:
                def sendto(self, data, addr):
                    sent.append(json.loads(data))

            model.datagram_transport = Transport()
            request = json.dumps({"kind": "whale-ota", "action": "info"}).encode()
            proto.datagram_received(request, ("10.0.0.2", 5000))
            self.assertEqual(model.ota_requests.qsize(), 0)
            proto.datagram_received(request, ("127.0.0.1", 5000))
            proto.datagram_received(request, ("127.0.0.1", 5001))
            self.assertEqual(model.ota_requests.qsize(), 1)
            self.assertEqual(sent[0]["phase"], "error")

        run(go())

    def test_service_reports_info(self):
        import json
        import harness_ble_bridge as bridge

        async def go():
            model = bridge.StatusModel()
            sent = []

            class Transport:
                def sendto(self, data, addr):
                    sent.append(json.loads(data))

            class Services:
                def get_characteristic(self, uuid):
                    return uuid == ota.OTA_UUID

            model.datagram_transport = Transport()
            device = FakeDevice()
            device.services = Services()
            model.ota_requests.put_nowait(({"action": "info", "_queuedAt": bridge.time.monotonic()},
                                           ("127.0.0.1", 1)))
            task = asyncio.create_task(bridge.ota_service(device, model))
            for _ in range(50):
                await asyncio.sleep(0.01)
                if sent:
                    break
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
            self.assertEqual(sent[0]["phase"], "info")
            self.assertEqual(sent[0]["version"], "whale-v1.3.9")
            self.assertTrue(sent[0]["rollback"])

        run(go())


if __name__ == "__main__":
    unittest.main()
