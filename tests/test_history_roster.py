import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import zstandard

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness/bridge"))
import harness_ble_bridge as bridge
from whale_v2 import Link, pack


def event(kind, **data):
    return {"type": kind, "data": data}


def assistant(turn, text, *extra):
    blocks = [{"type": "text", "text": text}, *extra]
    return event("assistant/message", turn=turn, message={"role": "assistant", "content": blocks})


def end(turn, reason="completed"):
    return event("turn/end", turn=turn, reason={"kind": reason})


class HistoryRoster(unittest.IsolatedAsyncioTestCase):
    async def test_only_latest_completed_reply_is_sent(self):
        self.registry(["a"])
        latest = "latest answer\n" * 60
        self.journal("a", [assistant(1, "first old answer"), end(1),
                           assistant(2, "second old answer"), end(2),
                           assistant(3, latest), end(3)])
        await self.sync()
        sid = self.workspace.keys["a"]
        row = self.workspace.sessions[sid]
        wire = Link(self.workspace)
        wire.receive(2, pack("BBIHHB", 2, 2, 511, 20, 20, 0))
        parts = self.workspace.history_pages[sid]
        transmitted = b"".join(wire.receive(0x1A, pack("QIII", self.workspace.epoch,
            sid, row["_reply"], i))[0][1][26:] for i in range(len(parts)))
        self.assertEqual(transmitted.decode(), latest.strip())
        self.assertNotIn(b"old answer", transmitted)

    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        (self.root / "storages").mkdir()
        self.model = bridge.StatusModel()
        self.workspace = self.model.workspace
        self.retries = {}

    def registry(self, ids, archived=()):
        store = {"unit": {"name": "workspace", "version": 2},
                 "global": {"initialized": True, "workspaceIds": ["w"], "archivedSessionIds": list(archived)},
                 "tables": {"workspaces": {"w": {"sessionIds": ids}}}}
        (self.root / "storages/workspace.json").write_text(json.dumps(store))

    def journal(self, key, records, **header):
        path = self.root / "sessions/local" / key / "session.v4.jsonl.zstd"
        path.parent.mkdir(parents=True, exist_ok=True)
        rows = [{"type": "session", "id": key, "version": 4, **header}, *records]
        compress = zstandard.ZstdCompressor().compress
        path.write_bytes(b"".join(compress(json.dumps(row).encode() + b"\n") for row in rows))
        return path

    async def sync(self):
        await bridge.sync_workspace(self.model, self.root, self.retries)

    async def test_reboot_restores_quiet_session_final_reply_and_archive_lifecycle(self):
        keys = ["active", "quiet", "archived", "helper", "delegated", "missing", "mismatch", "../unsafe"]
        self.registry(keys, ["archived"])
        self.journal("helper", [], origin="subagent")
        self.journal("delegated", [], delegationDepth=1)
        self.journal("mismatch", [], id="someone-else")
        self.journal("archived", [])
        reply = "第一段。\n" + "🐋长回复。" * 1200
        self.journal("quiet", [event("session/title", title="离线会话"),
            assistant(1, reply, {"type": "reasoning", "text": "private"}), end(1),
            assistant(2, "draft", {"type": "tool-call"}), end(2),
            assistant(3, "incomplete"), end(3, "interrupted")])
        self.workspace.update(dict(sessionId="active", runId="live", title="在线", state="thinking"))
        await self.sync()
        self.assertEqual(set(self.workspace.keys), {"quiet", "active"})
        wire = Link(self.workspace)
        wire.receive(2, pack("BBIHHB", 2, 2, 511, 20, 20, 0))
        wire.snapshot()
        self.assertEqual(wire.snapshot_receipt["total"], 2)
        sid = self.workspace.keys["quiet"]
        row = self.workspace.sessions[sid]
        self.assertEqual((row["title"], row["state"]), ("离线会话", "error"))
        rev = row["_reply"]
        def read(page):
            return wire.receive(0x1A, pack("QIII", self.workspace.epoch, sid, rev, page))[0]
        self.workspace.update(dict(sessionId="quiet", runId="new", state="thinking", sentAt=20), ("127.0.0.1", 1))
        count = struct.unpack_from("<I", read(0)[1], 20)[0]
        self.assertGreater(count, 32)
        self.assertEqual("".join(read(i)[1][26:].decode() for i in range(count)), reply)
        self.assertEqual(read(count)[0], 0x32)
        self.workspace.set_visibility("quiet", True, 30)
        self.registry(keys, ["archived", "quiet"])
        await self.sync()
        self.assertEqual(set(self.workspace.keys), {"active"})
        self.assertEqual(read(0)[0], 0x32)
        self.registry(keys, ["archived"])
        await self.sync()
        self.assertNotEqual(self.workspace.keys["quiet"], sid)
        self.assertEqual(set(self.workspace.keys), {"active", "quiet"})

    async def test_live_data_wins_and_new_final_invalidates_stored_pages(self):
        self.registry(["a"])
        self.journal("a", [event("session/title", title="disk"), assistant(1, "old reply"), end(1)])
        result = bridge.load_history(self.root / "sessions", "a", None)[1]
        live = dict(sessionId="a", runId="now", state="thinking", title="live", sentAt=2)
        self.workspace.update(live)
        await self.sync()
        sid = self.workspace.keys["a"]
        row = self.workspace.sessions[sid]
        self.assertEqual((row["state"], row["title"], row["replyPreview"]), ("thinking", "live", "old reply"))
        old_rev = row["_reply"]
        self.workspace.update(dict(live, replyId="fresh", replyPreview="new", sentAt=3))
        self.workspace.restore_history("a", *result)
        self.assertNotIn(sid, self.workspace.history_pages)
        row = self.workspace.sessions[sid]
        self.assertEqual(row["replyId"], "fresh")
        self.assertNotEqual(row["_reply"], old_rev)

    async def test_archive_during_restore_and_non_inherited_events_only(self):
        self.registry(["a"])
        self.journal("a", [assistant(1, "inherited"), end(1), event("session/end-seed")], isSeeded=True)
        loader = bridge.load_history
        def archive(*args):
            result = loader(*args)
            self.registry(["a"], ["a"])
            return result
        with patch.object(bridge, "load_history", archive):
            await self.sync()
        self.assertFalse(self.workspace.keys)
        await self.sync()
        self.registry(["a"])
        await self.sync()
        row = self.workspace.sessions[self.workspace.keys["a"]]
        self.assertFalse(row["_reply"])

    async def test_new_session_and_retry_after_partial_journal(self):
        self.registry([])
        await self.sync()
        self.registry(["new"])
        path = self.journal("new", [])
        path.write_bytes(b"partial")
        await self.sync()
        self.assertFalse(self.workspace.keys)
        self.journal("new", [])
        self.retries.clear()
        await self.sync()
        sid = self.workspace.keys["new"]
        self.journal("new", [event("session/title", title="new title"), assistant(1, "final"), end(1)])
        self.retries["new"] = 0, self.retries["new"][1]
        await self.sync()
        self.assertEqual(self.workspace.sessions[sid]["title"], "new title")
        self.assertEqual(self.workspace.sessions[sid]["replyPreview"], "final")
        self.registry([])
        await self.sync()
        self.assertFalse(self.workspace.sessions)
