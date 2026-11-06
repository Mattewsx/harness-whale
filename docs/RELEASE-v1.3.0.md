# v1.3.0 release record

[简体中文](RELEASE-v1.3.0.zh_CN.md)

This release combines voice input and session-list improvements with companion interactions, state sounds and navigation. Community review status is determined by the platform receipt.

1. Add voice input: hold Up on task home to record and stream while speaking. After stopping, your computer transcribes locally; review and confirm before sending.
2. Show only session names and colored states in the list, prioritizing questions, unread errors and new replies.
3. Add whale-girl interactions: greeting, petting, feeding a fish and drinking coffee. Companion mode opens automatically when idle, works offline, and returns to work when a new task or pending item arrives.
4. Add distinct sound effects and voice prompts for completion, choices, errors and Bluetooth disconnection. Switch between effects and voice, with voice as the default; manual stops stay silent.
5. Improve task home and settings navigation: scrolling long titles, a centered Confirm hint, settings accessible from the list, and progress feedback while holding Confirm.

Build: complete gate, merged 0x0 layout and matching ELF verified. Host tests: 59 application tests, C state/audio/settings tests, cross-language replay, native LVGL and plugin bundling passed. Device tests: the user accepted dev1 features with corrected voice delivery and dev2 navigation. Only the firmware version string changed for this final build; it was not reflashed. Unverified: final-image device test, cross-page BLE seeking, repeated recording/playback and prolonged stability.

Firmware: `whale-v1.3.0`

Full image SHA-256: `d345a38fb7d3a995a27da5d74b7c3ec1993b3c18e63cdf6cc8db4649ce491656`

ELF SHA-256: `ff9eee09905e5912c095f1fc95cc473541aea2c85628f42ada4a55b1fe6ec29f`

Update the companion bridge with this firmware. Source is on `feature/v1.3-companion-cues`. Flash the full image at `0x0`; the debug archive is bound to its hash. The accepted dev2 delivery remains unchanged.
