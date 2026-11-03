import asyncio
import json
import os
from pathlib import Path
import shutil
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness/bridge"))
from harness_ble_bridge import StatusModel, HarnessDatagram
from whale_v2 import Link, Workspace, pack


class ArchiveProjection(unittest.TestCase):
    def test_reordered_updates_and_explicit_restore(self):
        m = Workspace()
        state = dict(sessionId="a", runId="1", sentAt=10, visibleAt=10, title="同名")
        m.update(state)
        original = m.keys["a"]
        m.question(
            dict(**state, question="选择", options=["甲", "乙"], requestId="q"),
            ("127.0.0.1", 1),
        )
        identity = next(iter(m.questions))
        m.set_visibility("a", True, 11)
        m.set_visibility("a", True, 11)
        m.update(state)
        self.assertNotIn("a", m.keys)
        wire = Link(m)
        wire.ready = True
        self.assertEqual(wire.receive(0x22, identity + bytes(2))[0][1][20], 2)
        m.set_visibility("a", False, 13)
        m.update(state)
        self.assertFalse(m.sessions)
        m.update(dict(state, sentAt=15, visibleAt=15))
        self.assertNotEqual(original, m.keys["a"])
        m.set_visibility("a", True, 12)
        self.assertIn("a", m.keys)

    def test_last_window_and_empty_snapshot(self):
        m = Workspace()
        wire = Link(m)
        wire.ready = True
        for i in range(17):
            m.update(dict(sessionId=str(i), runId="r", sentAt=1, visibleAt=1))
        wire.snapshot(16, 8)
        m.set_visibility("16", True, 2)
        wire.changes()
        self.assertEqual((wire.cursor, len(wire.rows)), (8, 8))
        for i in range(16):
            m.set_visibility(str(i), True, 2)
        frames = wire.changes()
        self.assertEqual((wire.cursor, len(wire.rows)), (0, 0))
        reconnect = Link(m)
        reconnect.ready = True
        self.assertEqual(len(reconnect.receive(0x10, pack("QIBB", 0, 0, 8, 0))), 2)


class HostLifecycle(unittest.IsolatedAsyncioTestCase):
    async def test_registry_lifecycle_through_plugin_and_udp(self):
        model = StatusModel()
        m = model.workspace
        loop = asyncio.get_running_loop()
        udp, _ = await loop.create_datagram_endpoint(
            lambda: HarnessDatagram(model), local_addr=("127.0.0.1", 0)
        )
        env = {
            **os.environ,
            "HARNESS_WHALE_UDP_PORT": str(udp.get_extra_info("sockname")[1]),
            "HARNESS_WHALE_TELEMETRY_PORT": "0",
            "HARNESS_WHALE_BRIDGE_SCRIPT": "missing-fixture",
        }
        p = await asyncio.create_subprocess_exec(
            shutil.which("node"),
            Path(__file__).with_name("replay_lifecycle.mjs"),
            env=env,
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
        )

        async def send(*args):
            p.stdin.write((json.dumps(args) + "\n").encode())
            await p.stdin.drain()
            while line := await asyncio.wait_for(p.stdout.readline(), 2):
                if line.strip() == b"ack":
                    return
            self.fail("fixture closed")

        async def wait(check):
            async def poll():
                while not check():
                    await asyncio.sleep(0.03)

            await asyncio.wait_for(poll(), 5)

        try:
            await wait(lambda: "startup" in m.keys)
            self.assertEqual(set(m.keys), {"startup"})
            await send("create", "new", "")
            await wait(lambda: "new" in m.keys)
            sid = m.keys["new"]
            await send("rename", "new", "同名")
            await wait(lambda: m.sessions[sid]["title"] == "同名")
            await send("archive", "startup")
            # No further session event; heartbeat must notice archive by itself.
            await wait(lambda: "startup" not in m.keys)
            await send("late", "startup")
            await send("late", "child")
            await asyncio.sleep(0.1)
            self.assertEqual(set(m.keys), {"new"})
            await send("restore", "startup")
            await wait(lambda: "startup" in m.keys)
            await send("fault", "", True)
            await send("unload", "new")
            await asyncio.sleep(0.15)
            self.assertIn("new", m.keys)
            await send("fault", "", False)
            await asyncio.sleep(3.15)
            self.assertIn("new", m.keys)  # persisted despite handle disposal
            await send("delete", "new")
            await wait(lambda: "new" not in m.keys)
            await send("late", "new")
            await asyncio.sleep(0.15)
            self.assertEqual(set(m.keys), {"startup"})
            await send("create", "other", "同名")
            await wait(lambda: "other" in m.keys)
            self.assertNotEqual(sid, m.keys["other"])
        finally:
            if p.returncode is None:
                p.stdin.write(b'["exit"]\n')
                await p.stdin.drain()
                p.stdin.close()
                await asyncio.wait_for(p.wait(), 3)
            udp.close()
