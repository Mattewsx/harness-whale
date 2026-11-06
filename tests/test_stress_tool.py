"""The stress script's log parsing and its loopback exchange with the bridge."""
import asyncio
import sys
import threading
import unittest
from pathlib import Path

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
            loop.close()

        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        self.assertTrue(ready.wait(5))
        try:
            client = stress.Bridge(state["port"])
            status = client.status()
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


if __name__ == "__main__":
    unittest.main()
