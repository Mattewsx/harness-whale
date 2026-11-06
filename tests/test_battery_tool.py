"""The battery-life report parses the device's serial dump and estimates life."""
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "harness/tools"))
import whale_battery as battery


def dump(name, entries):
    lines = [f"I (812) whale_batt: log={name} count={len(entries)}"]
    for index, (t, soc, flags) in enumerate(entries):
        lines.append(f"I (813) whale_batt: log={name} i={index} t={t} soc={soc} flags={flags}")
    return lines


class BatteryReport(unittest.TestCase):
    def test_parse_keeps_the_latest_dump_of_each_run(self):
        lines = dump("batt_cur", [(0, 100, 3)]) + ["I (1) whale: noise"]
        lines += dump("batt_cur", [(0, 100, 3), (3600, 90, 0)])
        lines += dump("batt_prev", [(5, 80, 1)])
        runs = battery.parse(lines)
        self.assertEqual([e["soc"] for e in runs["batt_cur"]], [100, 90])
        self.assertEqual(runs["batt_prev"], [{"t": 5, "soc": 80, "flags": 1}])

    def test_rate_projection_and_time_share(self):
        # Two hours linked with the screen on (10 points), then two hours in
        # offline sleep (2 points): 12 points in 4 hours.
        entries = [{"t": 0, "soc": 100, "flags": 3}, {"t": 7200, "soc": 90, "flags": 12},
                   {"t": 14400, "soc": 88, "flags": 12}]
        report = battery.analyse(entries)
        self.assertEqual(report["hours"], 4.0)
        self.assertEqual(report["socUsed"], 12)
        self.assertEqual(report["socPerHour"], 3.0)
        self.assertEqual(report["projectedFullChargeHours"], 33.3)
        self.assertEqual(report["timeShare"]["linked"], 0.5)
        self.assertEqual(report["timeShare"]["timer_wake"], 0.5)
        self.assertEqual(report["powerLosses"], 0)

    def test_clock_reset_is_not_counted_as_time(self):
        entries = [{"t": 100, "soc": 60, "flags": 0}, {"t": 3700, "soc": 50, "flags": 0},
                   {"t": 3, "soc": 1, "flags": 0}, {"t": 30, "soc": 2, "flags": 0}]
        report = battery.analyse(entries)
        self.assertEqual(report["powerLosses"], 1)
        self.assertEqual(report["hours"], round(3627 / 3600, 2))

    def test_short_or_flat_runs_say_so(self):
        self.assertIn("note", battery.analyse([{"t": 0, "soc": 50, "flags": 0}]))
        flat = battery.analyse([{"t": 0, "soc": 50, "flags": 0}, {"t": 60, "soc": 50, "flags": 0}])
        self.assertNotIn("projectedFullChargeHours", flat)

    def test_cli_reads_a_saved_capture(self):
        with tempfile.TemporaryDirectory() as folder:
            capture = Path(folder) / "serial.txt"
            capture.write_text("\n".join(dump("batt_cur", [(0, 100, 2), (1800, 99, 2)])))
            out = Path(folder) / "report.json"
            with redirect_stdout(io.StringIO()):
                self.assertEqual(battery.main(["--log", str(capture), "--out", str(out)]), 0)
            self.assertEqual(json.loads(out.read_text())["current"]["socPerHour"], 2.0)
            capture.write_text("I (1) whale: nothing here\n")
            with redirect_stderr(io.StringIO()):
                self.assertEqual(battery.main(["--log", str(capture)]), 1)


if __name__ == "__main__":
    unittest.main()
