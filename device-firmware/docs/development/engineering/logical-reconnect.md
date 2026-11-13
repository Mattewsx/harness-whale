<p align="right"><a href="logical-reconnect.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Logical BLE reconnects

Windows can retain a physical BLE connection after a bridge closes its GATT client. A new bridge client restarts its wire sequence numbers, while the peripheral may not receive a GAP disconnect event. Rejecting that client's HELLO against the previous sequence state prevents negotiation until the physical link eventually closes.

A complete legacy `0x01` HELLO is therefore a logical session boundary: version 2, one fragment, a 10-byte body with protocol 2.2, all six mandatory capability bits and both frame sizes at least 20. It clears volatile receive sequence/reassembly state and the previous large-frame grant before normal handshake processing. A repeated valid HELLO can obtain a replacement ACK. Malformed, unsupported and partial HELLOs do not bypass normal sequence checks. Other duplicate messages remain suppressed.

This boundary does not clear NVS, pairing, badge progress or persisted question identities. Incoming commands still require the authenticated bonded connection. Python and C receivers implement the same boundary. The bridge retries HELLO at one-second intervals within the existing eight-second deadline, stopping when negotiation succeeds.

Acceptance must include reconnecting without a physical disconnect, a lost first ACK, normal duplicate suppression and saved question identity replay. Host checks cannot replace Windows/device reconnect measurements.
