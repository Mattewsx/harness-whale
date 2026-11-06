# 🐋 Harness Whale Companion

把 DeepSeek Harness 的多个工作会话变成一只陪你工作的鲸鱼娘。电脑通过 BLE 连接 FoloToy AI Passport，插件自动管理桥接器，无需 Wi-Fi。

当前分支为 **v1.4.0 整合候选版，尚未发布**，整合省电、续航记录修复、桌面时钟和蓝牙大帧优化，见 [整合与验证记录](docs/RELEASE-v1.4.0.zh_CN.md)。陪伴互动、状态音效／语音及导航继续保留。请使用同一交付包中的配套桥接器与固件，更新电脑组件后完全退出并重启 DSH。

[下载已发布的 v1.3.1](https://github.com/Mattewsx/harness-whale/releases/tag/v1.3.1) · [v1.3.1 更新公告](docs/ANNOUNCEMENT-v1.3.1.zh_CN.md) · [部署指南](docs/AI_DEPLOYMENT.md)。v1.3.1 已完成实机验收；v1.4.0 整合镜像的基础实机验收通过；随后补充的充电重启记录修复已通过回归和构建，新镜像尚未刷入。离线深睡、实际续航及长期稳定性等仍待专项验证。AI Passport 社区玩法为项目 1966，公开版本和审核状态以社区页面为准。

![R10.1 界面预览](docs/images/r101-preview.png)

上图使用正式 UI、字体与角色素材通过 LVGL 渲染，非设备实拍；标题、正文和电量为测试数据。

## 功能与按键

- 列表只显示 DSH 工作区中未归档的主会话，排除子代理；归档或删除后同步移除。
- 任务主页显示大幅鲸鱼娘、标题、状态、用时、连接状态和设备电量。
- 表达待命、思考、执行、等待、完成、错误、停止等工作状态，支持普通单选作答。
- 每个会话只读最近一次最终回复；新回复替换旧内容，没有历史回答切换。
- 回复通过上下键连续滚动，没有页码。约 13 KB 内存缓存 32 个传输分段，重复展开已有缓存不再请求。断电清空缓存，超长正文中被淘汰的部分需重新获取。
- 仅在存在真实 Todo 数据时显示进度；账户余额已隐藏，无需提供 API Key。
- 陪伴页提供打招呼、摸摸、喂小鱼、喝咖啡；离线可玩，工作事件优先，主页短按 ↑ 进入。
- v1.4.0 起，符合全局空闲条件时，主页或陪伴页无操作 2 分钟后进入桌面摆件：顶部大号时间（桥接器连上时校准，断开后靠芯片计时），中间是鲸鱼娘，底部一行日期和电量；夜里（23:00–7:00）只显示暗色时钟。会话开始干活即回到任务主页，按任意键返回。
- 完成、待选择、出错和断连有不同声音提示；音效／语音两种模式，默认语音。手动停止不提醒，录音期间暂缓提示。
- 无操作两分钟调暗、五分钟息屏；离线十分钟且已息屏时深睡，按键唤醒，也会定时短暂醒来等待电脑。桌面时钟遵循相同规则，详见 [省电说明](docs/POWER.zh_CN.md)。
- 桥接器和设备协商蓝牙大帧，加快列表、标题和回复传输；旧版本或 MTU 不足时保留原有帧长，`/whale` 可查看协商结果。

| 界面 | ↑ | ↓ | 确认 |
| --- | --- | --- | --- |
| 任务主页 | 短按陪伴；长按直接开始语音录音 | 展开最近回复 | 打开列表，或处理待答问题 |
| 回复 | 上滚；到开头后收起 | 下滚，或重试读取 | 收起回复 |
| 会话列表 | 上一个；第一条上方为设备设置 | 下一个 | 打开任务／设备设置 |
| 普通单选 | 上一个选项 | 下一个选项 | 确认当前选择 |
| 陪伴 | 上一个互动 | 下一个互动；长按打开列表 | 执行互动；长按返回任务主页 |
| 桌面摆件 | 返回任务主页 | 返回任务主页 | 返回任务主页 |

任务主页和回复界面长按 ↓ 进入列表，长按确认显示进度并进入设备设置，也可通过列表顶部的设备设置入口进入；设置返回原入口，声音／连接／版本子页返回设置。多选、自由输入和复杂补充仍在电脑上完成。

## 硬件与电脑要求

预编译固件针对 **FoloToy AI Passport：ESP32-C3、8 MB Flash、240 × 320 屏幕、三按键**。不要直接刷入其他 ESP32-C3 板卡；引脚以 [BSP](device-firmware/components/bsp/include/bsp_pins.h) 为准。

安装流程已在 Windows 10/11 64 位验证，要求 Python 3.10～3.13、蓝牙和 DeepSeek Harness。USB 数据线用于刷机，日常同步使用 BLE。

## 安装与升级

后续版本继续通过 USB 更新固件与素材；BLE OTA 方案已取消（[PR #7](https://github.com/Mattewsx/harness-whale/pull/7) 已关闭，未合入）。保持现有单应用分区，素材随应用一起更新，日常蓝牙同步与语音传输不受影响。相同分区布局下仍可使用下述保留配对升级方式。

下载 [v1.3.1 Release](https://github.com/Mattewsx/harness-whale/releases/tag/v1.3.1) 中的 `companion-installer.zip`，或使用 `v1.3.1` 标签源码。在解压目录执行：

```powershell
python installer\install_companion.py --non-interactive --skip-key
```

安装器创建独立 `.venv`，安装插件与桥接器，并更新受管 DSH profile 块。安装后完全退出并重启 DSH，输入 `/whale` 查看连接。v1.3.0 的全局空闲判断与工作提醒需要同步更新桥接器；旧桥接器仅保留手动陪伴。

交付包的安装器已包含匹配完整固件和 manifest。需要从头刷写时执行以下命令；不要混用旧版附件：

```powershell
python -m venv .flash-venv
.\.flash-venv\Scripts\python.exe -m pip install -r installer\requirements-flash.txt
.\.flash-venv\Scripts\python.exe installer\flash_firmware.py
```

标准安装器校验 SHA-256 后整片擦除并从 **0x0** 写入。旧固件、NVS、设置与配对会被清除。刷写前确认目标串口与数据影响。

已有相同分区布局的鲸鱼固件时，可按 [升级步骤](docs/UPGRADE.md) 使用 `Harness-Whale-v1.3.1-app-0x10000.bin` 保留配对。应用镜像只写 **0x10000**，不可交给完整镜像安装器，也不可从 0x0 写入。

## 语音输入

在 DSH 中启用本地语音识别（SenseVoice），等待模型准备完成。任务主页长按 ↑ 开始录音，确认键停止；检查识别文字，按确认打开操作，再明确选择发送。可重新录音或取消。文字只发往当前主会话，运行中的任务会排队接收。

设备边录边通过蓝牙传输，停止后在电脑本地转文字，不上传云端语音服务。老版本升级后若语音尚未就绪，检查本地识别是否准备完成；蓝牙缓存旧服务时需电脑和设备两端解绑后重新配对。详情见 [R11 说明](device-firmware/docs/whale-r11.zh_CN.md) 和 [术语校正边界](docs/VOICE-TERMS.zh_CN.md)。

会话列表只显示名称和彩色状态，待回答、未读错误、未读回复优先；查看对应内容后清除提醒。默认语音，可在设备设置切换为音效；完成音效为 85% 音量、150 毫秒。实机已确认语音发送成功且不重复、四种互动与声音切换后再次录音正常；长期稳定性仍未验证。

## 构建与验证

激活 ESP-IDF **5.5.3** 后执行：

```sh
cd device-firmware
./tools/validate.sh
```

完整门禁包含主机回归、交叉编译和合并镜像校验。结果为 `device-firmware/build/FoloToy-AI-Passport-full.bin`；匹配 ELF/MAP 存于 `build/firmware/<sha256>/`。

电脑端测试从仓库根目录执行：

```sh
python -m pip install -r installer/requirements-bridge.txt
python -m unittest discover -s tests -v
python -m compileall -q harness installer tests
npx --yes esbuild@0.25.10 harness/plugin/index.ts --bundle --platform=node --format=esm --outfile=plugin-check.mjs
```

插件回放需支持 TypeScript 类型剥离的 Node.js 22.18+。另有 [跨语言回放](tests/firmware-replay/RUN.txt) 与 [LVGL 渲染回归](device-firmware/tests/whale_render/README.zh_CN.md)。根目录 `.github/workflows/checks.yml` 是整合 CI 入口；`device-firmware/.github/` 保留固件独立工程定义，不会自动成为本仓库工作流。

## 结构与隐私

| 目录 | 用途 |
| --- | --- |
| `harness/plugin/` | DSH 事件、会话生命周期、普通单选代理 |
| `harness/bridge/` | 本机 UDP、DSH 本地记录、BLE 协议与回复传输 |
| `device-firmware/` | 应用、BSP、UI、字库和素材 |
| `installer/` | 安装器、完整固件刷写器与校验清单 |
| `tests/` | 电脑端回归与 Python→C 回放 |

UDP 仅绑定本机回环地址。BLE 传输会话标题、状态、问题选项和最近最终回复；回复可能包含敏感信息，请仅与自己的设备配对。设备缓存正文不写入 NVS。API Key 不进入 BLE/UDP 帧。旧版余额模块保留兼容，但本版隐藏余额，部署时使用 `--skip-key`。详见 [架构](docs/ARCHITECTURE.md) 和 [协议](docs/PROTOCOL.md)。

## 发布文件与验证范围

Release 包含完整镜像、应用升级镜像、匹配调试符号、`SHA256SUMS.txt` 和发布说明。源码由 GitHub 对应标签提供；二进制不进入 Git 历史。

R10.1 已通过构建、主机回归及实机启动/BLE/名单同步；用户确认最新回复、连续滚动、收起与重复展开正常。长时间稳定性、新回复实时替换及缓存请求次数仍需进一步实机验证，详见 [验收说明](docs/RELEASE-v1.2.0.zh_CN.md)。这是个人 Demo，硬件兼容范围有限。

## 来源与许可

基于 [1ikeMcFlurry/harness-whale-companion](https://github.com/1ikeMcFlurry/harness-whale-companion) 继续开发，复用 [FoloToy AI Passport](https://gitee.com/FoloToy/ai-passport) BSP，角色设计参考 [dsh-whale-musume](https://github.com/Sutera-Diffusus/dsh-whale-musume)。保留原作者版权与第三方组件各自许可；本仓库新增部分以 [MIT](LICENSE) 授权。
