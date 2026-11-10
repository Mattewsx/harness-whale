[简体中文](INTEGRATION-v0.17.0.zh_CN.md)

# v0.17.0 local integration

This is an unreleased development integration. It combines the reviewed random invitations with PR #11 (approvals and multi-select), #12 (daily summary) and #13 (spoken new tasks). It creates no GitHub Release or community submission.

## Review fixes

- Summary totals persist before the first BLE connection and after merges, remain cumulative beyond the bounded deduplication cache, and are invalidated when the bridge epoch changes. Wide totals wrap into two lines.
- Spoken tasks require a resolved workspace and successful attachment before the first message. Uncertain creation keeps its nonce receipt; retries cannot create a second session. New receipts are refused at capacity instead of evicting old identities.
- Device approvals carry the complete tool, arguments and permission reasons within the 512-byte prompt. Incomplete or oversized requests stay on the computer. Line separators remain faithfully JSON-escaped. Cancellation or lost session visibility during cleanup prevents acceptance.
- Combined capability bits and control messages retain both rich questions and spoken tasks. Existing skins, affection quotas, read-on-view badges and invitation rules remain intact.

## Validation

- Build: PASS — ESP-IDF 5.5.3 complete gate, single factory partition, merged image and matching debug archive verified.
- Host tests: PASS — 127 Python tests, plugin bundle, voice/approval safety replays, native application tests, 11 cross-language replay scenarios and production LVGL regression.
- Device tests: PASS only for invitation triggering, response actions/captions and retained settings on the earlier `f9227d9c20f40dcc90f882fa9f1825ec11155bb8c83815bcd828fe9d3748abf0` image, confirmed by the user on 2026-11-10.
- Unverified: the integrated image below has not been flashed; real DSH approval races, new-session creation, summary behavior, invitation ignore/interruption paths and long-duration operation need device acceptance. The installed bridge is unchanged.

## Integrated build identity

- Full image (0x0): `560b0f1b452e41f678c720dfdd18bff89bc4bf1076febd37571afae85fbc86e4`
- Application (0x10000): `27774d88c64b92301f22e0064055fefdbefd108fa03a312e5ebb1edb83a0d9ca`
- ELF: `2f76953dc7d6f6f8eeb3322ac6a93b271a47c349372711d0e457d5ac32a1ddee`

The installer manifest records this full image; firmware inputs are pinned in `release-source.json`. Generated binaries and raw diagnostics are excluded from Git. Preserving stored settings requires the matching partition layout and application-only upgrade; flashing still requires separate user authorization.
