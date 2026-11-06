# v1.4.0 integration candidate

**English** · [简体中文](RELEASE-v1.4.0.zh_CN.md)

This candidate combines power saving, corrected battery-life recording, the desk clock and protocol 2.3 large frames. It is not a public release. OTA is excluded; firmware and embedded artwork/audio update together over USB using the existing single-app partition layout.

## Included changes

1. Idle home or companion pages enter the desk display after two minutes when the global work summary is clear, or while offline. The display shows a large clock, whale, date and battery; 23:00–07:00 uses the dark clock layout. Any key returns home without activating the next page. Work or attention returns to its task or list without clearing unread flags.
2. The normal two-minute dim and five-minute screen-off still apply. Offline devices slow advertising after 30 seconds and sleep after ten minutes with the screen off. Timer wakes look for the computer, and keys wake immediately. See [power behaviour](POWER.md).
3. Cumulative recharge starts a new battery-log run without repeatedly replacing the prior discharge during continuous charging. The desktop analyser excludes recharge intervals in both new and legacy logs. SOC alone cannot distinguish flat USB-powered time; battery measurement still requires unplugging USB.
4. Protocol 2.3 negotiates up to 244-byte frames (236 payload bytes at MTU 247), with legacy 20-byte fallback. `/whale` reports the negotiated frame and MTU. This changes the command/reply transport; microphone audio retains its existing channel.
5. The bridge syncs the clock after the first snapshot and every 30 minutes. RTC time survives deep sleep; a power cycle needs another computer sync. Old bridges cannot prove global idle, so an offline desk display returns home after reconnecting to one; manual companion interactions remain available.

## Integration sources

- Power: `9f0793286254c019d61546d6bbc946a5635a8d5e` (PR #5), plus local battery fixes.
- Desk clock: `4179a7d0e226aea3612650422b8c6f229a35db05` (PR #8).
- Large frames: `98c836df938b3df60ee057c3112dec8d1f9b2690` (PR #6).
- OTA PR #7 is closed without merging. No OTA slots, asset partition, OTA characteristic or OTA uploader is included.

## Validation

Build: PASS — complete ESP-IDF 5.5.3 gate, merged image and matching debug archive verified. Host tests: PASS — 99 Python tests, native firmware tests, six cross-language replay scenarios, LVGL rendering, font coverage, Python compilation and plugin bundling. Some Windows symlink/POSIX-dependent infrastructure tests are skipped; the integrated snapshot has not run in Linux CI. Device tests: the prior image `48ec4027b3e8d0f8eaafc8066b0741722a738d1ff1795793b75cf7d1495ea935` passed basic acceptance: application-only USB upgrade preserved NVS; startup and automatic BLE reconnect succeeded, with 244-byte frames negotiated. The user confirmed screen/list, latest reply reading, recording/transcription/cancel, idle desk clock and correct time, key return without a second action, two-minute dim and five-minute screen-off/wake. Unverified: measured BLE throughput, offline deep sleep and key/timer wake, clock continuity through deep sleep, hardware work-event interruption, actual voice send and cue playback in this build, unplugged battery life and long-duration stability. The build identified below additionally fixes battery-history preservation when charging continues across a clock reset. Its regression tests and full build gate passed, but it has not been flashed. Prior hardware acceptance does not cover this exact image. This is scoped acceptance, not a completed release.

Firmware: `whale-v1.4.0`. Use the matching desktop package and restart DSH after installation. Full images flash at `0x0` and may clear pairing/settings. A compatible app-only USB upgrade at `0x10000` preserves NVS after verifying the device layout. Flashing needs separate approval.

## Build identity

Full image SHA-256: `33fcc437da6950d3a78c77b886ab9ab2e2975f063c84bf0ae19ff955b954a3ae`

ELF SHA-256: `2d1d5b4fede21657becaefdbd7a3535199b60888e6d7e9c81fd36a83a565e48c`
