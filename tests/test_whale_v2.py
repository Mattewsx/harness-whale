import asyncio
import json
import os
from pathlib import Path
import shutil
import struct
import sys
from types import SimpleNamespace
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness/bridge"))
import whale_v2 as v
from harness_ble_bridge import HarnessDatagram, StatusModel


class LargeFrames(unittest.TestCase):
    def test_valid_hello_reopens_logical_session_without_physical_disconnect(self):
        receiver = v.Receiver()
        receiver.frame = 244
        self.assertIsNone(receiver.accept(next(v.fragments(0x14, 44, b"x" * 300, 244))))
        control = next(v.fragments(0x23, 90, b"x"))
        self.assertEqual(receiver.accept(control), (0x23, b"x"))
        malformed = next(v.fragments(1, 1, v.pack("BBIHH", 3, 2, 63, 244, 244)))
        self.assertIsNone(receiver.accept(malformed))
        self.assertEqual(receiver.frame, 244)
        self.assertIn(False, receiver.active)
        handshake = next(v.fragments(1, 1, v.hello(244)[1]))
        self.assertEqual(receiver.accept(handshake), v.hello(244))
        self.assertEqual(receiver.frame, 0)
        self.assertNotIn(False, receiver.active)
        self.assertEqual(receiver.accept(handshake), v.hello(244))
        self.assertEqual(receiver.accept(control), (0x23, b"x"))
        self.assertIsNone(receiver.accept(control))

    def test_version3_fragments_and_receiver_grant(self):
        body = bytes(range(256)) * 2
        legacy = list(v.fragments(0x1B, 5, body))
        large = list(v.fragments(0x1B, 5, body, 244))
        self.assertEqual((len(legacy), len(large)), (43, 3))
        self.assertTrue(all(f[0] == 2 and len(f) <= 20 for f in legacy))
        self.assertTrue(all(f[0] == 3 and len(f) <= 244 for f in large))
        r = v.Receiver()
        self.assertIsNone(r.accept(large[0]))  # no grant: version 3 is rejected
        r.frame = 244
        for f in large[:-1]:
            self.assertIsNone(r.accept(f))
        self.assertEqual(r.accept(large[-1]), (0x1B, body))
        for f in list(v.fragments(0x1B, 6, body))[:-1]:  # 2.2 frames stay valid
            self.assertIsNone(r.accept(f))
        self.assertEqual(r.accept(legacy[-1][:2] + b"\x06" + legacy[-1][3:]), (0x1B, body))
        r.frame = 200  # a message must finish at the frame size it started with
        mixed = list(v.fragments(0x1B, 7, body, 244))
        self.assertIsNone(r.accept(mixed[0]))
        self.assertIsNone(r.accept(list(v.fragments(0x1B, 7, body, 200))[1]))
        with self.assertRaises(ValueError):
            list(v.fragments(0x1B, 1, body, 245))
        self.assertEqual([v.offered_frame(m) for m in (None, 23, 100, 247, 517)], [20, 20, 97, 244, 244])

    def test_hello_and_grant_negotiation(self):
        kind, body = v.hello(244)
        self.assertEqual(struct.unpack("<BBIHH", body), (2, 2, v.CAPS | v.LARGE_FRAMES, 244, 244))
        self.assertEqual(struct.unpack("<BBIHH", v.hello()[1]), (2, 2, v.CAPS, 20, 20))
        cases = [
            ((2, 3, v.CAPS | v.LARGE_FRAMES, 244, 244, 0), True, 244),
            ((2, 3, v.CAPS | v.LARGE_FRAMES, 180, 180, 0), True, 180),
            ((2, 2, v.CAPS, 20, 20, 0), True, 20),  # v1.3.1 firmware
            ((2, 3, v.CAPS, 20, 20, 0), True, 20),  # 2.3 firmware at MTU 23
            ((2, 3, v.CAPS | v.LARGE_FRAMES, 512, 512, 0), True, 20),  # larger than offered
            ((2, 3, v.CAPS | v.LARGE_FRAMES, 244, 200, 0), True, 20),  # asymmetric
            ((2, 4, v.CAPS, 20, 20, 0), False, 20),
            ((2, 3, v.CAPS | v.LARGE_FRAMES, 244, 244, 1), False, 20),
        ]
        for reply, ready, frame in cases:
            with self.subTest(reply=reply):
                link = v.Link(v.Workspace())
                link.offered = 244
                link.receive(2, v.pack("BBIHHB", *reply))
                self.assertEqual((link.ready, link.frame), (ready, frame))


class LargeFrameServe(unittest.IsolatedAsyncioTestCase):
    async def run_device(self, mtu, grant):
        model = v.Workspace()
        model.marks_path = Path(os.devnull)
        model.update(dict(sessionId="a", runId="r", state="done", title="蓝" * 40,
                          replyId="x", replyPreview="预览"))
        writes, done = [], asyncio.Event()

        class Client:
            is_connected = True
            mtu_size = mtu
            services = SimpleNamespace(get_characteristic=lambda _: None)

            async def start_notify(self, _, callback):
                self.notify = callback

            async def write_gatt_char(self, _, data, response):
                writes.append(bytes(data))
                if data[1] == 1:
                    hello = struct.unpack("<BBIHH", data[8:])
                    caps, offer = hello[2], hello[3]
                    frame = min(offer, 244) if grant and caps & v.LARGE_FRAMES else 20
                    caps = (caps & 4095) | (v.LARGE_FRAMES if frame > 20 else 0)
                    reply = v.pack("BBIHHB", 2, 3 if grant else 2, caps, frame, frame, 0)
                    for f in v.fragments(2, 1, reply):
                        self.notify(None, f)
                    for f in v.fragments(0x10, 2, v.pack("QIBB", 0, 0, 8, 0), frame):
                        self.notify(None, f)
                if data[1] == 0x13:
                    done.set()

        task = asyncio.create_task(v.serve(Client(), model))
        try:
            await asyncio.wait_for(done.wait(), 4)
            frame = model.frame
        finally:
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
        return writes, frame

    async def test_bridge_switches_after_grant(self):
        writes, frame = await self.run_device(247, True)
        self.assertEqual(frame, 244)
        self.assertEqual((writes[0][0], len(writes[0])), (2, 18))  # hello stays 2.2
        self.assertTrue(all(w[0] == 3 and len(w) <= 244 for w in writes[1:]))
        self.assertTrue(any(len(w) > 20 for w in writes[1:]))

    async def test_old_firmware_keeps_legacy_frames(self):
        writes, frame = await self.run_device(247, False)
        self.assertEqual(frame, 20)
        self.assertTrue(all(w[0] == 2 and len(w) <= 20 for w in writes))

    async def test_low_mtu_does_not_offer_large_frames(self):
        writes, frame = await self.run_device(23, True)
        self.assertEqual(frame, 20)
        caps = struct.unpack("<BBIHH", writes[0][8:])[2]
        self.assertFalse(caps & v.LARGE_FRAMES)
        self.assertTrue(all(w[0] == 2 and len(w) <= 20 for w in writes))


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


    def rich_link(self, m, caps):
        w = v.Link(m)
        w.receive(2, v.pack("BBIHHB", 2, 2, caps, 20, 20, 0))
        self.assertTrue(w.ready)
        return w

    def test_rich_questions_are_hidden_from_old_firmware(self):
        m = v.Workspace()
        ev = dict(sessionId="a", runId="1", state="waiting", hint=2)
        m.update(ev)
        m.question({**ev, "options": ["允许", "拒绝"], "question": "允许运行命令？ ls",
                    "requestId": "ap", "mode": "approval"}, ("127.0.0.1", 1))
        p = next(iter(m.questions))
        sid = m.keys["a"]
        old = self.rich_link(m, 4095)
        self.assertEqual(v.ROW.unpack_from(m.row(sid, rich=old.rich))[8], 0)
        self.assertNotEqual(v.ROW.unpack_from(m.row(sid, rich=True))[8], 0)
        self.assertEqual(old.receive(0x24, p + b"\xff")[0][1][20], 3)
        new = self.rich_link(m, 4095 | v.RICH_QUESTIONS)
        meta = new.receive(0x24, p + b"\xff")[0][1]
        self.assertEqual(meta[-1], 2)
        self.assertEqual(v.struct.unpack_from("<H", meta, 22)[0], len(meta) - 25)

    def test_approval_needs_two_labels_and_known_mode(self):
        m = v.Workspace()
        ev = dict(sessionId="a", runId="1", state="waiting")
        m.update(ev)
        m.question({**ev, "options": ["a", "b", "c"], "question": "q", "requestId": "x", "mode": "approval"}, None)
        m.question({**ev, "options": ["a", "b"], "question": "q", "requestId": "y", "mode": "other"}, None)
        self.assertFalse(m.questions)

    def test_multi_select_mask_submit_and_query(self):
        m = v.Workspace()
        sent = []

        class T:
            def sendto(self, data, addr):
                sent.append(json.loads(data))

        m.transport = T()
        ev = dict(sessionId="a", runId="1", state="question")
        m.update(ev)
        m.question({**ev, "options": ["a", "b", "c", "d"], "question": "多选",
                    "requestId": "mq", "mode": "multi"}, ("127.0.0.1", 1))
        p = next(iter(m.questions))
        w = self.rich_link(m, 4095 | v.RICH_QUESTIONS)
        self.assertEqual(w.receive(0x24, p + b"\xff")[0][1][-1], 1)
        # Single submit, an empty mask and out-of-range bits are all ignored.
        w.receive(0x22, p + b"\0\0")
        w.receive(0x26, p + v.pack("Q", 0))
        w.receive(0x26, p + v.pack("Q", 1 << 4))
        self.assertEqual(sent, [])
        self.assertEqual(w.receive(0x26, p + v.pack("Q", 0b101))[0][1][20], 4)
        self.assertEqual(sent[-1]["selectedList"], [0, 2])
        w.receive(0x26, p + v.pack("Q", 0b1))
        self.assertEqual(len(sent), 1)
        w.receive(0x25, p)
        self.assertEqual(sent[-1]["selectedList"], [0, 2])
        self.assertIn(0x26, v.CONTROL)


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

    async def test_approval_and_multi_select_real_plugin_udp(self):
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
            Path(__file__).with_name("replay_approval.mjs"),
            env=env,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        try:
            m = model.workspace
            for _ in range(100):
                if len(m.questions) == 2:
                    break
                await asyncio.sleep(0.03)
            self.assertEqual(len(m.questions), 2)
            link = v.Link(m)
            link.receive(2, v.pack("BBIHHB", 2, 2, 127 | v.RICH_QUESTIONS, 20, 20, 0))
            link.receive(0x10, v.pack("QIBB", 0, 0, 8, 0))
            ap = next(p for p, q in m.questions.items() if q.mode == 2)
            mq = next(p for p, q in m.questions.items() if q.mode == 1)
            self.assertIn("npm test", m.questions[ap].prompt.decode())
            self.assertEqual(m.questions[ap].labels, ["允许", "拒绝"])
            link.receive(0x22, ap + b"\0\0")
            link.receive(0x26, mq + v.pack("Q", 0b101))
            stdout, stderr = await asyncio.wait_for(proc.communicate(), 8)
            self.assertEqual(
                proc.returncode, 0, (stdout + stderr).decode(errors="replace")
            )
            self.assertEqual(link.receive(0x25, ap)[0][1][20:], bytes([0, 0, 0]))
            self.assertEqual(m.questions[mq].status, 0)
        finally:
            if proc.returncode is None:
                proc.kill()
                await proc.wait()
            transport.close()
