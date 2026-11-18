# AI 部署指南：按目标修订安装

先读根目录[AGENTS](../AGENTS.md)。本页用于当前main；公开版v0.17.0-demo的下载、校验和安装按其[发布页](https://github.com/Mattewsx/harness-whale/releases/tag/v0.17.0-demo)。不要把main的manifest与旧版镜像组合，或为当前main克隆v1.2.0。构建、下载和发布不代表刷机授权。

## 电脑组件

要求Windows 10/11、64位Python 3.10～3.13、蓝牙、DeepSeek Harness，无需Wi-Fi配网。检查Python版本与蓝牙可用性，不读取用户真实会话或凭据。

当前main包含番茄钟／日记暂停离线修复，固件仍标识v0.18.0-dev2；请按[升级说明](UPGRADE.md)的三个SHA-256识别修订。在新目录获取当前main源码，或使用与目标镜像匹配的既有源码；不要覆盖已有工作树或未提交改动。新目录安装示例：

```powershell
git clone --branch main --single-branch https://github.com/Mattewsx/harness-whale.git harness-whale-main
cd harness-whale-main
python installer\install_companion.py --non-interactive --skip-key
```

电脑组件应来自目标固件的同一提交／交付包；若main以后继续演进，固定到所选镜像对应提交，不默认用更新源码配旧镜像。v0.17.0-demo直接使用其发布页的同版computer.zip，不运行本页的main克隆示例。无法确认源码、镜像和清单对应时停止。

安装器维护独立`.venv`、受管插件和profile块，安装前备份其改动。余额已隐藏，无需配置API Key。完成后完全退出并重启DSH，用 `/whale` 检查桥接器；电脑端安装命令不刷设备，也不需要手动常驻Python终端。首次配对核对两端数字码并确认；普通应用升级不主动解绑。

语音先在DSH启用本地SenseVoice并等待模型就绪。从会话详情短按确认录音，再按确认停止，核对草稿后明确选择发送、重录或取消；长按↑用于回主页。不要照抄旧版本的“长按↑录音”或旧解绑菜单路径。已有配对无法连接时先检查电脑组件版本并重启DSH，不以解绑代替定位。

## 固件与清单

当前main的身份和分区前提见[升级说明](UPGRADE.md)。完整镜像、应用和ELF用途不同，必须选择同一修订：

- 已有相同布局且需保留数据：只按升级说明写应用0x10000，不运行完整镜像刷写器。
- 首次完整安装或明确接受重置：准备目标完整镜像和匹配manifest。当前main的manifest只接受其记录的完整镜像哈希；没有对应镜像时停止，不改哈希绕过校验，不从旧发布页下载同名文件顶替。

将目标完整镜像复制到 `installer/firmware/Harness-Whale-ESP32C3-8MB.bin`，同目录manifest必须属于该修订。在目标源码根目录准备环境：

```powershell
python -m venv .flash-venv
.\.flash-venv\Scripts\python.exe -m pip install -r installer\requirements-flash.txt
.\.flash-venv\Scripts\python.exe -m serial.tools.list_ports -v
```

只读确认芯片、串口、布局和镜像，不能在多个串口之间猜测。说明标准刷写器会整片擦除并从0x0写完整镜像，旧设置、配对、徽章和日记等NVS数据会清除，获得明确刷机授权后才执行以下命令：

```powershell
.\.flash-venv\Scripts\python.exe installer\flash_firmware.py --port COMx
```

用已确认串口替换COMx。应用镜像不能传给此工具；`--no-erase`也不能使完整镜像保留NVS。校验失败先核对所选修订及来源，禁止修改哈希或反复擦除试错。

## 验收与记录

核对实际运行镜像／ELF身份及目标修订功能，不再用R11.4或R10.1当作当前版本要求。检查BLE与会话同步、按键／回复／语音、原穿戴及已保存进度；对当前main再核对专注和日记。只记录实际做过的项目，既有部分验收不能冒充长期稳定性或当前新构建全量通过。

串口观察须有时限并在结束后释放。源码与调试包不提交真实会话、设备身份、凭据或未清理日志。匹配ELF只用于其对应镜像，不用旧ELF解码新固件。
