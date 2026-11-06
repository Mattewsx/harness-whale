"""The stress script's log parsing and its loopback exchange with the bridge."""
import asyncio
import contextlib
import io
import json
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "harness/bridge"))
sys.path.insert(0, str(ROOT / "harness/tools"))
import harness_ble_bridge as bridge
import whale_stress as stress


class SerialParsing(unittest.TestCase):
    def monitor(self):
        monitor = stress.SerialMonitor.__new__(stress.SerialMonitor)
        monitor.lock = threading.Lock()
        monitor.minimum = {}
        monitor.cues = {"played": 0, "skipped": 0}
        monitor.ble_reasons = {}
        monitor.voice_results = {}
        monitor.crashes = []
        monitor.samples = 0
        return monitor

    def test_keeps_lowest_headroom_and_counts_events(self):
        m = self.monitor()
        for line in [
            "I (1000) whale_diag: why=periodic heap_free=91000 heap_min=80000 stack_main=1200 "
            "stack_periph=0 stack_lvgl=2100 stack_ble=1500",
            "I (2000) whale_diag: why=link-down heap_free=90000 heap_min=78000 stack_main=1100 "
            "stack_periph=9000 stack_lvgl=2200 stack_ble=1400",
            "I (2100) whale_ble: disconnected reason=0x213",
            "I (8000) whale_diag: cue kind=4 played=1",
            "I (9000) whale_voice: capture result=2 stage=finish_ack error=0 duration_ms=1800 "
            "max_encode_us=9000 min_heap=76000 stack_min_bytes=3000",
            "I (9500) whale_diag: voice phase=4",
            "I (9600) whale: free text that should be ignored",
        ]:
            m.feed(line)
        self.assertEqual(m.samples, 2)
        self.assertEqual(m.minimum, {"heap_free": 90000, "heap_min": 76000, "stack_main": 1100,
                                     "stack_periph": 9000, "stack_lvgl": 2100, "stack_ble": 1400,
                                     "stack_voice": 3000})
        self.assertEqual(m.cues, {"played": 1, "skipped": 0})
        self.assertEqual(m.ble_reasons, {"0x213": 1})
        self.assertEqual(m.voice_results, {"2": 1})
        self.assertEqual(m.crashes, [])

    def test_detects_crash_and_reboot(self):
        m = self.monitor()
        m.feed("Guru Meditation Error: Core  0 panic'ed (Load access fault)")
        m.feed("ESP-ROM:esp32c3-api1-20210207 rst:0xc (RTC_SW_CPU_RST),boot:0xd")
        self.assertEqual(len(m.crashes), 2)

    def test_spread(self):
        self.assertEqual(stress.spread([300, 100, 200]), {"count": 3, "min": 100, "median": 200, "max": 300})
        self.assertEqual(stress.spread([]), {})


class BridgeExchange(unittest.TestCase):
    def test_status_and_cycle_round_trip(self):
        ready = threading.Event()
        state = {}

        def serve():
            loop = asyncio.new_event_loop()
            model = bridge.StatusModel()

            async def start():
                transport, _ = await loop.create_datagram_endpoint(
                    lambda: bridge.HarnessDatagram(model), local_addr=("127.0.0.1", 0))
                state.update(port=transport.get_extra_info("sockname")[1], model=model,
                             loop=loop, transport=transport)
                ready.set()

            loop.run_until_complete(start())
            loop.run_forever()
            state["transport"].close()
            loop.run_until_complete(asyncio.sleep(0.05))
            loop.close()

        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        self.assertTrue(ready.wait(5))
        try:
            with contextlib.closing(stress.Bridge(state["port"])) as client:
                # UDP may lose a datagram; poll as the real stress runner does.
                status = client.wait(lambda _: True, 5)
                self.assertIsNotNone(status, 'loopback bridge did not reply within 5 s')
                self.assertEqual(status["kind"], "whale-link-status")
                self.assertFalse(stress.ready(status))
                client.cycle(7)
                for _ in range(50):
                    if state["model"].link_cycle.is_set():
                        break
                    threading.Event().wait(0.02)
                self.assertTrue(state["model"].link_cycle.is_set())
                self.assertEqual(state["model"].link_hold, 7)
        finally:
            state["loop"].call_soon_threadsafe(state["loop"].stop)
            thread.join(5)


class StressOutcomes(unittest.TestCase):
    def test_ready_requires_completed_protocol_handshake(self):
        for state in ('connecting', 'connected', 'negotiating', 'reconnecting'):
            self.assertFalse(stress.ready({'connected': True, 'state': state}))
        self.assertFalse(stress.ready({'connected': False, 'state': 'v2.2'}))
        self.assertTrue(stress.ready({'connected': True, 'state': 'v2.2'}))

    def test_reconnect_round_does_not_pass_before_handshake(self):
        class Bridge:
            waits = 0
            def status(self): return {'disconnects': 0}
            def cycle(self, hold): pass
            def wait(self, predicate, timeout):
                self.waits += 1
                if self.waits == 1:
                    status = {'connected': True, 'state': 'v2.2', 'disconnects': 0}
                elif self.waits == 2:
                    status = {'connected': False, 'state': 'reconnecting', 'disconnects': 1}
                else:
                    status = {'connected': True, 'state': 'connected', 'disconnects': 1,
                              'lastReconnectMs': 123}  # Previous connection's stale timing.
                return status if predicate(status) else None
        with contextlib.redirect_stdout(io.StringIO()):
            rounds = stress.reconnect_rounds(Bridge(), 1, 0, 1, 0)
        self.assertEqual(rounds[0]['outcome'], 'timeout')

    def run_cli(self, voice_runner):
        class Bridge:
            closed = False
            def status(self): return {'kind': 'whale-link-status', 'connected': True, 'state': 'v2.2'}
            def close(self): self.closed = True
        client = Bridge()
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)/'report.json'
            argv = ['stress', '--voice-rounds', '2', '--reconnects', '0', '--out', str(out)]
            with patch.object(stress, 'Bridge', return_value=client), \
                 patch.object(stress, 'voice_rounds', side_effect=voice_runner), \
                 patch.object(sys, 'argv', argv), contextlib.redirect_stdout(io.StringIO()), \
                 contextlib.redirect_stderr(io.StringIO()):
                code = stress.main()
            self.assertTrue(client.closed)
            return code, json.loads(out.read_text('utf8'))

    def test_interrupt_preserves_completed_rounds_and_fails(self):
        def interrupt(*args, results):
            results.append({'round': 1, 'outcome': 'draft', 'sttMs': 80})
            raise KeyboardInterrupt
        code, report = self.run_cli(interrupt)
        self.assertEqual(code, 130)
        self.assertEqual(report['status'], 'interrupted')
        self.assertTrue(report['interrupted'])
        self.assertEqual(report['voice']['requested'], 2)
        self.assertEqual(report['voice']['completed'], 1)
        self.assertEqual(report['voice']['ok'], 1)

    def test_interrupt_before_first_round_is_not_success(self):
        def interrupt(*args, **kwargs): raise KeyboardInterrupt
        code, report = self.run_cli(interrupt)
        self.assertEqual(code, 130)
        self.assertEqual(report['voice']['completed'], 0)

    def test_early_return_is_incomplete_not_success(self):
        code, report = self.run_cli(lambda *args, **kwargs: None)
        self.assertEqual(code, 1)
        self.assertEqual(report['status'], 'incomplete')

    def test_completed_success_still_passes(self):
        def complete(*args, results):
            results.extend({'round': i, 'outcome': 'draft', 'sttMs': 80} for i in (1, 2))
        code, report = self.run_cli(complete)
        self.assertEqual(code, 0)
        self.assertEqual(report['status'], 'passed')

    def test_voice_loop_retains_partial_results_on_interrupt(self):
        class Bridge:
            waits = 0
            def status(self): return {'voice': {'draft': self.waits}}
            def wait(self, predicate, timeout):
                self.waits += 1
                if self.waits == 2: raise KeyboardInterrupt
                status = {'voice': {'draft': self.waits}, 'lastSttMs': 80}
                assert predicate(status)
                return status
        results = []
        with patch.object(stress.time, 'sleep'), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(KeyboardInterrupt):
                stress.voice_rounds(Bridge(), 2, 1, results=results)
        self.assertEqual(results, [{'round': 1, 'outcome': 'draft', 'sttMs': 80}])


if __name__ == "__main__":
    unittest.main()
