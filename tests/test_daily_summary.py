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
