#!/usr/bin/env python3
"""Read the device's battery-life log and estimate how long a charge lasts.

The firmware records one entry per state-of-charge change in NVS (time in RTC
seconds, which keep counting through offline deep sleep, plus the power state
at that moment). It prints the stored runs over USB once per boot as
``whale_batt`` lines. USB power charges the cell, so measure like this:

1. Charge to 100 %, unplug USB, and use the device normally (or leave it).
   A jump of 5 points or more starts a new run; the old run is kept as
   ``batt_prev``.
2. When done (or when it shuts down), plug USB back in and run
   ``python harness/tools/whale_battery.py --serial COM5 --reset``. ``--reset``
   restarts the board so it prints the log; without it, press reset yourself.

``--log FILE`` analyses a saved serial capture instead. Only SOC, timestamps
and power flags are read; the log holds no session data.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path
from typing import Any, Iterable

ENTRY = re.compile(r"whale_batt: log=(batt_prev|batt_cur) i=(\d+) t=(\d+) soc=(\d+) flags=(\d+)")
FLAGS = {"linked": 1, "screen": 2, "timer_wake": 4, "slow_adv": 8}


def parse(lines: Iterable[str]) -> dict[str, list[dict[str, int]]]:
    """Collect the last complete dump of each run from serial lines."""
    runs: dict[str, list[dict[str, int]]] = {"batt_prev": [], "batt_cur": []}
    for line in lines:
        match = ENTRY.search(line)
        if not match:
            continue
        name, index, t, soc, flags = match.groups()
        if int(index) == 0:  # a new dump (e.g. a second boot) replaces the old one
            runs[name] = []
        runs[name].append({"t": int(t), "soc": int(soc), "flags": int(flags)})
    return runs


def segments(entries: list[dict[str, int]]) -> list[list[dict[str, int]]]:
    """Split where the RTC clock went backwards (power was lost)."""
    parts: list[list[dict[str, int]]] = []
    for entry in entries:
        if not parts or entry["t"] < parts[-1][-1]["t"]:
            parts.append([])
        parts[-1].append(entry)
    return parts


def analyse(entries: list[dict[str, int]]) -> dict[str, Any]:
    if len(entries) < 2:
        return {"entries": len(entries), "note": "not enough samples yet"}
    seconds = 0
    used = 0
    share = {name: 0 for name in FLAGS}
    for part in segments(entries):
        for before, after in zip(part, part[1:]):
            span = after["t"] - before["t"]
            seconds += span
            used += before["soc"] - after["soc"]
            # The state recorded at a sample describes the stretch that follows it.
            for name, bit in FLAGS.items():
                if before["flags"] & bit:
                    share[name] += span
    report: dict[str, Any] = {
        "entries": len(entries),
        "startSoc": entries[0]["soc"],
        "endSoc": entries[-1]["soc"],
        "hours": round(seconds / 3600, 2),
        "socUsed": used,
        "powerLosses": len(segments(entries)) - 1,
    }
    if seconds:
        report["timeShare"] = {name: round(value / seconds, 3) for name, value in share.items()}
    if seconds and used > 0:
        rate = used / (seconds / 3600)
        report["socPerHour"] = round(rate, 2)
        report["projectedFullChargeHours"] = round(100 / rate, 1)
    else:
        report["note"] = "no net discharge measured yet"
    return report


def read_serial(port: str, baud: int, reset: bool, wait: float) -> list[str]:
    try:
        import serial  # pyserial, from installer/requirements-flash.txt
    except ImportError as exc:
        raise SystemExit("pyserial is required for --serial (pip install pyserial==3.5)") from exc
    port_handle = serial.Serial()
    port_handle.port = port
    port_handle.baudrate = baud
    port_handle.timeout = 0.5
    port_handle.dtr = False
    port_handle.rts = False
    port_handle.open()
    lines: list[str] = []
    try:
        if reset:  # USB-Serial-JTAG: RTS pulses the chip reset
            port_handle.rts = True
            time.sleep(0.2)
            port_handle.rts = False
        deadline = time.monotonic() + wait
        while time.monotonic() < deadline:
            raw = port_handle.readline()
            if raw:
                lines.append(raw.decode("utf-8", "replace").strip())
    finally:
        port_handle.close()
    return lines


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Harness Whale battery-life report")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--serial", help="device USB serial port, e.g. COM5 or /dev/ttyACM0")
    source.add_argument("--log", type=Path, help="saved serial capture to analyse")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--reset", action="store_true", help="reset the board so it prints the log")
    parser.add_argument("--wait", type=float, default=12.0, help="seconds to read the serial port")
    parser.add_argument("--out", type=Path, help="write the JSON report here")
    args = parser.parse_args(argv)

    if args.log:
        lines = args.log.read_text(encoding="utf-8", errors="replace").splitlines()
    else:
        lines = read_serial(args.serial, args.baud, args.reset, args.wait)
    runs = parse(lines)
    if not any(runs.values()):
        print("没有读到电量日志：确认是本版固件，并在串口打开后复位设备（或加 --reset）。",
              file=sys.stderr)
        return 1
    report = {"current": analyse(runs["batt_cur"]), "previous": analyse(runs["batt_prev"])}
    text = json.dumps(report, ensure_ascii=False, indent=2)
    print(text)
    if args.out:
        args.out.write_text(text + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
