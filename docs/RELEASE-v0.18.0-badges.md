<p align="right"><a href="RELEASE-v0.18.0-badges.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Badge accessories and logical reconnects

This revision builds on the published standby companion version. Use the
`feature/desk-companion-upgrade` branch for its matching computer components:

```text
python installer/install_companion.py --non-interactive --skip-key
```

Restart DSH after installing. Bluetooth is required for session synchronization;
no Wi-Fi setup is needed. On Home, choose Interactions, then Outfits or Badges.
Use Up/Down to browse and Confirm to wear an unlocked item. Hold Up to return Home.

## Changes

- Eight achievement badges unlock four accessory sets, independently of the four
  outfits. Changing clothes keeps the equipped accessory.
- The baby-whale plush remains visible on Home and during actions. When the
  character's hands are busy, it rests beside her. Old and new accessory areas
  are invalidated when switching poses or clothes.
- Badge progress and equipped items are saved. Loading supports 6-, 126- and
  206-byte badge records. Older formats keep cumulative progress but cannot keep
  full question identities; their first replay may count again. Current records
  retain complete wire identities, without promising cross-epoch business deduplication.
- Windows reconnects can address an already-paired peer without new advertising.
  A supported complete HELLO reopens the logical wire session even if Windows
  retains the physical connection. Other duplicate frames remain suppressed.

## Verification and scope

Build and host checks passed: ESP-IDF 5.5.3 complete gate, 142 Python tests,
Python-to-C replay, 324 save-migration combinations and native rendering checks.
The unchanged renderer inputs passed 720 redraw comparisons with zero mismatches
and 5,960 bytes of peak LVGL pool headroom. This is a host measurement.

The application-only upgrade retained the partition/NVS/PHY region byte for byte
before first boot. The running ELF matched the delivered image. Three logical
reconnects completed in 2.890, 2.766 and 2.766 seconds; a managed bridge restart
also recovered. The user confirmed outfit/action rendering and sleepwear plus
the plush after restarting. These results do not cover every pose's occlusion,
real sleep/wake, concurrent audio/reply/save memory peaks or long-run stability.

## Artifact identities and upgrades

- Embedded firmware version: `whale-v0.18.0-dev2`; distinguish this revision by its hashes.
- Full image SHA-256: `e22e332cbb0bafde8a9259a06969e8ffaa72a44b86387c2d411041711de4e5eb`.
- Application SHA-256: `29cb06ea56849ba0ca6dd5eebc3f048a85408df7c0cf68a1cb5432bf26700f2f`.
- ELF SHA-256: `5c66ef5e81174fa994e743588ac6d54733130277cd4ed3d200e15a9261ffbe6b`.

The full image is for `0x0` and can overwrite settings. To keep pairing and
progress on a confirmed identical layout, use the matching application at
`0x10000` following [UPGRADE](UPGRADE.md). Do not pass an application-only image
to the full-image installer. Community review status is reported by the server;
these notes do not claim approval or create a GitHub Release.
