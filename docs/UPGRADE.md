# 保留数据的应用升级

本页用于先选择目标修订，再核对其应用镜像与分区布局。`whale-v0.18.0-dev2` 曾用于多个不同构建，版本名相同不代表镜像相同。不要根据版本号大小或历史文档自动选择v1.3.1、v1.2.0，也不要将不同修订的固件、manifest和ELF混用。

## 选择目标修订

- **当前main：番茄钟／日记暂停离线修复版**，variant为 `focus-diary-paused-sleep`。使用本仓库同一main提交的电脑组件及该构建实际交付的镜像。安装器[manifest.json](../installer/firmware/manifest.json)记录下列身份；[release-source.json](release-source.json)记录固件输入。此修订没有独立GitHub Release；社区可见性以玩法1966实际审核结果为准，本页不承诺已公开下载。
- **GitHub公开版v0.17.0-demo**：只按[v0.17.0-demo发布页](https://github.com/Mattewsx/harness-whale/releases/tag/v0.17.0-demo)选择其应用、电脑包及SHA256SUMS。该应用不是下表的当前main应用；不要套用main清单。发布页已修正旧升级链接，标签源码和电脑包内的旧升级／部署文档不适用于该版。
- **其他历史修订**：使用该修订自己的发布页或原始交付身份与匹配源码。旧版验收文档保留历史，不会随main升级；缺少目标镜像、可信校验值或匹配清单时停止，不用另一个版本替代。

当前main身份（完整哈希必须一致）：

| 文件角色 | 写入位置／用途 | SHA-256 |
| --- | --- | --- |
| 完整镜像 `Harness-Whale-ESP32C3-8MB.bin` | 0x0；覆盖存储，不用于保留数据升级 | `4ffe3136e8140c753d61f2ce793967f0d648686e9be5e56ec5d3e08c986b8b57` |
| 应用镜像（构建原名 `FoloToy-AI-Passport.bin`） | 仅应用0x10000 | `9cb6a743711c24f80f17f286abb4a891125cd8f8aff56e96961fdc8c618fd6cd` |
| ELF | 只用于匹配本修订的调试，不刷写 | `45da90d0e0deecf765353b090dd546b63bb1a4d78ddd302d24e692bc22a71026` |

文件可以在交付时重命名，不能凭文件名判断身份。`installer/firmware/manifest.json`的 `application_sha256` 是当前应用校验值，`sha256`表中则是完整镜像校验值，二者不能互换。

## 分区与数据前提

仅适用于已只读核对相同布局的AI Passport鲸鱼固件：8 MB Flash、分区表0x8000、NVS 0x9000/0x6000、PHY 0xF000/0x1000、factory 0x10000/0x7F0000。未知布局或修改过分区的设备不能直接套用。先备份目标设备的重要存储，记下配对、穿戴、徽章与日记累计；重启不恢复未完成倒计时，已保存累计与未落盘内存更新也不同。

保留数据升级只写应用0x10000，不整片擦除，不写bootloader、分区表、PHY或NVS。升级后由目标应用执行存档兼容逻辑；本页不承诺将新存档降级到旧固件也安全。不要为普通升级主动解绑。

## 核对后升级

在所选版本源码根目录准备刷写环境与串口识别工具：

```powershell
python -m venv .flash-venv
.\.flash-venv\Scripts\python.exe -m pip install -r installer\requirements-flash.txt
.\.flash-venv\Scripts\python.exe -m serial.tools.list_ports -v
```

下面以**当前main应用**放在 `FoloToy-AI-Passport.bin` 为例，先校验；其他目标修订必须改用该修订自己的文件和可信校验值，不能套用本例哈希：

```powershell
$appImage = '.\FoloToy-AI-Passport.bin'
$expectedAppSha = '9cb6a743711c24f80f17f286abb4a891125cd8f8aff56e96961fdc8c618fd6cd'
if ((Get-FileHash -LiteralPath $appImage -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedAppSha) {
    throw '应用镜像与目标修订不匹配，停止升级'
}
```

确认目标串口、芯片与上面的分区布局，说明只升级应用及保存边界，并获得设备刷写授权后执行；`COMx`必须换成已确认的串口：

```powershell
.\.flash-venv\Scripts\python.exe -m esptool --chip esp32c3 --port COMx --baud 460800 write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x10000 $appImage
```

等待写后校验及重启，核对运行版本／ELF身份、无需重新配对的连接、穿戴、徽章和已保存日记累计。安装同一目标版本的电脑组件后完全退出重启DSH。核对失败时停止，不反复擦除或换旧固件试刷。

**应用镜像不可写0x0，也不可交给 `installer/flash_firmware.py`。** 该完整镜像刷写器校验匹配manifest后从0x0写入，默认整片擦除；即使传入 `--no-erase`，完整镜像仍会覆盖NVS。调试包里的完整 `flash_args` 也不是保留数据升级命令。首次完整安装另见[部署指南](AI_DEPLOYMENT.md)。
