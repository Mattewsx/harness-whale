# Whale-mode power saving and battery-life measurement

**English** · [简体中文](POWER.zh_CN.md)

Since v1.4.0, whale mode adds three power-saving behaviours on top of the v1.3 two-minute dimming. The desktop protocol and plugin are unchanged (still 2.2).

## Behaviour

| Situation | Behaviour |
| --- | --- |
| 2 minutes without input | Backlight dims (as in v1.3) |
| 5 minutes without input | Screen off: backlight off, UI refresh stopped; the first key press only wakes the screen |
| Running task, pending question, or a page other than home | Screen stays lit (as in v1.3); once the computer disconnects, stale "running" rows no longer count |
| 30 s after the computer disconnects | Advertising interval grows from 30–60 ms to about 1.0–1.3 s; a key press restores 30 s of fast advertising |
| Offline for 10 minutes with the screen off | Offline deep sleep: any key wakes it at once; it also wakes every 10 minutes, advertises with a dark screen for 75 s and sleeps again if no computer connects |
| 12 consecutive timer wakes (about 2 hours) without a computer | Key wake only |

Offline pages (reading replies, voice) return home after 5 idle minutes so the device can turn the screen off and sleep. It never sleeps while pairing, unbinding, recording or playing a cue.

Deep sleep restarts the application: pairing is kept and the in-memory reply cache is cleared, the same as a power cycle. Key presses in the first second after a key wake are ignored, so the waking press is not taken as a click. The fuel gauge keeps running through sleep so its reading is not reset on each wake.

The bridge backs off for at most 60 s and scans for 8 s, so the 75 s wake window always contains one full scan: once the computer is back, it reconnects within about 10 minutes, or immediately after any key press.

## Hardware testing

### Quick check (bench build)

Build with short thresholds to walk every state within minutes:

```bash
cd device-firmware
idf.py -D WHALE_POWER_TEST=1 build
```

Bench thresholds: dim at 10 s, screen off at 20 s, slow advertising after 10 s, deep sleep after 60 s offline, 30 s wake window, timer wake every 60 s, key-only after 3 cycles. The boot log prints `BENCH BUILD`; **never release this image**.

`whale_power` serial lines record every state change and the state every 30 s (`boot`, `sleeps`, `cycles`, `screen`, `adv`, `offline_ms`, `soc`); `whale_sleep` lines record each sleep with its timer and cycle count. Suggested checks:

1. Leave the device: `screen=dim` then `screen=off`; one key press only lights it.
2. Stop the bridge: `advertising slow`; a key press restores `fast`.
3. Stay offline: `offline sleep #1`, black screen; a key wakes it at once (`boot=key`).
4. Let it sleep again without touching it until the timer wake (`boot=timer`, screen stays dark); start the bridge within the window and it reconnects and lights the screen.
5. The USB serial port drops during deep sleep; this is expected.

### Battery life (release build)

USB charges the cell, so battery life has to be measured unplugged. The firmware writes every state-of-charge change to NVS (timestamps are RTC seconds, which keep counting through deep sleep) and prints it after USB is reconnected:

1. Charge to 100 %, unplug USB and use the device in the scenario to measure (e.g. connected and working, or offline). A rise of 5 points or more counts as a recharge and starts a new run; the previous run is kept.
2. Afterwards (or after it shuts down), reconnect USB and run:

   ```bash
   python harness/tools/whale_battery.py --serial COM5 --reset
   ```

   `--reset` restarts the device so it prints the log; pressing reset works too. The report gives duration, charge used, charge per hour and the projected full-charge life, plus the share of time linked, screen on, in timer wakes and with slow advertising. `--log FILE` analyses a saved serial capture.

The log holds only timestamps, charge and state bits, no session content. Gauge percentages have their own error; measure at least 20 points per scenario before trusting a projection.
