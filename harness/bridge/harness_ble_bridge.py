#!/usr/bin/env python3
"""Relay local DeepSeek Harness events to the passport over BLE.

The Harness plugin talks only to this process on 127.0.0.1 via UDP. The bridge
does not forward prompts, file paths, tool arguments, outputs, or API keys.
"""

from __future__ import annotations

import argparse
import asyncio
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
import json
import logging
from pathlib import Path
import os
import random
import sys
import time
from typing import Any
import urllib.error
import urllib.request

try:
    from . import whale_v2
    from .whale_history import journal_path, read_session_history, reply_pages
    from .whale_pairing import pair_passport, paired_passport_device, PairingDeclined
    from .harness_ble_protocol import (
        Balance, Currency, State, make_frame, make_question_frame, normalize_state, snapshot_from_mapping,
    )
except ImportError:  # Direct execution: python tools/harness_ble_bridge.py
    import whale_v2
    from whale_history import journal_path, read_session_history, reply_pages
    from whale_pairing import pair_passport, paired_passport_device, PairingDeclined
    from harness_ble_protocol import (
        Balance, Currency, State, make_frame, make_question_frame, normalize_state, snapshot_from_mapping,
    )


CMD_UUID = "54524145-4341-5244-0000-000000000010"
NOTIFY_UUID = "54524145-4341-5244-0000-000000000011"
DEFAULT_DEVICE_NAME = "HARNESS-WHALE"
DEFAULT_UDP_PORT = 8765
DEFAULT_TELEMETRY_PORT = 8766
BALANCE_URL = "https://api.deepseek.com/user/balance"
FINAL_HOLD_SECONDS = 4.0
STALE_EVENT_SECONDS = 30.0
QUESTION_SINGLE_PAGE_OPTIONS = 4
QUESTION_PAGED_OPTIONS = 3
# Reconnect backoff: 1 s, 2 s, 4 s ... capped at 60 s, with +/-20 % jitter so a
# flapping link or an absent device does not hammer the radio. A link that
# stayed up for RECONNECT_STABLE_SECONDS (or a deliberate stress-test cycle)
# starts again from the base delay.
RECONNECT_BASE_SECONDS = 1.0
RECONNECT_MAX_SECONDS = 60.0
RECONNECT_STABLE_SECONDS = 30.0
RECONNECT_JITTER = 0.2
LINK_HEARTBEAT_SECONDS = 2.0
PAIRING_DECLINED_SECONDS = 60.0
MAX_LINK_HOLD_SECONDS = 30.0

LOG = logging.getLogger("harness-ble")


def read_deepseek_api_key() -> str:
    """Read the key without ever forwarding it to Harness, UDP, or BLE.

    The Windows installer writes the value to the current user's Environment
    registry key. Reading that key directly also works when Harness was already
    running before the value was configured and therefore has a stale process
    environment.
    """
    value = os.environ.get("DEEPSEEK_API_KEY", "").strip()
    if value or os.name != "nt":
        return value
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Environment") as key:
            stored, _ = winreg.QueryValueEx(key, "DEEPSEEK_API_KEY")
        return str(stored).strip()
    except (FileNotFoundError, OSError, TypeError, ValueError):
        return ""


def parse_balance_document(document: dict[str, Any]) -> Balance | None:
    infos = document.get("balance_infos")
    if not isinstance(infos, list) or not infos:
        return None
    candidates = [item for item in infos if isinstance(item, dict)]
    candidates.sort(key=lambda item: str(item.get("currency", "")).upper() != "CNY")
    for item in candidates:
        code = str(item.get("currency", "")).upper()
        currency = Currency.CNY if code == "CNY" else Currency.USD if code == "USD" else None
        if currency is None:
            continue
        try:
            minor = int((Decimal(str(item.get("total_balance", "0"))) * 100)
                        .quantize(Decimal("1"), rounding=ROUND_HALF_UP))
        except (InvalidOperation, TypeError, ValueError):
            continue
        return Balance(max(0, minor), currency, bool(document.get("is_available", False)))
    return None


def fetch_balance(api_key: str) -> Balance | None:
    request = urllib.request.Request(
        BALANCE_URL,
        headers={
            "Accept": "application/json",
            "Authorization": f"Bearer {api_key}",
            "Cache-Control": "no-cache",
            "Pragma": "no-cache",
        },
        method="GET",
    )
    with urllib.request.urlopen(request, timeout=10) as response:
        document = json.loads(response.read().decode("utf-8"))
    if not isinstance(document, dict):
        return None
    return parse_balance_document(document)


class LinkCycle(Exception):
    """Raised inside the BLE loop to drop the link on a local stress-test request."""


class LinkStats:
    """Disconnect accounting and reconnect backoff for one bridge process.

    Only counters, exception type names and timings are kept: no addresses,
    session IDs or text, so the values are safe to show in /whale.
    """

    def __init__(self, clock=time.monotonic, wall=time.time, rand=random.random) -> None:
        self.clock = clock
        self.wall = wall
        self.rand = rand
        self.state = "starting"
        self.connected = False
        self.disconnects = 0
        self.last_reason: str | None = None
        self.last_disconnect_at: int | None = None
        self.last_reconnect_ms: int | None = None
        self.retry_at: float | None = None
        self.attempt = 0
        self.connected_at: float | None = None
        self.down_at: float | None = None

    def up(self) -> None:
        """The link carries frames again (legacy connect or v2.2/2.3 handshake)."""
        now = self.clock()
        if self.connected_at is None:
            self.connected_at = now
        if self.down_at is not None:
            self.last_reconnect_ms = round((now - self.down_at) * 1000)
            self.down_at = None
        self.retry_at = None

    def lost(self, reason: str, manual: bool = False) -> float:
        """Record a failed attempt or a dropped link; return the delay before retrying."""
        now = self.clock()
        if self.connected_at is not None:
            self.disconnects += 1
            self.last_reason = reason
            self.last_disconnect_at = int(self.wall() * 1000)
            if now - self.connected_at >= RECONNECT_STABLE_SECONDS:
                self.attempt = 0
            self.connected_at = None
            self.down_at = now
        if manual:
            self.attempt = 0
        delay = min(RECONNECT_MAX_SECONDS, RECONNECT_BASE_SECONDS * (2 ** min(self.attempt, 16)))
        delay *= 1 + RECONNECT_JITTER * (2 * self.rand() - 1)
        self.attempt += 1
        return self.hold(min(RECONNECT_MAX_SECONDS, delay))

    def hold(self, seconds: float) -> float:
        """A fixed wait (pairing declined) that does not grow the backoff."""
        self.retry_at = self.clock() + seconds
        return seconds

    def fields(self) -> dict[str, Any]:
        fields: dict[str, Any] = {"disconnects": self.disconnects}
        if self.last_reason:
            fields["lastDisconnect"] = self.last_reason
            fields["lastDisconnectAt"] = self.last_disconnect_at
        if self.last_reconnect_ms is not None:
            fields["lastReconnectMs"] = self.last_reconnect_ms
        if self.retry_at is not None:
            fields["retryIn"] = round(max(0.0, self.retry_at - self.clock()), 2)
        return fields


class StatusModel:
    def __init__(self) -> None:
        self.workspace = whale_v2.Workspace()
        self.current: dict[str, Any] = {"state": "offline", "tool": "none"}
        self.pending: dict[str, Any] | None = None
        self.updated_at = 0.0
        self.final_until = 0.0
        self.seq = 0
        self.balance: Balance | None = None
        self.balance_refresh = asyncio.Event()
        self.new_turn_pending = False
        self.changed = asyncio.Event()
        self.question: dict[str, Any] | None = None
        self.question_addr: tuple[str, int] | None = None
        self.question_page = 0
        self.question_revision = 0
        self.datagram_transport: asyncio.DatagramTransport | None = None
        self.link = LinkStats()
        self.link_cycle = asyncio.Event()
        self.link_hold = 0.0
        self.packets_sent = 0
        self.question_received = 0
        self.approval_received = 0

    def link_report(self) -> dict[str, Any]:
        """Loopback-only status for the stress script: counters, no session data."""
        report: dict[str, Any] = {
            "kind": "whale-link-status",
            "connected": self.link.connected,
            "state": self.link.state,
            "packetsSent": self.packets_sent,
            **self.link.fields(),
        }
        frame = getattr(self.workspace, "frame", None)
        if frame:
            report["frame"] = frame
        voice = getattr(self.workspace, "voice", None)
        if voice is not None:
            report["voice"] = dict(getattr(voice, "stats", {}))
            if getattr(voice, "last_stt_ms", None) is not None:
                report["lastSttMs"] = voice.last_stt_ms
        return report

    def merge(self, data: dict[str, Any]) -> None:
        if data.get("v") != 1:
            return
        now = time.monotonic()
        incoming = dict(data)
        state = normalize_state(incoming.get("state"))
        if state in (State.DONE, State.ERROR, State.STOPPED):
            self.current = incoming
            self.pending = None
            self.final_until = now + FINAL_HOLD_SECONDS
        elif normalize_state(self.current.get("state")) in (State.DONE, State.ERROR, State.STOPPED) and now < self.final_until:
            self.pending = incoming
        else:
            self.current = incoming
            self.pending = None
        if incoming.get("newTurn"):
            self.new_turn_pending = True
        self.updated_at = now
        self.changed.set()

    def set_balance(self, value: Balance | None) -> None:
        self.workspace.balance_old = value is None and self.workspace.balance is not None
        if value is not None: self.workspace.balance = value
        if value != self.balance:
            self.balance = value
            self.changed.set()

    def request_balance_refresh(self) -> None:
        self.balance_refresh.set()

    def set_question(self, data: dict[str, Any], addr: tuple[str, int]) -> None:
        request_id = str(data.get("requestId") or "")
        options = data.get("options")
        if not request_id or not isinstance(options, list) or len(options) < 2:
            return
        if data.get("mode", "single") != "single":  # legacy v1 firmware answers single choice only
            return
        labels = [str(item) for item in options]
        self.question = {"requestId": request_id, "options": labels}
        self.question_addr = addr
        self.question_page = 0
        self.question_revision += 1
        self.changed.set()

    def clear_question(self, request_id: str = "") -> None:
        if self.question is None:
            return
        if request_id and request_id != self.question.get("requestId"):
            return
        self.question = None
        self.question_addr = None
        self.question_page = 0
        self.question_revision += 1
        self.changed.set()

    def question_page_view(self) -> tuple[list[str], int, int | None]:
        """Return visible labels, their source offset and the optional paging index."""
        if self.question is None:
            return [], 0, None
        options = self.question["options"]
        if len(options) <= QUESTION_SINGLE_PAGE_OPTIONS:
            return list(options), 0, None
        page_count = (len(options) + QUESTION_PAGED_OPTIONS - 1) // QUESTION_PAGED_OPTIONS
        page = self.question_page % page_count
        start = page * QUESTION_PAGED_OPTIONS
        labels = list(options[start:start + QUESTION_PAGED_OPTIONS])
        labels.append("下一页" if page + 1 < page_count else "回到开头")
        return labels, start, len(labels) - 1

    def answer_question(self, index: int) -> None:
        pending = self.question
        addr = self.question_addr
        transport = self.datagram_transport
        if pending is None or addr is None or transport is None:
            return
        visible, start, paging_index = self.question_page_view()
        if not 0 <= index < len(visible):
            return
        if paging_index is not None and index == paging_index:
            page_count = ((len(pending["options"]) + QUESTION_PAGED_OPTIONS - 1) //
                          QUESTION_PAGED_OPTIONS)
            self.question_page = (self.question_page + 1) % page_count
            self.question_revision += 1
            self.changed.set()
            return
        selected = start + index
        if selected >= len(pending["options"]):
            return
        document = {
            "v": 1,
            "kind": "harness-answer",
            "requestId": pending["requestId"],
            "selected": selected,
        }
        transport.sendto(json.dumps(document, separators=(",", ":")).encode("utf-8"), addr)
        self.clear_question(str(pending["requestId"]))

    def question_frame(self) -> bytes:
        visible, _, _ = self.question_page_view()
        return make_question_frame(visible)

    def next_frame(self) -> bytes:
        now = time.monotonic()
        if self.pending is not None and now >= self.final_until:
            self.current = self.pending
            self.pending = None
        data = dict(self.current)
        state = normalize_state(data.get("state"))
        if self.updated_at and now - self.updated_at > STALE_EVENT_SECONDS and state not in (State.IDLE, State.OFFLINE):
            data.update(state="idle", tool="none")
        self.seq = (self.seq + 1) & 0xFFFFFFFF
        snapshot = snapshot_from_mapping(
            data,
            seq=self.seq,
            balance=self.balance,
            new_turn=self.new_turn_pending,
        )
        self.new_turn_pending = False
        return make_frame(snapshot)


class HarnessDatagram(asyncio.DatagramProtocol):
    def __init__(self, model: StatusModel) -> None:
        self.model = model

    def connection_made(self, transport: asyncio.BaseTransport) -> None:
        self.model.workspace.transport = transport
        self.model.datagram_transport = transport  # type: ignore[assignment]

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        if addr[0] not in {"127.0.0.1", "::1"} or len(data) > 60000:
            return
        try:
            document = json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            LOG.warning("ignored malformed local Harness event")
            return
        if isinstance(document, dict):
            kind = document.get("kind")
            if kind == "whale-roster-check":
                # Local read-only diagnostic, with no session IDs, titles or reply text.
                report = {"kind":"whale-roster-check","projected":len(self.model.workspace.sessions),
                          "lastGattSnapshot":self.model.workspace.delivered,
                          "questionDiagnostics": {
                              "received": self.model.question_received,
                              "approvalReceived": self.model.approval_received,
                              "pending": sum(q.status == 4 for q in self.model.workspace.questions.values()),
                              "pendingApproval": sum(q.status == 4 and q.mode == 2
                                                     for q in self.model.workspace.questions.values())}}
                if self.model.datagram_transport:
                    self.model.datagram_transport.sendto(json.dumps(report).encode(), addr)
            elif kind == "whale-link-status":
                if self.model.datagram_transport:
                    self.model.datagram_transport.sendto(
                        json.dumps(self.model.link_report()).encode(), addr)
            elif kind == "whale-link-cycle":
                # Local stress test: drop the BLE link once and let it reconnect,
                # optionally staying down long enough for the device's disconnect cue.
                try:
                    hold = float(document.get("hold") or 0)
                except (TypeError, ValueError):
                    hold = 0.0
                self.model.link_hold = min(max(hold, 0.0), MAX_LINK_HOLD_SECONDS)
                self.model.link_cycle.set()
                self.model.changed.set()
            elif kind == "whale-visibility":
                items = document.get("items")
                if not isinstance(items,list): return
                for item in items:
                    if not isinstance(item,dict): continue
                    key = item.get("key")
                    self.model.workspace.set_visibility(key,item.get("hidden"),item.get("stamp"))
                    if isinstance(key,str) and self.model.workspace.is_hidden(key):
                        if self.model.current.get("sessionId")==key:
                            self.model.current={"state":"idle","tool":"none"}
                            self.model.final_until=0
                        if self.model.pending and self.model.pending.get("sessionId")==key:
                            self.model.pending=None
                self.model.changed.set()
            elif kind == "whale-reply-page":
                self.model.workspace.reply_page(document,addr)
            elif kind == "whale-result":
                self.model.workspace.resolve(document.get("requestId"), document.get("status"), document.get("option",65535))
            elif kind == "harness-question":
                self.model.question_received += 1
                self.model.approval_received += document.get("mode") == "approval"
                self.model.workspace.question(document, addr)
                if not self.model.workspace.is_hidden(document.get("sessionId")):
                    self.model.set_question(document, addr)
            elif kind == "harness-question-cancel":
                self.model.workspace.resolve(document.get("requestId"),2)
                self.model.clear_question(str(document.get("requestId") or ""))
            elif kind == "harness-balance-refresh":
                self.model.request_balance_refresh()
            else:
                self.model.workspace.update(document,addr)
                if not self.model.workspace.is_hidden(document.get("sessionId")):
                    self.model.merge(document)


async def balance_loop(model: StatusModel, interval: float) -> None:
    api_key = read_deepseek_api_key()
    if not api_key:
        LOG.info("DEEPSEEK_API_KEY is not set; balance will display as --")
        return
    while True:
        model.balance_refresh.clear()
        try:
            value = await asyncio.to_thread(fetch_balance, api_key)
            model.set_balance(value)
            LOG.info("DeepSeek balance refreshed (%s)", value.currency.name if value else "unavailable")
        except (OSError, urllib.error.URLError, ValueError, json.JSONDecodeError) as exc:
            model.workspace.balance_old = model.workspace.balance is not None
            LOG.warning("balance refresh failed: %s", type(exc).__name__)
        try:
            await asyncio.wait_for(model.balance_refresh.wait(), timeout=interval)
        except asyncio.TimeoutError:
            pass


async def write_frame(client: Any, frame: bytes) -> None:
    mtu = getattr(client, "mtu_size", 23) or 23
    step = max(20, min(mtu - 3, 244))
    for offset in range(0, len(frame), step):
        await client.write_gatt_char(CMD_UUID, frame[offset:offset + step], response=True)


def send_telemetry(transport: asyncio.DatagramTransport, port: int, **fields: Any) -> None:
    document = {
        "v": 1,
        "kind": "harness-whale-hardware",
        "sentAt": int(time.time() * 1000),
        **fields,
    }
    transport.sendto(json.dumps(document, separators=(",", ":")).encode("utf-8"),
                     ("127.0.0.1", port))


async def ble_loop(model: StatusModel, device_name: str, address: str | None,
                   interval: float, telemetry: asyncio.DatagramTransport,
                   telemetry_port: int) -> None:
    try:
        from bleak import BleakClient, BleakScanner
    except ImportError as exc:
        raise SystemExit("bleak is required; install tools/requirements-client.txt") from exc

    link = model.link
    known_device = None
    connection_failures = 0

    def report(state: str, connected: bool = False, **fields: Any) -> None:
        link.state = state
        link.connected = connected
        send_telemetry(telemetry, telemetry_port, connected=connected, state=state,
                       device=device_name, packetsSent=model.packets_sent,
                       **link.fields(), **fields)

    async def wait_retry(delay: float, state: str, **fields: Any) -> None:
        # Keep /whale alive during backoff; its heartbeat expires after 7 s.
        # Use a deadline so telemetry work cannot extend the retry delay.
        deadline = link.clock() + delay
        while True:
            report(state, **fields)
            remaining = deadline - link.clock()
            if remaining <= 0:
                return
            await asyncio.sleep(min(LINK_HEARTBEAT_SECONDS, remaining))

    while True:
        link.retry_at = None
        model.link_cycle.clear()
        try:
            device = known_device
            if device is None:
                try:
                    device = await asyncio.wait_for(
                        paired_passport_device(device_name, address), timeout=4.0)
                except (ImportError, OSError, asyncio.TimeoutError) as exc:
                    LOG.debug("Paired-device lookup unavailable: %s", type(exc).__name__)
                if device is None:
                    device = address
            if device is None:
                LOG.info("scanning for BLE device %s", device_name)
                report("scanning")
                found = await BleakScanner.find_device_by_name(device_name, timeout=8.0)
                if found is None:
                    await wait_retry(link.lost("not-found"), "not-found")
                    continue
                device = found
            report("connecting")
            # Reusing the peer identity must not reuse a stale GATT database
            # after the device restarted or its firmware changed.
            options = {"winrt": {"use_cached_services": False}} if sys.platform == "win32" else {}
            async with BleakClient(device, **options) as client:
                # Retain the discovered/cached BLEDevice, not just its address:
                # a new name scan can miss a peer Windows still keeps connected.
                known_device = device
                LOG.info("BLE connected to %s", device_name)
                mtu = int(getattr(client, "mtu_size", 23) or 23)
                report("connected", True, mtu=mtu)

                if client.services.get_service(whale_v2.SERVICE):
                    await pair_passport(client)
                    phase = "negotiating"
                    def record_write(ready):
                        nonlocal phase, connection_failures
                        model.packets_sent += 1
                        if ready and phase != "v2.2":
                            phase = "v2.2"
                            connection_failures = 0
                            link.up()
                    relay = asyncio.create_task(whale_v2.serve(client, model.workspace, record_write))
                    cycle = asyncio.create_task(model.link_cycle.wait())
                    try:
                        while not relay.done():
                            if cycle.done():
                                raise LinkCycle("stress-test cycle")
                            frame = getattr(model.workspace, "frame", None)
                            report(phase, True, mtu=mtu, **({"frame": frame} if frame else {}))
                            await asyncio.wait({relay, cycle}, timeout=2,
                                               return_when=asyncio.FIRST_COMPLETED)
                        await relay
                    finally:
                        relay.cancel()
                        cycle.cancel()
                        await asyncio.gather(relay, cycle, return_exceptions=True)
                    # serve() returns once the peer has gone away.
                    await wait_retry(link.lost("link-lost"), "reconnecting")
                    continue
                if client.services.get_characteristic(NOTIFY_UUID) is None:
                    raise ConnectionError("Passport GATT services unavailable; reconnecting")

                def on_notify(_: Any, data: bytearray) -> None:
                    if len(data) >= 2 and data[0] == 0x07 and data[1] >= 0x10:
                        LOG.warning("device rejected Harness packet: status=0x%02X", data[1])
                    if len(data) >= 2 and data[0] == 0x08 and 0x40 <= data[1] < 0x44:
                        pass  # Legacy devices lack question identity; display only.

                await client.start_notify(NOTIFY_UUID, on_notify)
                link.up()
                connection_failures = 0
                question_revision = -1
                while client.is_connected:
                    if model.link_cycle.is_set():
                        raise LinkCycle("stress-test cycle")
                    model.changed.clear()
                    if question_revision != model.question_revision:
                        await write_frame(client, make_question_frame([]))
                        question_revision = model.question_revision
                    await write_frame(client, model.next_frame())
                    model.packets_sent += 1
                    report("connected", True, mtu=mtu)
                    try:
                        await asyncio.wait_for(model.changed.wait(), timeout=interval)
                    except asyncio.TimeoutError:
                        pass
            await wait_retry(link.lost("link-lost"), "reconnecting")
        except asyncio.CancelledError:
            raise
        except LinkCycle:
            LOG.info("BLE link cycled by local stress test")
            delay = link.hold(max(link.lost("stress-test", manual=True), model.link_hold))
            model.link_hold = 0.0
            await wait_retry(delay, "reconnecting")
        except PairingDeclined as exc:
            LOG.warning("BLE pairing declined: %s", exc)
            link.lost(type(exc).__name__)
            await wait_retry(link.hold(PAIRING_DECLINED_SECONDS), "reconnecting",
                             error=type(exc).__name__)
        except Exception as exc:
            connection_failures += 1
            if connection_failures >= 3:
                known_device = None
                connection_failures = 0
            delay = link.lost(type(exc).__name__)
            LOG.warning("BLE reconnect in %.1fs after %s: %s", delay, type(exc).__name__, exc)
            await wait_retry(delay, "reconnecting", error=type(exc).__name__)


async def demo_loop(model: StatusModel) -> None:
    demo = [
        {"v": 1, "state": "idle", "tool": "none", "elapsed": 0},
        {"v": 1, "state": "thinking", "tool": "none", "elapsed": 2, "newTurn": True},
        {"v": 1, "state": "tool", "tool": "Read", "elapsed": 5, "todoDone": 1, "todoTotal": 4},
        {"v": 1, "state": "tool", "tool": "exec_command", "elapsed": 8, "todoDone": 2, "todoTotal": 4},
        {"v": 1, "state": "waiting", "tool": "none", "elapsed": 11, "todoDone": 2, "todoTotal": 4},
        {"v": 1, "state": "question", "tool": "none", "elapsed": 13,
         "_options": ["继续修复", "先看说明", "稍后处理", "其他(电脑)"]},
        {"v": 1, "state": "stopped", "tool": "none", "elapsed": 14, "todoDone": 2, "todoTotal": 4},
        {"v": 1, "state": "done", "tool": "none", "elapsed": 16, "todoDone": 4, "todoTotal": 4},
        {"v": 1, "state": "idle", "tool": "none", "elapsed": 16, "todoDone": 4, "todoTotal": 4},
    ]
    while True:
        for item in demo:
            current = dict(item)
            options = current.pop("_options", None)
            if options is None:
                model.clear_question("demo")
            else:
                model.set_question({"requestId": "demo", "options": options}, ("127.0.0.1", 9))
            model.merge(current)
            await asyncio.sleep(4)


def workspace_roster(path: Path) -> set[str]:
    """Read only the committed Workspace v2 projection; preserve last good data on partial writes."""
    store = json.loads(path.read_text(encoding="utf-8-sig"))
    if store["unit"] != {"name": "workspace", "version": 2} or store["global"]["initialized"] is not True:
        raise ValueError("workspace registry not ready")
    def ids(values):
        if not isinstance(values, list) or any(not isinstance(x,str) or not x for x in values):
            raise ValueError("invalid workspace membership")
        return set(values)
    members = set()
    for wid in ids(store["global"]["workspaceIds"]):
        members.update(ids(store["tables"]["workspaces"][wid]["sessionIds"]))
    return members - ids(store["global"]["archivedSessionIds"])


def load_history(root, key, previous):
    path = journal_path(root, key)
    if path is None:
        return None, None
    stat = path.stat()
    signature = (path, stat.st_size, stat.st_mtime_ns)
    if signature == previous:
        return signature, None
    history = read_session_history(path, key)
    if history is not None:
        reply_id, pages = reply_pages(history.reply)
        return signature, (history, reply_id, pages)
    return None, None


async def sync_workspace(model, home, retries):
    path = home / "storages" / "workspace.json"
    roster = await asyncio.to_thread(workspace_roster, path)
    workspace = model.workspace
    if workspace.roster != roster:
        workspace.set_roster(roster)
        for key in retries.keys() - roster:
            del retries[key]
        if model.current.get("sessionId") not in roster:
            model.current = {"state":"idle","tool":"none"}
            model.final_until = 0
        if model.pending and model.pending.get("sessionId") not in roster:
            model.pending = None
        model.changed.set()
    for key in sorted(roster):
        next_check, signature = retries.get(key, (0, None))
        if workspace.is_hidden(key) or next_check > time.monotonic():
            continue
        sid = workspace.keys.get(key)
        row = workspace.sessions.get(sid, {})
        if row.get("replyId") and row.get("title") and sid not in workspace.history_pages:
            continue
        signature, result = await asyncio.to_thread(load_history, home / "sessions", key, signature if row else None)
        if result is None:
            retries[key] = time.monotonic() + 5, signature
            continue
        # Reading an inactive session may take longer than a concurrent archive.
        if await asyncio.to_thread(workspace_roster, path) != roster:
            return
        if not workspace.is_hidden(key):
            workspace.restore_history(key, *result)
            retries[key] = time.monotonic() + 5, signature
            model.changed.set()


async def refresh_workspace(model):
    here = Path(__file__).resolve().parent
    home = here.parent if here.name == "harness-whale-bridge" else Path.home()/".dsh"
    retries = {}
    while True:
        try:
            await sync_workspace(model, home, retries)
        except (OSError, ValueError, KeyError, TypeError):
            pass
        await asyncio.sleep(1)


async def async_main(args: argparse.Namespace) -> None:
    loop = asyncio.get_running_loop()
    model = StatusModel()
    if not args.demo:
        model.workspace.set_roster(set())
    transport, _ = await loop.create_datagram_endpoint(
        lambda: HarnessDatagram(model), local_addr=("127.0.0.1", args.port))
    telemetry, _ = await loop.create_datagram_endpoint(
        lambda: asyncio.DatagramProtocol(), local_addr=("127.0.0.1", 0))
    LOG.info("listening for Harness events on 127.0.0.1:%d", args.port)
    send_telemetry(telemetry, args.telemetry_port, connected=False,
                   state="starting", device=args.device_name, packetsSent=0)
    tasks = []
    try:
        if args.demo:
            tasks.append(asyncio.create_task(demo_loop(model)))
        else:
            tasks.append(asyncio.create_task(refresh_workspace(model)))
        # Battery remains device-local; this release does not poll account balances.
        if args.dry_run:
            while True:
                model.changed.clear()
                LOG.info("dry-run frame=%s", model.next_frame().hex())
                try:
                    await asyncio.wait_for(model.changed.wait(), timeout=args.interval)
                except asyncio.TimeoutError:
                    pass
        else:
            await ble_loop(model, args.device_name, args.address, args.interval,
                           telemetry, args.telemetry_port)
    finally:
        send_telemetry(telemetry, args.telemetry_port, connected=False,
                       state="stopped", device=args.device_name)
        transport.close()
        telemetry.close()
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="DeepSeek Harness to BLE passport bridge")
    parser.add_argument("--device-name", default=DEFAULT_DEVICE_NAME)
    parser.add_argument("--address", help="optional fixed BLE address")
    parser.add_argument("--port", type=int, default=DEFAULT_UDP_PORT)
    parser.add_argument("--telemetry-port", type=int, default=DEFAULT_TELEMETRY_PORT)
    parser.add_argument("--interval", type=float, default=2.0)
    parser.add_argument("--balance-interval", type=float, default=300.0)
    parser.add_argument("--no-balance", action="store_true")
    parser.add_argument("--demo", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )
    try:
        asyncio.run(async_main(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
