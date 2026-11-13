"""Reconnect backoff, disconnect accounting and the loopback stress-test hooks."""
import asyncio
import json
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness/bridge"))
import harness_ble_bridge as bridge
import whale_v2 as v


class Clock:
    def __init__(self):
        self.now = 100.0

    def __call__(self):
        return self.now


class BackoffTests(unittest.TestCase):
    def stats(self, rand=0.5):
        clock = Clock()
        return clock, bridge.LinkStats(clock=clock, wall=lambda: 1_700_000_000.0, rand=lambda: rand)

    def test_failed_attempts_double_up_to_the_cap(self):
        _, link = self.stats()
        delays = [link.lost("not-found") for _ in range(10)]
        self.assertEqual(delays[:4], [1.0, 2.0, 4.0, 8.0])
        self.assertEqual(max(delays), bridge.RECONNECT_MAX_SECONDS)
        self.assertEqual(link.disconnects, 0, "a scan miss is not a disconnect")

    def test_jitter_stays_within_twenty_percent(self):
        for rand, expected in ((0.0, 0.8), (1.0, 1.2)):
            _, link = self.stats(rand)
            self.assertAlmostEqual(link.lost("not-found"), expected)

    def test_jitter_never_exceeds_sixty_seconds(self):
        for rand in (0.0, 0.5, 1.0):
            _, link = self.stats(rand)
            delays = [link.lost("not-found") for _ in range(20)]
            self.assertTrue(all(0 < d <= 60 for d in delays), delays)

    def test_retry_countdown_uses_elapsed_time_and_clears_on_connection(self):
        clock, link = self.stats()
        link.hold(60)
        clock.now += 17.5
        self.assertEqual(link.fields()["retryIn"], 42.5)
        clock.now += 60
        self.assertEqual(link.fields()["retryIn"], 0)
        link.up()
        self.assertNotIn("retryIn", link.fields())

    def test_drop_counts_reason_and_reconnect_time(self):
        clock, link = self.stats()
        link.up()
        clock.now += 5
        link.lost("BleakError")
        self.assertEqual(link.disconnects, 1)
        self.assertEqual(link.fields()["lastDisconnect"], "BleakError")
        self.assertEqual(link.fields()["lastDisconnectAt"], 1_700_000_000_000)
        clock.now += 2.5
        link.up()
        self.assertEqual(link.fields()["lastReconnectMs"], 2500)
        self.assertNotIn("retryIn", link.fields())

    def test_short_lived_links_keep_backing_off(self):
        clock, link = self.stats()
        delays = []
        for _ in range(4):
            link.up()
            clock.now += 3
            delays.append(link.lost("link-lost"))
        self.assertEqual(delays, [1.0, 2.0, 4.0, 8.0])

    def test_stable_link_and_manual_cycle_restart_from_base(self):
        clock, link = self.stats()
        for _ in range(5):
            link.lost("not-found")
        link.up()
        clock.now += bridge.RECONNECT_STABLE_SECONDS
        self.assertEqual(link.lost("link-lost"), 1.0)
        for _ in range(5):
            link.lost("not-found")
        self.assertEqual(link.lost("stress-test", manual=True), 1.0)

    def test_pairing_hold_does_not_grow_backoff(self):
        _, link = self.stats()
        self.assertEqual(link.hold(60), 60)
        self.assertEqual(link.fields()["retryIn"], 60)
        self.assertEqual(link.lost("not-found"), 1.0)


class LoopbackHooks(unittest.TestCase):
    def test_question_diagnostics_expose_counts_only(self):
        model = bridge.StatusModel()
        sent = []
        model.datagram_transport = SimpleNamespace(sendto=lambda data, addr: sent.append(json.loads(data)))
        model.workspace.update({"sessionId": "synthetic-private-id", "runId": "r", "state": "waiting"})
        protocol = bridge.HarnessDatagram(model)
        protocol.datagram_received(json.dumps({"kind": "harness-question", "sessionId": "synthetic-private-id",
            "runId": "r", "requestId": "synthetic-request", "mode": "approval", "question": "private prompt",
            "options": ["allow", "deny"]}).encode(), ("127.0.0.1", 5000))
        protocol.datagram_received(b'{"kind":"whale-roster-check"}', ("127.0.0.1", 5000))
        report = sent[-1]
        self.assertEqual(report["questionDiagnostics"],
                         {"received": 1, "approvalReceived": 1, "pending": 1, "pendingApproval": 1})
        self.assertNotIn("private", json.dumps(report))

    def test_status_report_has_counters_only(self):
        model = bridge.StatusModel()
        sent = []
        model.datagram_transport = SimpleNamespace(sendto=lambda data, addr: sent.append(json.loads(data)))
        protocol = bridge.HarnessDatagram(model)
        protocol.datagram_received(b'{"kind":"whale-link-status"}', ("127.0.0.1", 5000))
        self.assertEqual(sent[0]["kind"], "whale-link-status")
        self.assertEqual(set(sent[0]), {"kind", "connected", "state", "packetsSent", "disconnects"})

    def test_cycle_request_is_loopback_only(self):
        model = bridge.StatusModel()
        protocol = bridge.HarnessDatagram(model)
        protocol.datagram_received(b'{"kind":"whale-link-cycle"}', ("192.168.1.9", 5000))
        self.assertFalse(model.link_cycle.is_set())
        protocol.datagram_received(b'{"kind":"whale-link-cycle"}', ("127.0.0.1", 5000))
        self.assertTrue(model.link_cycle.is_set())


class LinkCycleLoop(unittest.IsolatedAsyncioTestCase):
    async def test_long_backoff_keeps_heartbeat_and_countdown_alive(self):
        clock = Clock()
        model = bridge.StatusModel()
        model.link = bridge.LinkStats(clock=clock, rand=lambda: 0.5)
        model.link.attempt = 6  # Exercise the actual 60-second wait without wall-clock sleep.
        reports = []
        real_sleep = asyncio.sleep

        async def sleep(seconds):
            clock.now += seconds
            await real_sleep(0)

        scans = 0
        async def scan(*args, **kwargs):
            nonlocal scans
            scans += 1
            if scans > 1:
                raise asyncio.CancelledError
            return None

        telemetry = SimpleNamespace(sendto=lambda data, _: reports.append((clock.now, json.loads(data))))
        fake = SimpleNamespace(BleakScanner=SimpleNamespace(find_device_by_name=scan), BleakClient=None)
        with patch.dict(sys.modules, bleak=fake), patch.object(bridge.asyncio, "sleep", sleep):
            with self.assertRaises(asyncio.CancelledError):
                await bridge.ble_loop(model, "fixture", None, 0.01, telemetry, 0)
        waiting = [(at, r) for at, r in reports if r['state'] == 'not-found']
        self.assertEqual(waiting[0][1]['retryIn'], 60)
        self.assertEqual(waiting[-1][1]['retryIn'], 0)
        self.assertEqual(waiting[-1][0] - waiting[0][0], 60)
        self.assertTrue(all(b[0]-a[0] < 7 for a, b in zip(waiting, waiting[1:])))
        self.assertTrue(all(not r['connected'] for _, r in waiting))
        self.assertNotIn('retryIn', reports[-1][1])  # Next scan is no longer in backoff.

    async def cycle_reconnect(self, paired):
        model = bridge.StatusModel()
        connects = []
        device = object()
        scans = []
        targets = []

        async def lookup(*_):
            return device if paired else None

        async def scan(*_, **__):
            scans.append(True)
            return device if len(scans) == 1 else None

        class Client:
            mtu_size = 23
            services = SimpleNamespace(get_service=lambda _: True)

            def __init__(self):
                self.is_connected = True
                self.stopped = []

            async def __aenter__(self):
                connects.append(self)
                return self

            async def __aexit__(self, *exc):
                self.is_connected = False

            async def start_notify(self, _, callback):
                self.notify = callback

            async def stop_notify(self, uuid):
                self.stopped.append(uuid)

            async def write_gatt_char(self, _, data, response):
                if data[1] == 1 and data[4] == 0:
                    for kind, body, serial in [
                        (2, v.pack("BBIHHB", 2, 2, 511, 20, 20, 0), 1),
                        (0x10, v.pack("QIBB", 0, 0, 8, 0), 2),
                    ]:
                        for frame in v.fragments(kind, serial, body):
                            self.notify(None, frame)

        reports = []

        class Telemetry:
            def sendto(self, data, addr):
                reports.append(json.loads(data))

        def client(target, **options):
            targets.append(target)
            if sys.platform == "win32":
                self.assertEqual(options, {"winrt": {"use_cached_services": False}})
            return Client()

        fake = SimpleNamespace(BleakScanner=SimpleNamespace(find_device_by_name=scan),
                               BleakClient=client)

        async def pair_client(_):
            pass

        async def until(predicate):
            while not predicate():
                await asyncio.sleep(0.01)

        with patch.dict(sys.modules, bleak=fake), patch.object(bridge, "pair_passport", pair_client), \
                patch.object(bridge, "paired_passport_device", lookup), \
                patch.object(bridge, "RECONNECT_BASE_SECONDS", 0.05):
            task = asyncio.create_task(
                bridge.ble_loop(model, "fixture", None, 0.01, Telemetry(), 0))
            try:
                await asyncio.wait_for(until(lambda: model.link.state == "v2.2"), 4)
                model.link_cycle.set()
                await asyncio.wait_for(until(lambda: len(connects) == 2 and model.link.state == "v2.2"), 4)
            finally:
                task.cancel()
                await asyncio.gather(task, return_exceptions=True)
        self.assertFalse(connects[0].is_connected)
        self.assertEqual(connects[0].stopped, [v.NOTIFY])
        report = model.link_report()
        self.assertEqual(report["disconnects"], 1)
        self.assertEqual(report["lastDisconnect"], "stress-test")
        self.assertIn("lastReconnectMs", report)
        self.assertTrue(any(r.get("disconnects") == 1 and r["state"] == "v2.2" for r in reports))
        self.assertEqual(len(scans), 0 if paired else 1)
        self.assertTrue(all(target is device for target in targets))

    async def test_stress_cycle_reuses_discovered_peer_without_second_advertisement(self):
        await self.cycle_reconnect(paired=False)

    async def test_paired_peer_connects_and_reconnects_without_advertising(self):
        await self.cycle_reconnect(paired=True)

    async def test_failed_audio_subscription_releases_control_subscription(self):
        model = v.Workspace()
        model.marks_path = Path("synthetic-marks")
        disconnected = []
        model.voice = SimpleNamespace(disconnected=lambda: disconnected.append(True), feed=lambda _: None)
        stopped = []

        class Client:
            is_connected = True
            services = SimpleNamespace(get_characteristic=lambda _: object())

            async def start_notify(self, uuid, _):
                if uuid != v.NOTIFY:
                    raise RuntimeError("synthetic audio subscription failure")

            async def stop_notify(self, uuid):
                stopped.append(uuid)

        with self.assertRaisesRegex(RuntimeError, "synthetic audio"):
            await v.serve(Client(), model)
        self.assertEqual(stopped, [v.NOTIFY])
        self.assertEqual(disconnected, [True])


if __name__ == "__main__":
    unittest.main()
