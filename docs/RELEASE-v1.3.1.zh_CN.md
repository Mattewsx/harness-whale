# v1.3.1 发布记录（准备中）

[English](RELEASE-v1.3.1.md)

本版是 v1.3.0 之上的稳定性更新，不改协议（仍为 2.2）、分区布局和界面。

1. 修复 `whale_wire.c` 分段数比较在 GCC 13 `-Wsign-compare` 下报错的问题（`11u/12u`）；CI 的固件主机测试同时用 Clang 和 GCC 跑。
2. 桥接器重连改为指数退避：1、2、4……秒，最多 60 秒，±20% 抖动；连接稳定 30 秒后从 1 秒重新开始。配对被拒仍固定等待 60 秒。
3. `/whale` 新增断连次数、最近一次断连原因和时间、最近一次重连耗时，以及断开时的下次重试倒计时。
4. 新增实机压力脚本 `harness/tools/whale_stress.py`：N 轮“录音→识别”，N 次断开重连（每隔几次保持断开 7 秒，让设备播放断连提示音），并汇总识别耗时、重连耗时，以及串口日志里的堆和各任务栈余量、崩溃或意外重启。
5. 固件只在串口输出健康日志（`whale_diag`、`whale_ble`）：每 30 秒、蓝牙连上/断开时记录空闲堆、历史最低堆和主任务、外设、LVGL、NimBLE 任务的栈余量；记录提示音播放与 BLE 断开原因。不含会话、标题或正文。

## 压力测试用法

先启动带 v1.3.1 桥接器的 Harness（或直接运行 `harness_ble_bridge.py`），设备连上后：

```
python harness/tools/whale_stress.py --voice-rounds 5 --reconnects 10 --serial COM5 --out stress.json
```

- 语音轮：按提示在设备上录一句话，识别结果出现后在设备上放弃，不要发送。
- 重连轮：脚本通过本机回环让桥接器主动断开一次，测量恢复到 2.2 就绪的时间；`--cue-every 3` 表示每 3 次保持断开 7 秒以触发断连提示音，`0` 关闭。
- `--serial` 可选，需要 `pyserial`（`installer/requirements-flash.txt`）。打开串口时不拉 DTR/RTS，避免复位设备；刷机期间不要运行。
- 任一轮失败或串口出现崩溃／重启时返回码为 1。

## 状态

Build：未构建。`installer/firmware/manifest.json` 中完整镜像的 SHA-256 暂为 `pending-v1.3.1-build`，标准安装器会拒绝任何镜像；发布前须用 ESP-IDF 5.5.3 构建本源码并填入真实哈希。Host tests：见 PR。Device tests：未进行。未验证：上述压力测试本身的实机结果。

Firmware: `whale-v1.3.1`
