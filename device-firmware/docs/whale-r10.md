**English** · [简体中文](whale-r10.zh_CN.md)

# Whale companion R10

The task detail page presents a 196-pixel character, task title, status, elapsed
time, connection state, and battery reading on the 240 × 320 display. A two-line
title uses a smaller character to preserve the text. The home page always opens
with the latest reply collapsed.

| Screen | Up | Down | Confirm |
| --- | --- | --- | --- |
| Task detail | Read the full title | Open the latest final reply, when available | Open the task list, or answer a pending question |
| Reply | Read backward; collapse at the beginning | Read forward, fetch the next segment, or retry a failed read | Collapse to task detail |
| Task list | Select previous task | Select next task | Open task detail |

Long Down opens the task list from task detail or the reply reader. Long Confirm
opens connection management. Existing question, pairing, and result checks remain
available. A new final reply leaves the current text and reading position intact
and displays an update notice; advancing beyond the current segment opens the
new reply. Removing the focused session returns to the list and clears its text.

Only real todo counts are displayed. Account balance is hidden. Battery reads
continue every 30 seconds through the BSP, with stale and unknown indicators.
Elapsed time advances only for active states while synchronized.

The BLE protocol remains 2.2. Use the current companion bridge with workspace
membership, archival filtering, and local-history restoration. Old reply frames
must match the session, reply revision, and requested segment before acceptance.

The font generator covers the GB2312 level-one inventory and fixed interface
text at the required sizes. The title uses 18 pixels, reply text 16 pixels, and
secondary labels 12 or 14 pixels. Unsupported characters follow the existing
font-placeholder and question-validation behavior.

Run `tools/validate.sh` for the repository gate. The application regression test
uses the real C dispatcher with hardware stubs. On-device acceptance must check
large-image animation, title clipping, reply navigation, questions, reconnection,
session removal, battery updates, and minimum free heap.

The merged image is written at `0x0` and covers NVS. A compatible application
update at `0x10000` preserves NVS and pairing. Flash only after user confirmation.
