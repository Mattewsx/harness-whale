# DeepSeek Harness Whale Companion v1.2.0

[简体中文](RELEASE-v1.2.0.zh_CN.md)

This release pairs the v1.2.0 desktop companion with firmware `whale-R11.4-2.2`
and protocol 2.2. It retains the exact R11.4 binary that passed the build gate and
short-recording acceptance; the firmware was not rebuilt merely to rename it.

## Changes

- Hold Up on task home to record; Confirm stops. Review the transcript and explicitly
  choose Send, Record again, or Cancel. Audio uploads over Bluetooth while speaking
  and is recognized locally on the computer after stopping.
- The session list shows names and colored states, prioritizing questions, unread
  errors and unread replies. Reading the relevant content clears its attention marker.
- Task home emphasizes the whale, title, status and elapsed time. Only the latest
  reply is retained, with continuous scrolling and an in-memory segment cache.
- Completion chime uses 85% volume for 150 ms and is deferred during recording.
- Capture startup, encoder stack and graphics-memory allocation fixes. Narrow
  terminology correction maps `deep sick` / `deep seek` to `DeepSeek` in explicit
  Chinese product commands; this is not a general English accuracy improvement.

## Installation

Use source from this tag or `Harness-Whale-v1.2.0-companion.zip`. Run
`python installer/install_companion.py --non-interactive --skip-key` and fully restart
DSH. Enable its local SenseVoice recognizer and wait for model preparation. Match
and confirm pairing codes on both sides. If an older Bluetooth binding lacks the
audio service, remove both bindings and pair again as described in the deployment guide.

Flash the full `Harness-Whale-ESP32C3-8MB.bin` at **0x0** for a fresh installation;
this may clear settings and pairing. Compatible application-only upgrades use
**0x10000**, after checking the partition layout. Publication does not authorize flashing.

## Validation

- Build: existing full ESP-IDF 5.5.3 gate passed. Build inputs, merged layout and matching
  ELF archive were verified again for this release.
- Host tests: 49 desktop regressions, voice-preview and terminology cases, plugin
  bundling and Python compilation passed. R11.4's firmware gate also passed native
  tests, cross-language replay, font and LVGL rendering checks.
- Device tests: R11.4 startup, list presentation, short recording/transcription and
  the latest terminology correction were confirmed by the user.
- Unverified: sustained/repeated recordings, measured encoding time and stack margin,
  confirmed delivery into a real target session, and completion audio after recording.

Full-image SHA-256: `2ade37872b9f61ff9e749282a6e74bc6d162ff13f642dac34e920c5cb53d3825`.
Matching ELF SHA-256: `9dcf55a630d438411d41d942951d44f8a22fe0ebf210623dca7a88a3c3ddec3f`.
See `SHA256SUMS.txt` for release assets. No private recordings, real sessions or
credentials are included.
