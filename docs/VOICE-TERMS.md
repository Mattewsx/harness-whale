# Voice terminology correction

[简体中文](VOICE-TERMS.zh_CN.md)

The computer plugin applies a narrow text correction before displaying the voice
draft. After an explicit Chinese product operation such as 打开 / 启动 / 使用 /
重启, `deep seek`, `deep sick`, and `deepseek` become `DeepSeek`. It preserves
standalone `deep`, ordinary English, quoted literals, URLs and identifier suffixes.
This is post-recognition correction, not acoustic hotword support or a general
accuracy fix. Unusual command-like quoted text can still be ambiguous; review the
draft before explicitly sending it. The submitted text is the text previewed.

The current DSH SenseVoice API has no hotword parameter. Synthetic comparisons
of Chinese/automatic/English language hints, uncompressed/20 kbit/s Opus audio,
and DSH-style VAD did not establish a reliable general improvement. An isolated
bilingual Zipformer trial with DEEPSEEK / HARNESS hotwords improved the English
fixtures but regressed on mixed speech; it is not installed as a runtime model.
Do not replace the current recognizer or claim actual microphone accuracy from
these synthetic fixtures.

This change requires reloading the computer plugin, not flashing firmware.
The recognition language, recording, BLE compression, model and confirmation
flow are unchanged. Regression coverage exercises the HTTP preview path,
positive/negative terminology cases, and absence of automatic delivery. Real
microphone acceptance remains required. Firmware R11.4 build artifacts retain
their original identities.
