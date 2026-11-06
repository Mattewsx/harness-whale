"""BLE OTA client for the Harness Whale passport (firmware v1.4+).

Uses its own GATT characteristic, independent of the 2.x status protocol:
op byte + payload, windowed writes without response, status via notify.
Wire format: device-firmware/main/whale_ota_core.h and docs/OTA.md.
"""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
import hashlib
import re
import struct
import time
from typing import Any, Callable

OTA_UUID = "13b50204-663a-48c5-a224-37c1c2569080"
PROJECT = "FoloToy-AI-Passport"
ESP32C3_CHIP_ID = 5
MARKER = re.compile(rb"WHALE-ASSET-PACK:([0-9a-f]{64})")
APP_DESC = struct.Struct("<II8s32s32s")
APP_DESC_MAGIC = 0xABCD5432

BEGIN, DATA, END, ABORT, INFO, ROLLBACK = 1, 2, 3, 4, 5, 6
STATUS, INFO_REPLY = 0x81, 0x85
IDLE, CONFIRM, RECEIVING, VERIFYING, DONE, FAILED, CANCELLED, ROLLBACK_CONFIRM = range(8)
MAX_CHUNK = 236
ACK_BYTES = 2048
WINDOW_BYTES = 4096
INFO_PENDING, INFO_ROLLBACK, INFO_ASSETS = 1, 2, 4

CODES = {
    0: "ok", 1: "busy", 2: "bad packet", 3: "image size not accepted",
    4: "declined on the device", 5: "flash write failed", 6: "SHA-256 mismatch",
    7: "image rejected (not this project or version mismatch)", 8: "offset rewind",
    9: "image built for a different asset pack; flash the full image over USB",
    10: "no previous firmware to roll back to", 11: "timed out", 12: "BLE link lost",
}
STATES = {
    IDLE: "idle", CONFIRM: "waiting for confirmation on the device", RECEIVING: "receiving",
    VERIFYING: "verifying", DONE: "done", FAILED: "failed", CANCELLED: "cancelled",
    ROLLBACK_CONFIRM: "waiting for rollback confirmation on the device",
}


class OtaError(RuntimeError):
    pass


@dataclass(frozen=True)
class Image:
    data: bytes
    version: str
    sha256: str
    asset_pack: str

    @property
    def asset_id(self) -> bytes:
        return bytes.fromhex(self.asset_pack)[:8]


@dataclass(frozen=True)
class DeviceInfo:
    pending: bool
    rollback: bool
    assets_ok: bool
    max_size: int
    asset_id: bytes
    version: str
    other_version: str


def parse_image(data: bytes) -> Image:
    """Validate an application image (not the merged full image) for OTA."""
    if len(data) < 0x20 + APP_DESC.size or data[0] != 0xE9:
        raise OtaError("not an ESP application image (the 8 MB full image is USB-only)")
    if struct.unpack_from("<H", data, 12)[0] != ESP32C3_CHIP_ID:
        raise OtaError("image is not built for ESP32-C3")
    magic, _, _, version, project = APP_DESC.unpack_from(data, 0x20)
    text = lambda raw: raw.split(b"\0", 1)[0].decode("ascii", "replace")
    if magic != APP_DESC_MAGIC or text(project) != PROJECT:
        raise OtaError(f"image is not a {PROJECT} application")
    packs = set(MARKER.findall(data))
    if len(packs) != 1:
        raise OtaError("image has no asset-pack marker; firmware older than v1.4 cannot be sent over BLE")
    version_text = text(version)
    if not re.fullmatch(r"[\x21-\x7e]{1,31}", version_text):
        raise OtaError("image version string is not usable")
    return Image(data, version_text, hashlib.sha256(data).hexdigest(), packs.pop().decode())


def begin_packet(image: Image) -> bytes:
    v = image.version.encode("ascii")
    return (struct.pack("<BBI", BEGIN, 1, len(image.data)) + bytes.fromhex(image.sha256)
            + image.asset_id + bytes([len(v)]) + v)


def data_packet(offset: int, chunk: bytes) -> bytes:
    return struct.pack("<BI", DATA, offset) + chunk


def parse_status(payload: bytes) -> tuple[int, int, int]:
    if len(payload) != 7 or payload[0] != STATUS:
        raise OtaError("malformed status")
    return payload[1], payload[2], struct.unpack_from("<I", payload, 3)[0]


def parse_info(payload: bytes) -> DeviceInfo:
    if len(payload) < 17 or payload[0] != INFO_REPLY or payload[1] != 1:
        raise OtaError("malformed info reply")
    flags = payload[2]
    (max_size,) = struct.unpack_from("<I", payload, 3)
    asset = payload[7:15]
    n = payload[15]
    version = payload[16:16 + n].decode("ascii", "replace")
    rest = payload[16 + n:]
    other = rest[1:1 + rest[0]].decode("ascii", "replace") if rest else ""
    return DeviceInfo(bool(flags & INFO_PENDING), bool(flags & INFO_ROLLBACK),
                      bool(flags & INFO_ASSETS), max_size, asset, version, other)


def describe(code: int) -> str:
    return CODES.get(code, f"error {code}")


class Session:
    """One OTA conversation over a connected, paired bleak client."""

    def __init__(self, client: Any, clock: Callable[[], float] = time.monotonic) -> None:
        self.client = client
        self.clock = clock
        self.replies: asyncio.Queue[bytes] = asyncio.Queue()
        self.started = False

    async def __aenter__(self) -> "Session":
        await self.client.start_notify(OTA_UUID, lambda _, data: self.replies.put_nowait(bytes(data)))
        self.started = True
        return self

    async def __aexit__(self, *exc: Any) -> None:
        if self.started:
            try:
                await self.client.stop_notify(OTA_UUID)
            except Exception:  # link may already be gone after a restart
                pass

    async def send(self, packet: bytes, response: bool = True) -> None:
        await self.client.write_gatt_char(OTA_UUID, packet, response=response)

    async def reply(self, timeout: float) -> bytes:
        return await asyncio.wait_for(self.replies.get(), timeout)

    async def status(self, timeout: float) -> tuple[int, int, int]:
        while True:
            payload = await self.reply(timeout)
            if payload[:1] == bytes([STATUS]):
                return parse_status(payload)

    async def info(self, timeout: float = 5.0) -> DeviceInfo:
        await self.send(bytes([INFO]))
        while True:
            payload = await self.reply(timeout)
            if payload[:1] == bytes([INFO_REPLY]):
                return parse_info(payload)

    async def push(self, image: Image, progress: Callable[[str, int, int], None] = lambda *a: None,
                   confirm_timeout: float = 70.0) -> DeviceInfo:
        info = await self.info()
        if info.asset_id != image.asset_id:
            raise OtaError(describe(9))
        if len(image.data) > info.max_size:
            raise OtaError(f"image is {len(image.data)} bytes; the OTA slot holds {info.max_size}")
        if info.pending:
            raise OtaError("the running firmware has not finished its self-test; reconnect and retry")
        total = len(image.data)
        await self.send(begin_packet(image))
        state, code, _ = await self.status(5.0)
        if state != CONFIRM:
            raise OtaError(describe(code))
        progress("confirm", 0, total)
        state, code, _ = await self.status(confirm_timeout)
        if state != RECEIVING:
            raise OtaError(describe(code) if code else STATES.get(state, "refused"))
        acked = sent = stalls = 0
        mtu = int(getattr(self.client, "mtu_size", 247) or 247)
        chunk = max(16, min(MAX_CHUNK, mtu - 3 - 5))
        try:
            while acked < total:
                while sent < total and sent - acked < WINDOW_BYTES:
                    piece = image.data[sent:sent + chunk]
                    await self.send(data_packet(sent, piece), response=False)
                    sent += len(piece)
                try:
                    state, code, offset = await self.status(10.0)
                except asyncio.TimeoutError:
                    # The tail of a window was dropped, so no later chunk could
                    # reveal the gap. Resend from the last ack; the device
                    # answers with a rewind to its real offset if needed.
                    stalls += 1
                    if stalls > 3:
                        raise
                    sent = acked
                    continue
                stalls = 0
                if state != RECEIVING:
                    raise OtaError(describe(code))
                if code == 8:  # rewind: a write without response was dropped
                    sent = acked = offset
                elif code == 0:
                    if offset < acked or offset > sent:
                        raise OtaError("device acknowledged an unexpected offset")
                    acked = offset
                progress("sending", acked, total)
        except (asyncio.TimeoutError, OtaError):
            try:
                await self.send(bytes([ABORT]))
            except Exception:
                pass
            raise
        progress("verifying", total, total)
        await self.send(bytes([END]))
        while True:
            state, code, _ = await self.status(60.0)
            if state == DONE:
                progress("done", total, total)
                return info
            if state not in (VERIFYING, RECEIVING):  # late rewind replies are stale
                raise OtaError(describe(code))

    async def rollback(self, confirm_timeout: float = 70.0) -> None:
        await self.send(bytes([ROLLBACK]))
        state, code, _ = await self.status(5.0)
        if state != ROLLBACK_CONFIRM:
            raise OtaError(describe(code))
        state, code, _ = await self.status(confirm_timeout)
        if state != DONE:
            raise OtaError(describe(code) if code else STATES.get(state, "refused"))


def has_ota(client: Any) -> bool:
    services = getattr(client, "services", None)
    try:
        return bool(services and services.get_characteristic(OTA_UUID))
    except Exception:
        return False
