<p align="right"><a href="whale-r11.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# R11.4: attention markers and voice replies

R11.4 is a development build on `feature/r11-voice-input`, based on R10.4. The public v0.10.1 artifacts remain unchanged. Install the matching plugin and bridge from this checkout; `voice.ts` and `whale_voice.py` are required. The installer adds the pinned FFmpeg decoder to its own Python environment. No Wi-Fi configuration or speech cloud key is needed.

## First use

1. Run the matching companion plugin/bridge on the paired computer. Enable local SenseVoice under DSH Plugins → Voice input and finish resource preparation. The “resources prepared, wake automatically when recording” standby state is supported. Fully quit and restart DSH after updating the companion plugin. The device reports unavailable if the provider or audio transport is not ready.
2. Open a task home and hold Up: recording starts automatically after readiness checks. A short Up press no longer opens the full title. Speak toward Passport; confirm again to stop, or press Up to cancel. The limit is 60 seconds.
3. Read the transcript with Up/Down. Confirm opens Send / Record again / Cancel. Only choosing Send delivers text to the pinned conversation. A running task receives a next-turn followup, without interruption.
4. Confirm the sent/queued result to return. A question must use its existing answer flow. For an uncertain result, Confirm queries the original message; it does not resend. Hold Confirm to leave a voice page (except during a pending send).

## Attention and preserved behavior

The task list shows only task names and status, without numeric IDs or reply excerpts. Active work is blue, waiting for an answer amber, completed/new reply green, errors red, and idle/stopped gray. Selected rows use lighter matching colors for contrast.

Opening the list or paging refreshes priority: awaiting answer → unread error → new reply → active work → other sessions, stable within each group. Opening the list focuses its highest-priority item. Status updates do not reshuffle the visible list while it is in use. Reading or resolving an item affects its rank on the next refresh. Selection waits for refresh completion; the cached list remains available offline.

The list distinguishes new replies, questions awaiting an answer, and unread errors. Successfully displaying the first reply segment marks that revision read. Entering an errored task acknowledges its error marker but retains the error state. Merely browsing the list never clears markers. Answer/cancellation resolves a question. Older firmware receives only the original row bits.

Read markers are stored on the computer in `HarnessWhale/read-marks.json` under the local application-data directory (or `~/.local/share`), keyed by a hash of the session identity. They contain revision IDs, not reply text. The bounded store survives bridge restarts. Archived/deleted sessions remain excluded. Battery, latest-reply caching and continuous scrolling, real Todo progress, and the 85% / 150 ms completion tone remain supported. The peripheral worker defers sound during microphone capture.

## Transport and failure handling

The encrypted/bonded BLE link uses an additional notification characteristic ending `0203` for 62-byte frames: 12 bytes of stream identity/sequence/sample metadata and a 50-byte Opus packet. Encoding is 16 kHz mono, 20 ms, 20 kbit/s CBR, complexity zero. BLE ATT MTU must be at least 67. The device retains at most 100 packets (two seconds), resends missing frames, and fails explicitly on overflow or incomplete upload. It uploads while speaking, then decodes and recognizes locally after Stop; this is not incremental live transcription.

Protocol 2.2 negotiates voice bit 512 and attention bit 1024. Messages `40`–`47` cover preparation, status/transcript, codec header, end, cancel, submit, query and cumulative ACK; `4a` acknowledges an attention revision. Numeric values are hexadecimal. Transcript messages use the existing bulk lane. Session/epoch/recording identity prevents an old result attaching to another capture.

The plugin exposes an authenticated ephemeral loopback service exclusively to its managed child. Tokens travel through process environment and are never logged or committed. Original audio remains in memory and is discarded after processing/cancellation. Drafts expire after ten minutes of inactivity. Transcript size is bounded to 1,800 UTF-8 bytes; overlong or invalid results require rerecording. No audio or transcript is stored in device NVS.

Confirmed sends use a stable message identity. Serialized receipt checks and durable DSH inbox events prevent duplicate delivery after a consumed message or service restart. Resume uses DSH's session controller, preserving session configuration and ownership. If the bridge epoch changes while delivery is uncertain, check the desktop conversation; the device cannot automatically reconcile its old transport identity. Incomplete recordings cannot resume after a disconnect. Completed drafts survive a BLE reconnect while both processes stay alive, but not a device/bridge restart.

## R11.2 capture startup correction

The pinned `esp_audio_codec` 2.6.2 encoder returns raw Opus packets and leaves `codec_spec_info` empty. R11.1 incorrectly required that optional field to contain an `OpusHead`, so it exited before opening the microphone. R11.2 constructs the RFC 7845 mono header in the application and checks the actual encoder format instead. Capture logs identify the failed stage without audio, transcript, session or pairing data.

For this pinned C3 VOIP encoder, lookahead is `Fs/400 + Fs/250`: 104 samples at 16 kHz, represented as 312 samples at the Ogg 48 kHz clock. This was checked against `opus_encoder_init` and `opus_encoder_ctl(OPUS_GET_LOOKAHEAD)` in the matching R11.1 ELF. One zero tail frame flushes delayed speech; the bridge trims the decoded stream to the captured sample count. Revalidate this assumption before changing the codec version or application mode. The regression gate covers empty optional metadata, header/tail generation, failures at ten startup/capture stages, cancellation, and clean retry.

During R11.1 acceptance, the list worked and Windows retained an old GATT service cache. Removing the computer pairing and device binding, then pairing again with matching numeric codes, restored the audio characteristic and subscription (MTU 247). If local recognition is ready but the audio channel remains missing after a firmware upgrade, use this re-pairing procedure. R11.2 does not change BLE services, so an already repaired pairing can be kept. R11.2 microphone capture and real-time encoding remain pending on-device acceptance.

## R11.3 encoder stack correction

R11.2 reached microphone initialization, then reset during encoding. The matched ELF (`387fe572b9fa82d2d2c6837784696a24966e68a079d136892b9efb6d741099c8`) resolves the protection fault to `silk_NSQ_c`, called by `silk_encode_frame_FIX`. The 16 KiB peripheral stack had already used 18,032 bytes at the trap; remaining NSQ allocations and the LPC helper bring this observed path to approximately 19,696 bytes. This is a lower-bound analysis of that path, not proof of every possible codec branch.

R11.3 assigns only the peripheral worker 24 KiB (8 KiB more). It leaves the BLE/display stacks and two-second retransmission window unchanged. Logs record free heap and minimum remaining stack after the first frame, every second, and at completion. IDF reports this stack watermark in bytes. Below 2 KiB remaining, capture stops and releases its resources instead of proceeding to another frame; hardware stack protection stays enabled. Tests cover this low-margin exit and a clean retry. This guard cannot prove the first frame is safe, so physical acceptance must measure margin with varied speech, 1/10/60-second captures, repeated recording, and concurrent UI/BLE activity.

## R11.4 resolved graphics-memory budget

R11.3 failed before capture with `audio_init / ESP_ERR_NO_MEM`; the I2S RX DMA allocation failed and minimum free heap reached 8,316 bytes. Its actual ELF contained a 65,536-byte `work_mem_int` LVGL pool. With the locked LVGL 9.6 dependency, `CONFIG_LV_MEM_SIZE_KILOBYTES=32` is deprecated and loses to the default byte-sized `CONFIG_LV_MEM_SIZE=65536`. The native renderer had used 32 KiB, so previous host tests did not reveal this firmware configuration mismatch.

R11.4 uses `CONFIG_LV_MEM_SIZE=32768`, recovering 32 KiB without changing the display DMA buffer, 32-slot reply cache, two-second audio window, codec quality, or 24 KiB peripheral stack. A compile-time UI assertion and a firmware gate check now verify both the resolved sdkconfig and the actual ELF pool symbol. Regression fixtures reject the old KB setting, the 64 KiB default, and a linked pool differing from configuration. This aligns firmware with the existing native renderer budget; physical DMA allocation, codec stack margin and sustained speech still require acceptance on R11.4.

## Validation and device acceptance

Automated coverage includes actual desktop Opus encode/decode, packet ordering/duplicates/gaps, bounded C retransmission windows, transcript confirmation, query-only recovery, durable submission deduplication, archive/question rejection, read revision persistence, firmware input state, Chinese coverage, native LVGL rendering and the existing reply/choice/session regressions. Tests use synthetic content and never send messages to a real conversation.

The complete ESP-IDF gate produces a verified merged 0x0 image with matching ELF/MAP. See the delivery report for exact hashes and results. Compilation and host tests do not validate microphone capture, speaker/codec switching, ESP32-C3 real-time encoding, BLE radio throughput, or live DSH provider delivery. Test those on the device with approval, including 1/10/60-second speech, cancellation, reconnect, archive, repeated Confirm, an active task, battery display and completion sound. Full-image flashing may reset pairing; prefer the documented compatible app upgrade when retaining NVS. Do not flash without approval.
