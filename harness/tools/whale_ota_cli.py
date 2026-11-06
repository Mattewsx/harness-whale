#!/usr/bin/env python3
"""Send a firmware update to the Harness Whale passport over BLE (v1.4+).

Normally the running bridge does the transfer on its existing paired link:

    python harness/tools/whale_ota_cli.py Harness-Whale-v1.4.0-ota.bin
    python harness/tools/whale_ota_cli.py --info
    python harness/tools/whale_ota_cli.py --rollback

With --direct the tool connects itself (stop the bridge first; the device
accepts one connection). The device always asks for confirmation on screen.
Only the application image is accepted; the 8 MB full image stays USB-only.
"""

from __future__ import annotations

import argparse
import asyncio
import json
from pathlib import Path
import socket
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "harness" / "bridge"))
import whale_ota  # noqa: E402

DEFAULT_PORT = 8765


def manifest_sha(image: Path, manifest: Path | None) -> str | None:
    if manifest is None:
        candidate = image.with_name("manifest.json")
        if not candidate.is_file():
            return None
        manifest = candidate
    ota = json.loads(manifest.read_text(encoding="utf-8")).get("ota") or {}
    if ota.get("file") != image.name:
        return None
    sha = ota.get("sha256", "")
    if len(sha) != 64:
        raise SystemExit(f"manifest has no released SHA-256 for {image.name}")
    return sha


def show(doc: dict) -> None:
    phase = doc.get("phase")
    if phase == "start":
        print(f"Image {doc['version']} ({doc['size']} bytes, sha256 {doc['sha256'][:16]}…)")
    elif phase == "confirm":
        print("Confirm on the device: short press OK to install, long press to cancel.")
    elif phase == "sending":
        done, total = doc.get("done", 0), max(1, doc.get("total", 1))
        print(f"\rSending {done * 100 // total:3d}%  {done}/{total} bytes", end="", flush=True)
    elif phase == "verifying":
        print("\nVerifying on the device…")
    elif phase == "done":
        print("Done. The device restarts into the new firmware.")
    elif phase == "info":
        print(f"Running:  {doc['version']}{'  (self-test pending)' if doc['pending'] else ''}")
        print(f"Other slot: {doc['otherVersion'] or '-'}{'  (rollback available)' if doc['rollback'] else ''}")
        print(f"Asset pack: {doc['assetId']}… {'ok' if doc['assetsOk'] else 'MISSING/MISMATCHED'}")
        print(f"Max OTA image: {doc['maxSize']} bytes")
    elif phase == "error":
        print(f"\nOTA failed: {doc.get('error')}", file=sys.stderr)


def via_bridge(request: dict, port: int, first_reply_timeout: float) -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    sock.sendto(json.dumps({"kind": "whale-ota", **request}).encode(), ("127.0.0.1", port))
    timeout = first_reply_timeout
    while True:
        sock.settimeout(timeout)
        try:
            data, _ = sock.recvfrom(65536)
        except socket.timeout:
            print("\nNo answer from the bridge. Is it running and connected to HARNESS-WHALE?"
                  " (or stop it and use --direct)", file=sys.stderr)
            return 2
        doc = json.loads(data.decode("utf-8"))
        if doc.get("kind") != "whale-ota-status":
            continue
        show(doc)
        phase = doc.get("phase")
        if phase == "error":
            return 1
        if phase in ("done", "info"):
            return 0
        timeout = 120.0 if phase in ("start", "confirm") else 30.0


async def direct(request: dict, image: whale_ota.Image | None, name: str) -> int:
    from bleak import BleakClient, BleakScanner
    from whale_pairing import pair_passport

    device = await BleakScanner.find_device_by_name(name, timeout=10.0)
    if device is None:
        print(f"{name} not found", file=sys.stderr)
        return 2
    async with BleakClient(device) as client:
        await pair_passport(client)
        if not whale_ota.has_ota(client):
            print("Device firmware has no BLE OTA; flash v1.4 over USB first.", file=sys.stderr)
            return 1
        async with whale_ota.Session(client) as session:
            try:
                if request["action"] == "info":
                    i = await session.info()
                    show({"phase": "info", "version": i.version, "otherVersion": i.other_version,
                          "pending": i.pending, "rollback": i.rollback, "assetsOk": i.assets_ok,
                          "assetId": i.asset_id.hex(), "maxSize": i.max_size})
                elif request["action"] == "rollback":
                    show({"phase": "confirm"})
                    await session.rollback()
                    show({"phase": "done"})
                else:
                    assert image is not None
                    show({"phase": "start", "version": image.version, "size": len(image.data),
                          "sha256": image.sha256})
                    await session.push(image, lambda p, d, t: show(
                        {"phase": p, "done": d, "total": t}))
            except (whale_ota.OtaError, asyncio.TimeoutError) as exc:
                show({"phase": "error", "error": str(exc) or type(exc).__name__})
                return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image", nargs="?", type=Path, help="application image (…-ota.bin)")
    parser.add_argument("--info", action="store_true", help="show running/other slot versions")
    parser.add_argument("--rollback", action="store_true", help="switch back to the other slot")
    parser.add_argument("--manifest", type=Path, help="manifest.json with the released OTA hash")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="bridge UDP port")
    parser.add_argument("--direct", action="store_true", help="connect without the bridge")
    parser.add_argument("--device-name", default="HARNESS-WHALE")
    args = parser.parse_args()
    if sum(map(bool, (args.image, args.info, args.rollback))) != 1:
        parser.error("give exactly one of: IMAGE, --info, --rollback")
    image = None
    if args.info:
        request = {"action": "info"}
    elif args.rollback:
        request = {"action": "rollback"}
    else:
        path = args.image.expanduser().resolve()
        try:
            image = whale_ota.parse_image(path.read_bytes())
        except (OSError, whale_ota.OtaError) as exc:
            parser.error(str(exc))
        expected = manifest_sha(path, args.manifest)
        if expected and expected != image.sha256:
            print("Image SHA-256 does not match the manifest; refusing to send.", file=sys.stderr)
            return 1
        request = {"action": "push", "path": str(path), "sha256": image.sha256}
    started = time.monotonic()
    code = (asyncio.run(direct(request, image, args.device_name)) if args.direct
            else via_bridge(request, args.port, 20.0))
    if code == 0 and request["action"] != "info":
        print(f"Finished in {time.monotonic() - started:.0f} s. After the restart the device"
              " keeps the new firmware only once it reconnects; check with --info.")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
