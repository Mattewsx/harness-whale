# AI 自动部署指南

使用同一发布标签的源码、manifest 与固件。当前版本 [v0.10.1](https://github.com/Mattewsx/harness-whale/releases/tag/v0.10.1)，固件 R10.1。先阅读根目录 AGENTS.md。未经用户确认不刷机，发布或下载成功不代表获得刷机授权。

## 环境和安装

要求 Windows 10/11、64 位 Python 3.10～3.13、蓝牙、DeepSeek Harness。检查 `python --version`、Python 位数与蓝牙可用性，不读取用户会话或凭据进行诊断。

```powershell
git clone --branch v0.10.1 https://github.com/Mattewsx/harness-whale.git
cd harness-whale
python installer\install_companion.py --non-interactive --skip-key
```

余额已隐藏，无需询问 API Key。安装器维护独立 `.venv`、受管插件与 profile 块。安装后让用户完全退出并重启 DSH，通过 `/whale` 检查桥接器；无需手动常驻 Python 终端。

## 首次刷机

1. 下载 `Harness-Whale-ESP32C3-8MB.bin`，放入 `installer/firmware/`，使用同标签的 `manifest.json`。
2. 创建刷写环境并枚举串口：

```powershell
python -m venv .flash-venv
.\.flash-venv\Scripts\python.exe -m pip install -r installer\requirements-flash.txt
.\.flash-venv\Scripts\python.exe -m serial.tools.list_ports -v
```

3. 只读确认目标为 AI Passport 的 ESP32-C3；多个串口时不要猜。向用户说明标准安装器会整片擦除旧固件、NVS、设置及配对，获得确认。
4. 执行 ` .\.flash-venv\Scripts\python.exe installer\flash_firmware.py --port COMx`。脚本校验 SHA-256，再擦除和从 0x0 写入完整镜像。用实际确认的串口替换 COMx。

已有鲸鱼固件并希望保留配对时，改用 [应用升级步骤](UPGRADE.md)。不要给标准安装器传入应用镜像，也不要用 `--no-erase` 假装保留 NVS：完整镜像仍覆盖 NVS 区域。

## 验收

- 设备版本 R10.1，鲸鱼娘界面、中文和电量正常。
- BLE 自动连接，`/whale` 显示 connected；会话名单与未归档主会话一致。
- 状态、标题和用时随任务变化；无 Todo 时不展示进度，余额不显示。
- ↓ 展开最新最终回复，↑↓ 连续滚动，没有页码；确认收起后再次展开正常。
- 普通单选的上下键和确认可用，多选和自由输入回电脑。
- 新回复替换旧回复，归档或删除同步移除条目。记录观察范围，不声称未做的长时间测试通过。

监视日志须限定时间并在结束后释放串口。原始会话、设备身份、凭据及未清理日志不提交 Git。安装器可重复运行，profile 修改前保留备份；失败时先检查原因，避免反复擦除设备或修改其他插件。
