[简体中文](RELEASE-v0.17.0.zh_CN.md)

# DeepSeek Harness Whale Companion v0.17.0-demo

v0.17.0-demo · Spontaneous company and spoken new tasks

1. Spontaneous invitations: when work is quiet, the whale girl may invite you to wave, pet her, offer a fish or share a coffee. Respond or let her quietly return to her routine.
2. A daily summary shows completed work and time spent while idle. Switch back to the clock in Settings if preferred.
3. Speak a new task from the session list, review the text and create a visible session on your computer. Improved failure and duplicate-confirmation handling keeps existing sessions intact.
4. Answer multi-select questions across pages and submit them together. Permission approvals default to Deny; read and confirm the full request before allowing it. Long requests stay on the computer.
5. Improve compatibility and reliability with real DSH approval routing, session workspace attachment and saved daily totals.

## Getting started

Download the matching computer package from this release. Run `python installer/install_companion.py --non-interactive --skip-key`, fully quit and reopen DSH, then pair over Bluetooth. Existing installations need the new computer package too. No Wi-Fi setup is required. See [README](../README.md) for all controls and local voice preparation.

The full image is for 0x0 and resets stored settings and pairing. On a device with the verified matching layout, the application image at 0x10000 preserves stored data; follow [upgrade guidance](UPGRADE.md). Never put the application image at 0x0. Assets and code are updated together over USB.

## Validation and scope

- Build: PASS, complete ESP-IDF 5.5.3 gate, merged-image and matching debug-bundle verification, production LVGL regression.
- Host tests: PASS, 129 tests and plugin bundle. The functional base is `548d1f59621935eba742100935a0bbce7ffee73d`.
- Device tests: basic integration acceptance passed on candidate image `560b0f1b452e41f678c720dfdd18bff89bc4bf1076febd37571afae85fbc86e4`, with the matching PC fixes. The user confirmed settings preservation, spoken task creation without duplicates, original sessions, multi-select, summary and real short-request approval denial/allow-once. Invitations were separately accepted on the earlier candidate.
- Unverified: this release image changes only version identifiers and the version heading from the accepted candidate and has not been reflashed. Long-duration behavior, daily rollover, all outfit/action combinations and physical concurrent cancellation still need dedicated checks.

## Build identity

- Full image: `1e4015e47f8c7bf7a254454b26db896d69ce545b583e76daf258471093064763`
- Application: `bbf82a23417eaeae12beb5535c40120a475df34a012419982b778202421c6c0b`
- ELF: `d7dcb8613f22debe8fe76390df404f0cbafa17ef6279c0ea6247074f91b9459c`

Source inputs are pinned in `release-source.json`. Debug assets match the exact released application. Community project 1966 is submitted separately for review; its public version depends on moderation, not the GitHub release status.
