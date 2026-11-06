# v1.4.0 release record (candidate)

[简体中文](RELEASE-v1.4.0.zh_CN.md)

Protocol 2.3: faster BLE transfer through negotiated large frames. Partition layout and UI are unchanged.

1. Capability bit 12 negotiates large frames. The bridge offers `min(244, MTU − 3)`; the firmware grants the smallest of both peers' sizes, 244 and its own ATT MTU − 3. At MTU 247 a frame carries 236 payload bytes instead of 12.
2. Large fragments use header version 3; legacy fragments stay version 2 and remain accepted. The hello reply itself is always a legacy frame, and the grant is dropped on disconnect.
3. Backward compatible both ways: v1.3.1 and older firmware ignore bit 12 and keep 20-byte frames; an older bridge never offers bit 12, so new firmware answers exactly as 2.2.
4. Firmware receives large writes through an 8-slot, 244-byte ring (about 2 KB) released in order; the event queue format is unchanged.
5. `/whale` shows the negotiated `BLE frame` size and MTU.

## Status

Build: not run locally (no ESP-IDF in the development container); CI builds the firmware. Host tests: see the pull request. Device tests: not run. Unverified: throughput on hardware, Windows MTU negotiation, long-duration stability.

The full-image SHA-256 in `installer/firmware/manifest.json` is a placeholder that no image can match until the ESP-IDF 5.5.3 build of this exact source is recorded.

Firmware: `whale-v1.4.0`
