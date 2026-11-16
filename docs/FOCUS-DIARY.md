<p align="right"><a href="FOCUS-DIARY.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Focus timer and daily diary

The Home dock is ordered Interactions, Focus, Sessions, Settings. Up/Down selects an entry and Confirm opens it. Hold Up to return Home.

## Start and continue

Open Focus, then Time. Choose 15, 30 or 60 minutes and press Confirm once to start and return Home. Browsing durations does not start a timer. The parent icon or a long Confirm cancels selection without changing the last duration. The overview's Start action reuses the last started duration.

Reopen Focus to pause, resume or end the round, or view Diary. Viewing records and returning preserves the timer state. A finished focus round adds one to today's total and starts a break: 5 minutes after a 15- or 30-minute round, or 10 minutes after a 60-minute round. Completion requests a screen wake and uses the normal sound setting; it does not replace higher-priority reading, questions or voice pages.

Focus and interactions need no Wi-Fi or PC connection. Running and paused rounds keep their remaining time in RAM and prevent offline deep sleep; ordinary dimming and screen-off still apply. End the round to restore normal offline sleep eligibility. Restarting or removing power ends an unfinished round; its countdown is not restored.

## Diary and saved totals

Open Diary inside Focus to review the seven most recent daily records, with actual dates, completed tasks, work time, focus count and the recorded outfit/accessory. Confirm or long Confirm returns to Focus. Long diary sentences wrap within the card.

Dates use the computer-synchronized local clock and a 05:00 workday boundary. Offline focus completions are merged once when the clock becomes available and schedule a save. Normal saving followed by a restart restores completed daily totals; a pending or undated in-memory update is not a promise of recovery after sudden power loss. Unpairing removes the Bluetooth bond but preserves local diary history and today's completed total.

The `diary_v1` record is a packed 12-byte entry; seven entries form an 84-byte archive. Storage runs in the worker using immutable snapshot slots, separate from display/input callbacks. Work time accumulates to a 300-second save threshold; other record changes and failed writes follow the queued save/retry path. Badge storage and the existing partition layout are unchanged.

## Validation

Application tests cover all three start durations, cancel/quick-start, pause/resume beyond the offline sleep threshold, stopping to permit sleep again, diary snapshots and retry, delayed clock recovery, unpairing and reloading totals. Native rendering tests check diary wrapping, redraw stability and the existing memory pool. Hardware acceptance is separate: verify actual buttons, screen wake, sound, completed totals across restart, and concurrent heap/stack margins for the exact flashed image.
