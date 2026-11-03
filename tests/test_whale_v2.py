import asyncio
import os
from pathlib import Path
import shutil
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness/bridge"))
import whale_v2 as v
from harness_ble_bridge import HarnessDatagram, StatusModel


class Wire(unittest.TestCase):
    def test_boundaries_and_crc(self):
        m = v.Workspace()
        for i in range(10):
            m.update(
                dict(
                    sessionId=str(i),
                    runId="1",
                    state="tool",
                    title="蓝" * 30,
                    todoDone=0,
                    todoTotal=3,
                )
            )
        w = v.Link(m)
        frames = w.snapshot()
        self.assertEqual([x[0] for x in frames], [17] + [18] * 8 + [19])
        self.assertEqual(len(frames[1][1]), 109)
        raw = frames[0][1] + b"".join(b[13:] for _, b in frames[1:-1])
        self.assertEqual(zlib.crc32(raw), int.from_bytes(frames[-1][1][-4:], "little"))
        modified = bytearray(raw)
        modified[24] ^= 1
        self.assertNotEqual(zlib.crc32(modified), zlib.crc32(raw))
        self.assertEqual(len(w.snapshot(8)), 4)

    def test_fragment_replay_and_interleaving(self):
        a = list(v.fragments(0x21, 65535, b"x" * 802))
        self.assertEqual(len(a), 67)
        r = v.Receiver()
        self.assertIsNone(r.accept(a[0]))
        self.assertIsNone(r.accept(a[0]))
        b = list(v.fragments(0x23, 1, b"y" * 23))
        self.assertIsNone(r.accept(b[0]))
        self.assertEqual(r.accept(b[1]), (0x23, b"y" * 23))
        for f in a[1:-1]:
            self.assertIsNone(r.accept(f))
        self.assertEqual(r.accept(a[-1]), (0x21, b"x" * 802))
        self.assertIsNone(r.accept(a[-1]))

    def test_old_run_never_applies_to_new_question(self):
        m = v.Workspace()
        ev = dict(sessionId="a", runId="1", state="thinking")
        m.update(ev)
        q = {**ev, "options": ["a", "b"], "question": "选择", "requestId": "before"}
        m.question(q, ("127.0.0.1", 12345))
        p = next(iter(m.questions))
        m.update({**ev, "runId": "2"})
        w = v.Link(m)
        w.ready = True
        self.assertEqual(w.receive(0x22, p + b"\0\0")[0][1][20], 2)


class Integration(unittest.IsolatedAsyncioTestCase):
    async def test_two_concurrent_sessions_real_plugin_udp(self):
        model = StatusModel()
        loop = asyncio.get_running_loop()
        transport, _ = await loop.create_datagram_endpoint(
            lambda: HarnessDatagram(model), local_addr=("127.0.0.1", 0)
        )
        telemetry, _ = await loop.create_datagram_endpoint(
            asyncio.DatagramProtocol, local_addr=("127.0.0.1", 0)
        )
        port = telemetry.get_extra_info("sockname")[1]
        telemetry.close()
        env = {
            **os.environ,
            "HARNESS_WHALE_UDP_PORT": str(transport.get_extra_info("sockname")[1]),
            "HARNESS_WHALE_TELEMETRY_PORT": str(port),
            "HARNESS_WHALE_BRIDGE_SCRIPT": "missing-replay-fixture",
        }
        node = shutil.which("node")
        if not node:
            self.skipTest("Node with TypeScript stripping required")
        proc = await asyncio.create_subprocess_exec(
            node,
            Path(__file__).with_name("replay_plugin.mjs"),
            env=env,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        try:
            for _ in range(100):
                if len(model.workspace.questions) == 2:
                    break
                await asyncio.sleep(0.03)
            self.assertEqual(len(model.workspace.questions), 2)
            m = model.workspace
            link = v.Link(m)
            link.receive(2, v.pack("BBIHHB", 2, 2, 127, 20, 20, 0))
            self.assertTrue(link.ready)
            frames = link.receive(0x10, v.pack("QIBB", 0, 0, 8, 0))
            self.assertEqual(len(frames), 4)
            sid = m.keys["room-a"]
            p = next(p for p in m.questions if v.IDENTITY.unpack(p)[1] == sid)
            meta = link.receive(0x24, p + b"\xff")[0][1]
            self.assertEqual(meta[20:22], bytes([5, 2]))
            page = link.receive(0x24, p + b"\x01")[0][1]
            self.assertEqual(page[20:22], bytes([1, 2]))
            self.assertEqual(link.receive(0x22, p + b"\x04\0")[0][1][20], 4)
            link.receive(0x22, p + b"\0\0")
            stdout, stderr = await asyncio.wait_for(proc.communicate(), 8)
            self.assertEqual(
                proc.returncode, 0, (stdout + stderr).decode(errors="replace")
            )
            self.assertEqual(link.receive(0x25, p)[0][1][20:], bytes([0, 4, 0]))
            other = next(q for key, q in m.questions.items() if key != p)
            self.assertEqual(other.status, 1)
        finally:
            if proc.returncode is None:
                proc.kill()
                await proc.wait()
            transport.close()
