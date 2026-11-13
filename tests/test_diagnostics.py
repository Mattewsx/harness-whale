import asyncio
import json
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness/bridge"))
import harness_ble_bridge as bridge
import whale_v2 as v


class LinkDiagnostics(unittest.IsolatedAsyncioTestCase):
    async def test_packet_counter_tracks_actual_completed_writes(self):
        model = bridge.StatusModel()
        model.workspace.update(
            {"sessionId": "a", "runId": "r", "state": "thinking", "title": "test"}
        )
        seen = asyncio.Event()

        class Client:
            is_connected = True
            writes = 0
            mtu_size = 23
            services = SimpleNamespace(get_service=lambda _: True)

            async def __aenter__(self):
                return self

            async def __aexit__(self, *exc):
                self.is_connected = False

            async def start_notify(self, _, callback):
                self.notify = callback

            async def write_gatt_char(self, _, data, response):
                self.writes += 1
                if data[1] == 1 and data[4] == 0:
                    for kind, body, serial in [
                        (2, v.pack("BBIHHB", 2, 2, 511, 20, 20, 0), 1),
                        (0x10, v.pack("QIBB", 0, 0, 8, 0), 2),
                    ]:
                        for frame in v.fragments(kind, serial, body):
                            self.notify(None, frame)

        client = Client()

        class Telemetry:
            def sendto(self, data, addr):
                msg = json.loads(data)
                if msg["state"] == "v2.2":
                    self.assertion(msg)

            def assertion(_, msg):
                self.assertEqual(msg["packetsSent"], client.writes)
                self.assertGreater(client.writes, 1)
                seen.set()

        fake = SimpleNamespace(BleakScanner=None, BleakClient=lambda _, **options: client)

        async def paired(_):
            pass

        async def cached_peer(*_):
            return None

        with patch.dict(sys.modules, bleak=fake), patch.object(
            bridge, "pair_passport", paired
        ), patch.object(bridge, "paired_passport_device", cached_peer):
            t = asyncio.create_task(
                bridge.ble_loop(model, "fixture", "fixture", 0.01, Telemetry(), 0)
            )
            try:
                await asyncio.wait_for(seen.wait(), 4)
            finally:
                t.cancel()
                await asyncio.gather(t, return_exceptions=True)

class HandshakeDeadline(unittest.IsolatedAsyncioTestCase):
    async def test_lost_first_hello_ack_retries_then_stops_when_ready(self):
        ready = asyncio.Event()
        sequences = []

        class Client:
            is_connected = True
            services = SimpleNamespace(get_characteristic=lambda _: None)

            async def start_notify(self, _, callback):
                self.callback = callback

            async def stop_notify(self, _):
                pass

            async def write_gatt_char(self, _, frame, response):
                if frame[1] != 1:
                    return
                sequences.append(int.from_bytes(frame[2:4], "little"))
                if len(sequences) == 1:
                    return  # peer accepted the HELLO, but its first ACK was lost
                for kind, body, serial in [(2, v.pack("BBIHHB", 2, 2, 63, 20, 20, 0), 1),
                                           (0x10, v.pack("QIBB", 0, 0, 8, 0), 2)]:
                    for packet in v.fragments(kind, serial, body):
                        self.callback(None, packet)

        def written(is_ready):
            if is_ready:
                ready.set()

        with patch.object(v, "HELLO_RETRY_SECONDS", 0.05), patch.object(v, "HELLO_TIMEOUT_SECONDS", 0.5):
            task = asyncio.create_task(v.serve(Client(), v.Workspace(), written))
            try:
                await asyncio.wait_for(ready.wait(), 1)
                await asyncio.sleep(0.15)
                self.assertEqual(sequences, [1, 2])
            finally:
                task.cancel()
                await asyncio.gather(task, return_exceptions=True)

    async def test_connected_without_hello_yields_to_reconnect(self):
        class SilentClient:
            is_connected = True
            async def start_notify(self, _, callback):
                pass
            async def write_gatt_char(self, uuid, frame, response):
                pass
        with patch.object(v, "HELLO_TIMEOUT_SECONDS", 0.05):
            with self.assertRaisesRegex(ConnectionError, "handshake timed out"):
                await asyncio.wait_for(v.serve(SilentClient(), v.Workspace()), 1)
