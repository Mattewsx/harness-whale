import json
import struct
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness/bridge"))
import whale_v2 as v

# 2026-11-09 14:00 local in a fixed UTC+8 zone (via a patched localtime offset).
NOON = 1793944800
TZ = 8 * 3600


def local(fake_offset=TZ):
    class Stamp:
        def __init__(self, t):
            self.tm_gmtoff = fake_offset

    return patch.object(v.time, "localtime", lambda t=None: Stamp(t))


def ready_link(model):
    link = v.Link(model)
    link.receive(2, v.pack("BBIHHB", 2, 2, v.CAPS, 20, 20, 0))
    return link


def run(m, key, run_id, *states, elapsed=0):
    for state in states:
        m.update({"sessionId": key, "runId": run_id, "state": state, "elapsed": elapsed})


class WorkDay(unittest.TestCase):
    def test_day_starts_at_five_local(self):
        with local():
            day = v.work_day(NOON)
            # 04:59 the next morning still belongs to today; 05:00 starts a new day.
            next_morning = NOON + 15 * 3600 - 60
            self.assertEqual(v.work_day(next_morning), day)
            self.assertEqual(v.work_day(next_morning + 60), day + 1)


class Tally(unittest.TestCase):
    def test_cumulative_totals_survive_dedup_eviction_and_restart(self):
        with local(), tempfile.TemporaryDirectory() as folder, \
                patch.object(v.time, "time", return_value=NOON):
            first = v.DailyTally()
            first.record("synthetic", "long-run", "done", 86400)
            for i in range(v.MAX_RUNS):
                first.record("synthetic", f"run-{i}", "done", 1)
            expected = (v.MAX_RUNS + 1, 86400 + v.MAX_RUNS)
            self.assertEqual(first.totals()[1:], expected)
            self.assertEqual(len(first.runs), v.MAX_RUNS)
            self.assertFalse(first.record("synthetic", f"run-{v.MAX_RUNS - 1}", "done", 1))
            path = Path(folder) / "daily-summary.json"
            first.attach(path)
            restarted = v.DailyTally()
            restarted.attach(path)
            self.assertEqual(restarted.totals()[1:], expected)
            self.assertEqual(len(restarted.runs), v.MAX_RUNS)
            self.assertFalse(restarted.record("synthetic", f"run-{v.MAX_RUNS - 1}", "done", 1))
            restarted.record("synthetic", "after-restart", "error", 60)
            restarted.attach(path)  # reattaching must not add evicted totals twice
            third = v.DailyTally()
            third.attach(path)
            self.assertEqual(third.totals()[1:], (expected[0], expected[1] + 60))

    def test_cumulative_totals_merge_with_pre_connection_runs(self):
        with local(), tempfile.TemporaryDirectory() as folder, \
                patch.object(v.time, "time", return_value=NOON), patch.object(v, "MAX_RUNS", 2):
            path = Path(folder) / "daily-summary.json"
            first = v.DailyTally()
            for i in range(3):
                first.record("synthetic", f"run-{i}", "done", 10)
            first.attach(path)
            second = v.DailyTally()
            second.record("synthetic", "run-2", "done", 10)  # retained overlap
            second.record("synthetic", "before-ble", "done", 20)
            second.attach(path)
            self.assertEqual(second.totals()[1:], (4, 50))
            self.assertEqual(len(second.runs), 2)
            self.assertFalse(second.record("synthetic", "before-ble", "done", 20))
            restarted = v.DailyTally()
            restarted.attach(path)
            self.assertEqual(restarted.totals()[1:], (4, 50))

    def test_legacy_runs_only_schema_migrates_before_cache_is_bounded(self):
        with local(), tempfile.TemporaryDirectory() as folder, \
                patch.object(v.time, "time", return_value=NOON), patch.object(v, "MAX_RUNS", 2):
            path = Path(folder) / "daily-summary.json"
            path.write_text(json.dumps({"day": v.work_day(NOON), "runs": {
                "synthetic-a": [1, 60], "synthetic-b": [0, 30], "synthetic-c": [1, 10],
            }}), encoding="utf8")
            first = v.DailyTally()
            first.record("synthetic", "before-ble", "done", 20)
            first.attach(path)
            self.assertEqual(first.totals()[1:], (3, 120))
            self.assertEqual(len(first.runs), 2)
            saved = json.loads(path.read_text(encoding="utf8"))
            self.assertEqual((saved["done"], saved["seconds"]), (3, 120))
            restarted = v.DailyTally()
            restarted.attach(path)
            self.assertEqual(restarted.totals()[1:], (3, 120))

    def test_pre_connection_runs_survive_attach_and_restart(self):
        with local(), patch.object(v.time, "time", return_value=NOON):
            for existing in (None, "invalid json", '{"day": 0, "runs": {}}'):
                with self.subTest(existing=existing), tempfile.TemporaryDirectory() as folder:
                    path = Path(folder) / "daily-summary.json"
                    if existing is not None:
                        path.write_text(existing, encoding="utf8")
                    first = v.DailyTally()
                    first.record("synthetic", "run-before-ble", "done", 60)
                    first.attach(path)
                    restarted = v.DailyTally()
                    restarted.attach(path)
                    self.assertEqual(restarted.totals()[1:], (1, 60))

    def test_merged_pre_connection_runs_are_saved_without_another_completion(self):
        with local(), tempfile.TemporaryDirectory() as folder, \
                patch.object(v.time, "time", return_value=NOON):
            path = Path(folder) / "daily-summary.json"
            first = v.DailyTally()
            first.attach(path)
            first.record("synthetic-a", "first", "done", 120)
            second = v.DailyTally()
            second.record("synthetic-b", "before-ble", "done", 60)
            second.attach(path)
            restarted = v.DailyTally()
            restarted.attach(path)
            self.assertEqual(restarted.totals()[1:], (2, 180))

    def test_counts_each_finished_run_once(self):
        with local(), patch.object(v.time, "time", return_value=NOON):
            m = v.Workspace()
            run(m, "a", "r1", "thinking", "tool", "done", elapsed=600)
            run(m, "a", "r1", "done", elapsed=600)  # heartbeat repeats
            run(m, "b", "r2", "thinking", "error", elapsed=120)
            run(m, "b", "r3", "thinking", "stopped", elapsed=60)
            run(m, "c", "r4", "thinking", elapsed=999)  # still working
            day, done, seconds = m.daily.totals()
        self.assertEqual((day, done, seconds), (v.work_day(NOON), 1, 780))

    def test_runs_first_seen_finished_are_not_counted(self):
        with local(), patch.object(v.time, "time", return_value=NOON):
            m = v.Workspace()
            run(m, "a", "r1", "done", elapsed=600)  # replayed after a bridge restart
            run(m, "a", "history", "thinking", "done", elapsed=50)  # restored journal
            self.assertEqual(m.daily.totals()[1:], (0, 0))

    def test_totals_reset_when_the_work_day_changes(self):
        with local():
            m = v.Workspace()
            with patch.object(v.time, "time", return_value=NOON):
                run(m, "a", "r1", "thinking", "done", elapsed=600)
                self.assertEqual(m.daily.totals()[1:], (1, 600))
            self.assertEqual(m.daily.totals(NOON + 86400)[1:], (0, 0))

    def test_persisted_totals_merge_for_the_same_day_only(self):
        with local(), tempfile.TemporaryDirectory() as folder, \
                patch.object(v.time, "time", return_value=NOON):
            path = Path(folder) / "daily-summary.json"
            first = v.Workspace()
            first.daily.attach(path)
            run(first, "a", "r1", "thinking", "done", elapsed=600)
            second = v.Workspace()
            run(second, "b", "r2", "thinking", "done", elapsed=60)
            second.daily.attach(path)
            run(second, "a", "r1", "thinking", "done", elapsed=600)  # already counted
            self.assertEqual(second.daily.totals()[1:], (2, 660))
            later = v.Workspace()
            later.daily.attach(path, now=NOON + 86400)
            self.assertEqual(later.daily.totals(NOON + 86400)[1:], (0, 0))


class Frame(unittest.TestCase):
    def test_summary_follows_snapshot_and_changes(self):
        with local():
            m = v.Workspace()
            link = ready_link(m)
            self.assertEqual(link.daily(NOON), [])  # device not ready before a snapshot
            link.snapshot()
            (kind, body), = link.daily(NOON)
            self.assertEqual(kind, 0x54)
            self.assertNotIn(kind, v.CONTROL)  # bulk lane, like the 0x53 clock
            self.assertEqual(struct.unpack("<QIHI", body), (m.epoch, v.work_day(NOON), 0, 0))
            self.assertEqual(link.daily(NOON), [])  # unchanged
            with patch.object(v.time, "time", return_value=NOON):
                run(m, "a", "r1", "thinking", "done", elapsed=7800)
            (_, body), = link.daily(NOON)
            self.assertEqual(struct.unpack("<QIHI", body)[2:], (1, 7800))
            # A new handshake (reconnect) sends the summary again.
            link.receive(2, v.pack("BBIHHB", 2, 2, v.CAPS, 20, 20, 0))
            self.assertEqual(len(link.daily(NOON)), 1)


if __name__ == "__main__":
    unittest.main()
