# v1.3.1 release record (in preparation)

[简体中文](RELEASE-v1.3.1.zh_CN.md)

A stability update on top of v1.3.0. Protocol (still 2.2), partition layout and UI are unchanged.

1. Fix the `whale_wire.c` fragment-count comparison that fails under GCC 13 `-Wsign-compare` (`11u/12u`). CI now runs the firmware host tests with both Clang and GCC.
2. Bridge reconnects use exponential backoff: 1, 2, 4 … seconds with ±20 % jitter, clamped to 60 s after jitter. A link that stayed up for 30 s starts again from 1 s. A declined pairing still waits a fixed 60 s.
3. `/whale` shows the disconnect count, the last disconnect reason and time, the last reconnect duration and, while disconnected, the remaining retry delay. During backoff the bridge sends a heartbeat every 2 s, so waiting is not mistaken for an offline bridge.
4. New hardware stress script `harness/tools/whale_stress.py`: N record→transcribe rounds and N disconnect/reconnect cycles (every few cycles stay down for 7 s so the device plays its disconnect cue), summarising transcription and reconnect times plus heap and per-task stack headroom, crashes or unexpected reboots from the serial log.
5. Firmware writes serial-only health lines (`whale_diag`, `whale_ble`): every 30 s and on BLE link up/down it logs free heap, minimum-ever heap and stack headroom of the main, peripheral, LVGL and NimBLE tasks; it also logs played cues and BLE disconnect reasons. No session IDs, titles or text.

## Running the stress test

Start Harness with the v1.3.1 bridge (or run `harness_ble_bridge.py` directly). Once the device is connected:

```
python harness/tools/whale_stress.py --voice-rounds 5 --reconnects 10 --serial COM5 --out stress.json
```

- Voice rounds: record one utterance on the device when prompted; discard the draft on the device instead of sending it.
- Reconnect rounds: the script asks the bridge over loopback to drop the link once and measures the time until the v2.2 handshake is complete; a physical BLE connection alone does not pass. `--cue-every 3` keeps every third drop down for 7 s to trigger the disconnect cue; `0` disables it.
- `--serial` is optional and needs `pyserial` (`installer/requirements-flash.txt`). The port is opened with DTR/RTS released so the device is not reset. Do not run it while flashing.
- Exit status is 1 if any round failed, requested rounds remain incomplete, or the serial log shows a crash or reboot. Ctrl+C preserves completed rounds and returns 130 with `status: interrupted`. Reports include requested/completed counts; only a complete successful run has `status: passed` and exit code 0. The UDP socket and serial monitor are closed on exit.

## Status

Build: not built. The full-image SHA-256 in `installer/firmware/manifest.json` is `pending-v1.3.1-build`, so the standard installer refuses every image; before publishing, build this source with ESP-IDF 5.5.3 and record the real hash. Host tests: see the PR. Device tests: none yet. Unverified: real-device results of the stress run itself.

Firmware: `whale-v1.3.1`

### Reconnect and stress-tool regression checks

The follow-up desktop-only correction passes 82 Python application tests, Python compilation,
plugin bundling (esbuild 0.25.12), the firmware static/host gate and all three Python-to-C
replay scenarios. Tests cover the post-jitter cap, heartbeat/countdown throughout a 60 s
backoff, incomplete handshake rejection and interrupted runs with preserved partial results.
Windows symlink-dependent infrastructure checks are skipped by the host gate. No firmware
source changed in this correction; no new full image was built and no device stress run was
performed. Real radio timing and on-device recording remain unverified.
