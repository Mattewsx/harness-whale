#!/usr/bin/env python3
"""Hardware stress run for the Harness Whale companion.

Drives two loops against a real device through the locally running bridge and
summarises link, voice and memory health:

* voice rounds: the operator records one utterance per round on the device;
  the script waits for the bridge's transcription result (record -> STT) and
  records its latency. Discard each draft on the device; nothing is sent.
* reconnect rounds: the script asks the bridge to drop the BLE link
  (loopback ``whale-link-cycle``) and measures how long the link takes to come
  back. With ``--cue-every K`` every K-th drop stays down long enough for the
  device to play its disconnect cue, exercising audio playback.

With ``--serial PORT`` the device's USB log is read as well, for heap/stack
headroom (``whale_diag`` / ``whale_voice`` lines), played cues, BLE disconnect
reasons and crashes or unexpected reboots. Without it, only bridge-side
measurements are reported.

The bridge must already be running (Harness with the companion plugin, or
``harness_ble_bridge.py``). The script talks to it on 127.0.0.1 only and never
reads session titles, replies or transcripts.
"""

from __future__ import annotations

import argparse
import json
import re
import socket
import statistics
import sys
import threading
import time
from pathlib import Path
from typing import Any

BRIDGE_PORT = 8765
CUE_HOLD_SECONDS = 7.0  # the device queues its disconnect cue after 5 s offline
STT_DONE = ("draft", "empty", "asr_error", "archived", "broken", "failed")
LOG_LINE = re.compile(r"^[IWE] \((\d+)\) (\w+): (.*)$")
FIELD = re.compile(r"(\w+)=(-?\w+)")
CRASH = re.compile(r"Guru Meditation|abort\(\) was called|Backtrace:|stack overflow|"
                   r"Task watchdog got triggered|rst:0x", re.I)


class Bridge:
    def __init__(self, port: int, timeout: float = 1.5) -> None:
        self.address = ("127.0.0.1", port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(timeout)

    def status(self) -> dict[str, Any] | None:
        self.sock.sendto(b'{"kind":"whale-link-status"}', self.address)
        deadline = time.monotonic() + self.sock.gettimeout()
        while time.monotonic() < deadline:
            try:
                data, _ = self.sock.recvfrom(65536)
            except socket.timeout:
                return None
            try:
                message = json.loads(data)
            except ValueError:
                continue
            if isinstance(message, dict) and message.get("kind") == "whale-link-status":
                return message
        return None

    def cycle(self, hold: float = 0.0) -> None:
        payload = {"kind": "whale-link-cycle", "hold": hold}
        self.sock.sendto(json.dumps(payload).encode(), self.address)

    def wait(self, predicate, timeout: float, interval: float = 0.25) -> dict[str, Any] | None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            status = self.status()
            if status is not None and predicate(status):
                return status
            time.sleep(interval)
        return None


def ready(status: dict[str, Any]) -> bool:
    return bool(status.get("connected")) and status.get("state") in ("v2.2", "connected")


class SerialMonitor(threading.Thread):
    """Collect only health counters from the device log; raw lines are not stored."""

    def __init__(self, port: str, baud: int) -> None:
        super().__init__(daemon=True)
        try:
            import serial  # pyserial, from installer/requirements-flash.txt
        except ImportError as exc:
            raise SystemExit("pyserial is required for --serial (pip install pyserial==3.5)") from exc
        self.serial = serial.Serial()
        self.serial.port = port
        self.serial.baudrate = baud
        self.serial.timeout = 0.5
        # Keep DTR/RTS released so opening the port does not reset the ESP32-C3.
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.open()
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.minimum: dict[str, int] = {}
        self.cues = {"played": 0, "skipped": 0}
        self.ble_reasons: dict[str, int] = {}
        self.voice_results: dict[str, int] = {}
        self.crashes: list[str] = []
        self.samples = 0

    def low(self, key: str, value: str) -> None:
        try:
            number = int(value)
        except ValueError:
            return
        if number > 0 and (key not in self.minimum or number < self.minimum[key]):
            self.minimum[key] = number

    def feed(self, line: str) -> None:
        with self.lock:
            if CRASH.search(line):
                self.crashes.append(line[:120])
                return
            match = LOG_LINE.match(line)
            if not match:
                return
            tag, body = match.group(2), match.group(3)
            fields = dict(FIELD.findall(body))
            if tag == "whale_diag" and "heap_min" in fields:
                self.samples += 1
                for key in ("heap_free", "heap_min", "stack_main", "stack_periph",
                            "stack_lvgl", "stack_ble"):
                    if key in fields:
                        self.low(key, fields[key])
            elif tag == "whale_diag" and "kind" in fields and "played" in fields:
                self.cues["played" if fields["played"] == "1" else "skipped"] += 1
            elif tag == "whale_voice":
                if "stack_min_bytes" in fields:
                    self.low("stack_voice", fields["stack_min_bytes"])
                if "min_heap" in fields:
                    self.low("heap_min", fields["min_heap"])
                if body.startswith("capture result="):
                    result = fields.get("result", "?")
                    self.voice_results[result] = self.voice_results.get(result, 0) + 1
            elif tag == "whale_ble" and "reason" in fields:
                reason = fields["reason"]
                self.ble_reasons[reason] = self.ble_reasons.get(reason, 0) + 1

    def run(self) -> None:
        while not self.stop.is_set():
            try:
                raw = self.serial.readline()
            except Exception as exc:  # port unplugged, device reset, etc.
                with self.lock:
                    self.crashes.append(f"serial read failed: {type(exc).__name__}")
                return
            if raw:
                self.feed(raw.decode("utf-8", "replace").strip())

    def close(self) -> dict[str, Any]:
        self.stop.set()
        self.join(timeout=2)
        self.serial.close()
        with self.lock:
            return {"diagSamples": self.samples, "minimum": dict(self.minimum),
                    "cues": dict(self.cues), "bleDisconnectReasons": dict(self.ble_reasons),
                    "voiceCaptureResults": dict(self.voice_results),
                    "crashes": list(self.crashes)}


def spread(values: list[int]) -> dict[str, Any]:
    if not values:
        return {}
    return {"count": len(values), "min": min(values), "median": round(statistics.median(values)),
            "max": max(values)}


def voice_rounds(bridge: Bridge, rounds: int, timeout: float) -> list[dict[str, Any]]:
    results = []
    for index in range(1, rounds + 1):
        before = bridge.status() or {}
        counts = dict(before.get("voice") or {})
        print(f"[voice {index}/{rounds}] 在设备上录一句话（按平时的语音操作）。"
              "识别结果出现后在设备上放弃，不要发送。", flush=True)

        def finished(status: dict[str, Any]) -> bool:
            now = status.get("voice") or {}
            return any(now.get(key, 0) > counts.get(key, 0) for key in STT_DONE)

        status = bridge.wait(finished, timeout)
        if status is None:
            results.append({"round": index, "outcome": "timeout"})
            print("  超时：没有收到识别结果", flush=True)
            continue
        now = status.get("voice") or {}
        outcome = next(key for key in STT_DONE if now.get(key, 0) > counts.get(key, 0))
        entry = {"round": index, "outcome": outcome, "sttMs": status.get("lastSttMs")}
        results.append(entry)
        print(f"  {outcome}，识别耗时 {entry['sttMs']} ms", flush=True)
        time.sleep(2)
    return results


def reconnect_rounds(bridge: Bridge, rounds: int, cue_every: int, timeout: float,
                     settle: float) -> list[dict[str, Any]]:
    results = []
    for index in range(1, rounds + 1):
        if bridge.wait(ready, timeout) is None:
            results.append({"round": index, "outcome": "not-ready"})
            print(f"[link {index}/{rounds}] 连接未就绪，跳过", flush=True)
            continue
        time.sleep(settle)
        before = bridge.status() or {}
        hold = CUE_HOLD_SECONDS if cue_every and index % cue_every == 0 else 0.0
        bridge.cycle(hold)
        dropped = bridge.wait(lambda s: s.get("disconnects", 0) > before.get("disconnects", 0), 10)
        if dropped is None:
            results.append({"round": index, "outcome": "no-drop"})
            print(f"[link {index}/{rounds}] 桥接器没有断开连接（版本过旧？）", flush=True)
            continue
        back = bridge.wait(ready, timeout + hold)
        if back is None:
            results.append({"round": index, "outcome": "timeout", "holdSeconds": hold})
            print(f"[link {index}/{rounds}] 超时：未能重连", flush=True)
            continue
        took = back.get("lastReconnectMs")
        results.append({"round": index, "outcome": "ok", "reconnectMs": took, "holdSeconds": hold})
        print(f"[link {index}/{rounds}] 重连 {took} ms" + (f"（含保持断开 {hold:.0f}s）" if hold else ""),
              flush=True)
    return results


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    parser = argparse.ArgumentParser(description="Harness Whale hardware stress run")
    parser.add_argument("--voice-rounds", type=int, default=5)
    parser.add_argument("--reconnects", type=int, default=10)
    parser.add_argument("--cue-every", type=int, default=3,
                        help="every Nth reconnect stays down long enough to play the disconnect cue; 0 disables")
    parser.add_argument("--serial", help="device USB serial port, e.g. COM5 or /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--bridge-port", type=int, default=BRIDGE_PORT)
    parser.add_argument("--voice-timeout", type=float, default=90.0)
    parser.add_argument("--reconnect-timeout", type=float, default=60.0)
    parser.add_argument("--settle", type=float, default=3.0, help="seconds to keep each link up before dropping it")
    parser.add_argument("--out", type=Path, help="write the JSON report here")
    args = parser.parse_args()

    bridge = Bridge(args.bridge_port)
    first = bridge.status()
    if first is None:
        print("没有连上本机桥接器：请先启动 Harness（或 harness_ble_bridge.py），并确认桥接器是 v1.3.1 或更新。",
              file=sys.stderr)
        return 2
    monitor = SerialMonitor(args.serial, args.baud) if args.serial else None
    if monitor:
        monitor.start()
    started = time.time()
    report: dict[str, Any] = {"kind": "harness-whale-stress", "startedAt": int(started),
                              "bridgeAtStart": first}
    voice: list[dict[str, Any]] = []
    link: list[dict[str, Any]] = []
    try:
        if args.voice_rounds > 0:
            voice = voice_rounds(bridge, args.voice_rounds, args.voice_timeout)
        if args.reconnects > 0:
            link = reconnect_rounds(bridge, args.reconnects, args.cue_every,
                                    args.reconnect_timeout, args.settle)
    except KeyboardInterrupt:
        print("已中断，只输出已完成的轮次", file=sys.stderr)
    report["durationSeconds"] = round(time.time() - started)
    report["bridgeAtEnd"] = bridge.status()
    report["voice"] = {"rounds": voice,
                       "ok": sum(r["outcome"] == "draft" for r in voice),
                       "sttMs": spread([r["sttMs"] for r in voice if isinstance(r.get("sttMs"), int)])}
    plain = [r["reconnectMs"] for r in link
             if r["outcome"] == "ok" and not r["holdSeconds"] and isinstance(r.get("reconnectMs"), int)]
    report["reconnect"] = {"rounds": link, "ok": sum(r["outcome"] == "ok" for r in link),
                           "reconnectMs": spread(plain)}
    if monitor:
        report["device"] = monitor.close()

    print(json.dumps({k: v for k, v in report.items() if k not in ("bridgeAtStart",)},
                     ensure_ascii=False, indent=2))
    if args.out:
        args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    failed = (len(voice) - report["voice"]["ok"]) + (len(link) - report["reconnect"]["ok"])
    crashed = bool(report.get("device", {}).get("crashes"))
    return 1 if failed or crashed else 0


if __name__ == "__main__":
    raise SystemExit(main())
