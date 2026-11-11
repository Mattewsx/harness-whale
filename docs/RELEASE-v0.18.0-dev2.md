[简体中文](RELEASE-v0.18.0-dev2.zh_CN.md)

# DeepSeek Harness Whale Companion v0.18.0-dev2

Compared with the public v0.17.0-demo:

1. Answer waiting questions across sessions in arrival order. Accepted answers open the next question automatically. Read-but-unanswered questions remain available without restoring cleared unread badges.
2. The whale girl celebrates finished work and looks anxious when questions wait. Reading, recording and interactions keep their current screen. These expressions reuse existing character poses.
3. Add arrows and common arithmetic symbols so supported questions do not fall back to the computer because of missing glyphs.
4. Keep the back/defer control clear of session titles. Long titles are shortened.

## Use and upgrade

Download this repository’s main-branch source ZIP, extract it, run `python installer/install_companion.py --non-interactive --skip-key`, then fully quit and reopen DSH. Update the computer components for continuous answers; older bridges fall back to the ordinary list. No Wi-Fi setup is needed. See [README](../README.md) for pairing and controls.

The full image flashes at 0x0 and resets settings and pairing. With a verified matching partition layout, follow [upgrade guidance](UPGRADE.md) to write only the matching application at 0x10000 and preserve stored data. Never write the application image at 0x0.

## Validation scope

- Build: PASS, complete ESP-IDF 5.5.3 gate, merged-image and matching debug-archive verification.
- Host tests: PASS, 132 computer-side tests, font coverage, native LVGL question rendering and independent code review.
- Device tests: PASS within this iteration’s scope. The exact application below was flashed; boot identity and Bluetooth reconnect passed. The user confirmed question rendering, back/defer navigation, actual answer submission, automatic next-question progression and retained affection/outfit/settings.
- Unverified: emotion presentation, long-duration behavior, daily rollover and every outfit/action combination were not retested on the device in this iteration.

## Build identity

- Full image: `c5f97c1388eab812ad42175d44a78e971f0b3963a3ba91273ba02afbf3afdb2a`
- Application: `0642fe8619d5f61babbd81661238041423164f4d6fde7947b1bc1bee559114f9`
- ELF: `85bbf36f724db8b68b616c6e419279da4234bb014932ecb176c41acfcfd658fc`

Inputs are pinned in [release-source.json](release-source.json). The community target is existing project 1966; moderation status follows the server receipt. No GitHub Release is created in this operation.
