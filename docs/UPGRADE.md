# 保留配对升级到 R11.4

这条路径仅适用于已确认使用相同分区布局的 AI Passport 鲸鱼固件：8 MB Flash、分区表 0x8000、NVS 0x9000/0x6000、PHY 0xF000/0x1000、factory 0x10000/0x7F0000。未知固件或改过分区的设备先核对布局，不能直接套用。

从 [v1.2.0](https://github.com/Mattewsx/harness-whale/releases/tag/v1.2.0) 下载 `Harness-Whale-ESP32C3-app-0x10000.bin` 和 `SHA256SUMS.txt`。应用 SHA-256：

```text
2c40fcf608a1fe8db4ba0715fe9cd32ec3d9dfbec7e87455e36de3b1486b29d4
```

在放置应用镜像的目录检查：

```powershell
(Get-FileHash .\Harness-Whale-ESP32C3-app-0x10000.bin -Algorithm SHA256).Hash
```

确认哈希一致后，在源码根目录创建 `.flash-venv` 并安装 `installer/requirements-flash.txt`。用 `python -m serial.tools.list_ports -v`（该虚拟环境中的 Python）确认目标串口，再向用户确认应用升级范围。假设镜像位于源码根目录，获得授权后：

```powershell
.\.flash-venv\Scripts\python.exe -m esptool --chip esp32c3 --port COMx --baud 460800 write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x10000 .\Harness-Whale-ESP32C3-app-0x10000.bin
```

用确认的串口替换 COMx。此流程不执行整片擦除，也不写 bootloader、分区表或 NVS；相同布局下保留配对。等待写后校验成功与设备重启，再检查 BLE 重连和回复阅读。

**不要把应用镜像交给 `installer/flash_firmware.py`，该安装器固定从 0x0 写入完整镜像。** 完整镜像的正确文件名是 `Harness-Whale-ESP32C3-8MB.bin`，即使不先擦除，它也覆盖 NVS。两种文件不能互换。

R11.4 保持协议 2.2，但语音与提醒功能需要安装同一 v1.2.0 发布的电脑插件及桥接器，并完全退出重启 DSH。旧蓝牙服务缓存可能需要两端解绑后重新配对，具体步骤见部署指南。
