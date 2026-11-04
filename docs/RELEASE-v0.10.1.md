# Harness Whale Companion v0.10.1

## What's new

- Larger whale companion on task detail, with title, status, elapsed time and battery.
- Down opens the latest final reply. Up/Down scroll continuously; Confirm collapses it. There are no historical-answer controls or page numbers.
- A volatile device cache reuses previously read reply fragments, including offline while retained.
- Reduced LVGL invalidation: unchanged pages do not repaint; clock and battery updates repaint their own areas.
- BLE 2.2, board support and partition layout remain compatible. Balance is hidden and progress appears only when real Todo data exists.

## Install and build

Use source and firmware from this tag. Install the bridge with `python installer/install_companion.py --non-interactive --skip-key`, restart DeepSeek Harness and check `/whale`. Existing current R09/R10 bridge installations do not need reinstalling.

For a fresh installation, place `Harness-Whale-ESP32C3-8MB.bin` in `installer/firmware/` and follow the deployment guide. The standard installer erases Flash and writes at **0x0**, resetting NVS and pairing. For an existing compatible layout, the separate `Harness-Whale-ESP32C3-app-0x10000.bin` updates only the application at **0x10000** and preserves pairing; follow `docs/UPGRADE.md`. Never pass this application image to the full-image installer.

To build, activate ESP-IDF **5.5.3**, enter `device-firmware/` and run `./tools/validate.sh`. The verified merged image is `build/FoloToy-AI-Passport-full.bin`. Published firmware uses the previously device-tested R10.1 build with matching symbols; CI rebuilds are validation artifacts, not silent replacements.

## Validation

Build and host regression tests passed. Device startup, BLE reconnect, roster synchronization and pairing preservation were observed. The user confirmed latest-only text, continuous scrolling and fast repeated reopening. No watchdog or crash was observed in separate 45-second and 55-second windows. Long-duration stability, live replacement by a new reply, and on-device BLE cache-request counts remain unverified. See `docs/R10.1-HANDOFF.md` for artifact identities and scope.

## 简体中文

本版采用更大的鲸鱼娘任务主页，显示标题、状态、用时和电量。↓ 展开最近一次最终回复，↑↓ 连续阅读，确认收起；不显示历史回答或页码。设备内存缓存减少重复读取，优化 LVGL 局部刷新。余额隐藏，无真实 Todo 时不显示进度。

首次刷机使用完整镜像 **0x0**，会清除 NVS 和配对；相同布局的旧版用户可使用独立应用镜像 **0x10000** 保留配对，两种镜像不可互换。电脑端安装可跳过 API Key，当前 R09/R10 桥接器无需重装。

构建、主机回归、实机启动/蓝牙/名单同步和用户确认的回复阅读体验已通过。两段短时日志未复现看门狗告警；长期稳定性、新回复自动替换和缓存请求次数仍有待实机补测。使用本标签源码和校验清单，镜像及 ELF 身份详见发布验收文档。
