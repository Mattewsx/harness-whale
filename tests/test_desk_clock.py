import asyncio
import struct
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness/bridge"))
import whale_v2 as v


def ready_link(model):
    link = v.Link(model)
    link.receive(2, v.pack("BBIHHB", 2, 2, v.CAPS, 20, 20, 0))
    return link


class DeskClock(unittest.TestCase):
    def test_clock_waits_for_snapshot_then_resyncs(self):
        m = v.Workspace()
        link = v.Link(m)
        self.assertEqual(link.clock(), [])  # no handshake yet
        link = ready_link(m)
        self.assertEqual(link.clock(), [])  # device is not ready before a snapshot
        link.snapshot()
        (kind, body), = link.clock(now=1793975400.7, tz_minutes=480)
        self.assertEqual(kind, 0x53)
        self.assertNotIn(kind, v.CONTROL)  # bulk lane: old firmware drops it whole
        self.assertEqual(struct.unpack("<QqH", body), (m.epoch, 1793975400, 480))
        self.assertEqual(link.clock(), [])  # not again until the resync interval
        with patch.object(v.time, "monotonic", return_value=time.monotonic() + 1801):
            (kind, body), = link.clock(now=1793977200, tz_minutes=-300)
        self.assertEqual(struct.unpack("<Qqh", body)[2], -300)
        # A new handshake (reconnect) sends the clock again at once.
        link.receive(2, v.pack("BBIHHB", 2, 2, v.CAPS, 20, 20, 0))
        self.assertEqual(len(link.clock()), 1)

    def test_default_offset_comes_from_local_time(self):
        link = ready_link(v.Workspace())
        link.snapshot()
        (_, body), = link.clock(now=1793975400)
        offset = struct.unpack("<QqH", body)[2]
        expected = (time.localtime(1793975400).tm_gmtoff or 0) // 60
        self.assertEqual(struct.unpack("<h", struct.pack("<H", offset))[0], expected)


class DeskClockServe(unittest.IsolatedAsyncioTestCase):
    async def test_serve_writes_clock_after_snapshot(self):
        model = v.Workspace()
        model.update({"sessionId": "a", "runId": "r", "state": "idle", "title": "t"})
        written = []
        done = asyncio.Event()

        class Client:
            is_connected = True

            async def start_notify(self, _, callback):
                self.notify = callback

            async def write_gatt_char(self, _, data, response):
                written.append(bytes(data))
                if data[1] == 1 and data[4] == 0:
                    for kind, body, serial in [
                        (2, v.pack("BBIHHB", 2, 2, v.CAPS, 20, 20, 0), 1),
                        (0x10, v.pack("QIBB", 0, 0, 8, 0), 2),
                    ]:
                        for frame in v.fragments(kind, serial, body):
                            self.notify(None, frame)
                if data[1] == 0x53 and data[4] + 1 == data[5]:
                    done.set()

        with tempfile.TemporaryDirectory() as tmp:
            model.marks_path = Path(tmp) / "marks.json"
            task = asyncio.create_task(v.serve(Client(), model))
            try:
                await asyncio.wait_for(done.wait(), 4)
            finally:
                task.cancel()
                await asyncio.gather(task, return_exceptions=True)
        kinds = [frame[1] for frame in written if frame[4] == 0]
        self.assertLess(kinds.index(0x13), kinds.index(0x53))
        self.assertEqual(kinds.count(0x53), 1)


if __name__ == "__main__":
    unittest.main()
