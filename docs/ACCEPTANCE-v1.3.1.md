# v1.3.1 device acceptance — 2026-11-06

Full image SHA-256: `068eecc60daf5c4739cabac84a5eb906b55550f217b851d252917eca53e8e660`

Application SHA-256: `907f3c1733027414dac659f88ade3e9cde4ff8107016c0025402ca2a32e17512`

The original device was identified before flashing; its partition table matched the verified archive. Only the application at `0x10000` was written and verified. NVS, pairing and settings were preserved. The matching desktop components were installed, DSH restarted, and the v2.2 handshake confirmed. No rebuild followed device acceptance.

## Results

- Build: PASS. Full ESP-IDF gate and matching merged-image/debug archive verification.
- Host tests: PASS. 82 Python application tests, static/host firmware checks, three cross-language replays, LVGL rendering and plugin compilation. Windows symlink-dependent infrastructure checks were skipped.
- Startup: 40-second observation, version `whale-v1.3.1`, no observed crash or watchdog warning.
- Reconnects: 10/10 completed the v2.2 handshake. Three cycles deliberately held the link down for 7 seconds; two cue playback events were observed. The seven ordinary cycles took 4.000–13.266 seconds, median 4.781 seconds. No crash observed.
- Recording: operator confirmed three normal record → recognize → cancel cycles and successful repeat recording. Automated collection captured two drafts (312 ms and 203 ms transcription) plus one empty result; the operator confirmed an extra empty/quickly stopped attempt. The raw automated run remains `failed` (2/3 draft results), and is not treated as independent evidence of three successful recordings.
- Real send: operator confirmed one real voice message was received once and the session remained normal.
- Observed minimum heap: 39,060 bytes; reported minimum voice-task stack headroom: 2,232 bytes, BLE: 1,276 bytes. No crash or disconnect occurred in the monitored voice window. These measurements do not prove long-term stability.
- Serial monitoring was stopped and the port released.

## Remaining limits

Long-duration stability and a full visual/audio regression were not run for v1.3.1. All three intended recordings were confirmed by the operator; only two were independently counted as drafts by the automatic script. Raw local logs and real session content are not included in the repository.

The original build package remains immutable. Release packaging adds this acceptance record and current documentation while retaining the identical tested firmware and desktop runtime files.
