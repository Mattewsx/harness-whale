# Screen director

[简体中文](DIRECTOR.zh_CN.md)

`device-firmware/main/whale_director.c` decides which page owns the screen and when. It is pure logic: no ESP-IDF, LVGL or BLE. `whale_app.c` takes a snapshot (`whale_director_in_t`), asks the director, and carries out the answer: it opens pages, requests snapshots and cancels voice. The idea comes from the director design in Clawdmeter. Only the approach is borrowed; no code or protocol.

The first version moved the existing rules here without changing behavior. The pending queue is the first feature built on it. Later features (the anxious and celebrating moods, badges) add their rules here and their invariants to the fuzz test.

## Who owns the screen

Highest first:

1. Work that needs the user: questions, approvals, unread replies.
2. Voice: recording, uploading, a cue playing.
3. The page the user is reading or operating.
4. Companion interaction.
5. Everyday idle animation.
6. Desk display: today's summary or the clock.

## Rules

| Function | When | Answer |
| --- | --- | --- |
| `whale_director_desk` | Home or task page, idle for `WHALE_DESK_IDLE_MS` (2 min), every session verifiably idle, no voice, no companion reaction, no pending or active badge celebration | Desk display |
| `whale_director_desk` | Desk display, linked and ready to a bridge without the global summary (capability bit 11) | Home |
| `whale_director_offline` | Offline, not a resting page, idle for `WHALE_POWER_OFF_MS`, nothing holding (voice, audio, cue batch, pairing, unbinding) | Home (voice is cancelled) |
| `whale_director_summary` | Desk display and a valid global summary with a waiting question | Pending queue |
| `whale_director_summary` | Desk display and a valid global summary with only unread work | Attention list |
| `whale_director_summary` | Desk display and a valid summary with only running work | The running session |
| `whale_director_trigger` | Desk display and live cue 1 (a question) | Pending queue |
| `whale_director_trigger` | Home page left alone for `WHALE_QUEUE_IDLE_MS` (10 s), live cue 1, no voice, no companion reaction | Pending queue |
| `whale_director_trigger` | Desk display and live cue 2 to 4 | Attention list |
| `whale_director_trigger` | Desk display and trigger 5 (work started) | That session |
| `whale_director_key` | Desk display and any key press | Home |
| `whale_director_queue` | Result page of an accepted answer, shown for `WHALE_QUEUE_NEXT_MS` (1.5 s), a summary newer than the answer still counts a waiting question, no voice | Pending queue |
| `whale_director_result_key` | Confirm on that result page with the same summary | Pending queue (otherwise the task, as before) |
| `whale_director_screen` | Any operation page, or a linked live task outside the companion, wardrobe, badges and desk pages | Keep the screen lit |
| `whale_director_screen` | Voice, audio, cue batch, pairing or unbinding | Do not sleep |
| `whale_director_badge_start` | Newly unlocked badge bit(s) queued, on a quiet home page without voice, companion reaction or invitation | Start 4 s badge celebration (single badge name or merged `"解锁 N 枚新徽章"` summary) |
| `whale_director_activity_set` / `whale_director_activity` | Home page idle animation | Set from mood (5 min) or day and night; the next pick never repeats the current one |

## Feelings

`whale_director_feel` decides what the whale shows on the home page and above the list. The art is a placeholder: existing poses plus motion and a caption.

| Feeling | When | Shown as |
| --- | --- | --- |
| Celebrate | A session finished (live cue 3) or a badge celebration started, for `WHALE_CELEBRATE_MS` (4 s) | Happy and waving poses, bouncing, "任务完成啦！", `"解锁徽章 · <name>"` or `"解锁 N 枚新徽章"` |
| Anxious | A question has waited `WHALE_ANXIOUS_MS` (2 min) and no key was pressed for as long | Waiting pose, shaking, "有 N 个问题在等你" |
| Very anxious | The same for `WHALE_VERY_ANXIOUS_MS` (5 min) | Faster shake, "还在等你，来看看好不好？" |

Anxiety needs a fresh valid summary from a linked bridge. It counts from the later of the first waiting question and the last key press. Celebration comes first while it lasts. The whale stays calm on every other page and during voice, a companion reaction or an invitation.

"Pending queue" negotiates capability bit 17 and requests a FIFO snapshot with `0x10` sort byte 3. The bridge puts every answerable question first, oldest first, even when already read or outside the ordinary eight-row attention window. Reading does not become answering and never restores an unread badge. Ordinary list sort 2 keeps unread priority; sort 0 preserves the current order for paging. An old device never requests queue sorting. An old bridge without bit 17 falls back to an ordinary list from the desk/result page and leaves an idle home page alone.

The device opens row 0 directly. If no answerable question remains (for example, only computer-side questions are pending or the last one was archived), the desk display and the result page land on the list and a home page left alone stays home. Other pages are never interrupted: the list badge still shows the count.

Disconnecting or starting a new handshake cancels an in-flight automatic jump. A response with a changed bridge epoch also lands on the list/home without opening a question. Fresh capabilities and the reconnect baseline are never treated as the old queue response.

"Verifiably idle" means offline, or a fresh (under 30 s) valid global summary with no running, waiting or unread sessions.

## Invariants

The seeded fuzz test checks these after every random event:

- The desk display is only entered from the home or task page, after the idle time, while every session is idle, and never during voice or a companion reaction.
- A summary with waiting or unread work, or any live cue, always takes the desk display back to the task pages; a waiting question opens the queue.
- A question pops up over the home page only after it was left alone for 10 s, and never over voice or a companion reaction.
- The queue moves on only from the result of an accepted answer, after it has been shown, never during voice, and only on a summary newer than the answer that still counts a waiting question.
- Confirm on a result page opens the queue only when a question waits.
- A celebration needs a finished session or an unlocked badge celebration started on a quiet home page, and always ends after 4 s.
- The whale is anxious only while something verifiably waits, after both the wait and the time since the last key press passed the threshold.
- Feelings show only on the home page and the list, never during voice, a companion reaction or an invitation.
- Work never takes over any other page: the list, a reply being read, a question in progress, voice or settings.
- A key press always leaves the desk display.
- The offline rule only fires when offline, after the screen-off time, on an operation page, and never interrupts voice, audio, pairing or unbinding.
- After each pass, no offline operation page outlives the screen-off time.
- The desk display never stays on an old bridge.
- The companion, wardrobe, badges and desk pages never keep the screen lit. Pairing, unbinding and voice always block sleep.
- No flapping: a page the director just chose is stable on the next pass.
- The same input always gives the same answer.

## Tests

- `device-firmware/tests/test_whale_director.c`: table tests for every rule, then 300 seeds × 4000 random events through a small model of the app. A failure prints the seed, the state and the last 48 events. Reproduce with `WHALE_FUZZ_SEED=<seed>`; run longer with `WHALE_FUZZ_SEEDS` and `WHALE_FUZZ_STEPS`.
- `test_random_app_events` in `device-firmware/tests/test_whale_app.c`: 200 seeds × 3000 random BLE frames (some well-formed global summaries and live cues), key presses, link changes, voice and time jumps through the whole `whale_app.c`. It checks state ranges and that the desk display never hides work. Reproduce with `WHALE_APP_FUZZ_SEED=<seed>`.
- `tools/validate.sh --static` runs both normally and again under AddressSanitizer and UBSan when the compiler supports them.

`test_pending_queue` in `test_whale_app.c` walks the queue end to end: an accepted answer, a stale and a fresh summary, the automatic move to the next question, confirm, the fallbacks, and the home pop-up. The bridge order is covered by `test_pending_queue_is_first_in_first_out` in `tests/test_companion_cues.py`.

Not covered yet: the whole-app random fuzz still rarely reaches the question pages, because those need a full question exchange.
