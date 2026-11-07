# 🐋 Harness Whale Companion

把 DeepSeek Harness 的多个工作会话变成一只陪你工作的鲸鱼娘。电脑通过 BLE 连接 FoloToy AI Passport，插件自动管理桥接器，无需 Wi-Fi。

本地开发分支为 **v0.15.0-demo**，基于 v1.4.0。使用本次交付的电脑桥接器和固件体验完整的未读统计。

## 小房间与会话

- 开机进入鲸鱼娘的小房间。左上角为蓝牙状态，中央爱心的填充表示亲密度，右上角显示设备电量。日常动作随心情和昼夜变化。
- 底部默认选中会话。↑ 往左、↓ 往右、确认进入。互动、服饰、会话列表和设置的第一项上方有返回入口，长按 ↑ 可直接回主页。配对与正在提交的操作有独立确认流程。
- 互动包含打招呼、摸摸、喂小鱼、喝咖啡和服饰。完成互动会增加亲密度，每天最多 +5（按电脑同步的本地日期计算；离线所得也计入，校时回拨不会刷新额度），可离线玩。雏菊发夹和圆框眼镜初始锁定，衣橱支持已解锁配饰叠戴、再次选择脱下和一键脱下。本期不设解锁任务。
- 会话图标角标统计全局有未读事项的主会话数。浏览列表保留标记，看过题目可暂不回答，看过最近回复或错误后清除相应标记。会话详情的角色只展示工作状态。
- 在列表用 ↑↓ 选择，按确认打开。待单选的会话直接显示题目和选项，↑↓ 选择、再次确认提交，第一项上方可选“暂不回答”；长题干可长按 ↓ 阅读全文。有未读新回复时直接打开正文，确认收起回详情。其余会话进入详情，↑ 回列表、↓ 读最近回复、确认进入语音。多选、审批等在电脑上处理。
- 语音边录边传，按确认结束。电脑本地转写后可选发送或取消；长文本先上下阅读，再按确认进入操作。回复连续滚动，已读分段缓存在设备内存中。
- 声音、蓝牙和版本由主页设置进入。保留两分钟空闲时钟、五分钟息屏和原有状态提醒。手动互动时工作提醒更新角标，时钟遇到工作事件可返回相关会话。

## 硬件与电脑要求

预编译固件针对 **FoloToy AI Passport：ESP32-C3、8 MB Flash、240 × 320 屏幕、三按键**。不要直接刷入其他 ESP32-C3 板卡；引脚以 [BSP](device-firmware/components/bsp/include/bsp_pins.h) 为准。

安装流程已在 Windows 10/11 64 位验证，要求 Python 3.10～3.13、蓝牙和 DeepSeek Harness。USB 数据线用于刷机，日常同步使用 BLE。

## 安装与升级

后续版本继续通过 USB 更新固件与素材；BLE OTA 方案已取消（[PR #7](https://github.com/Mattewsx/harness-whale/pull/7) 已关闭，未合入）。保持现有单应用分区，素材随应用一起更新，日常蓝牙同步与语音传输不受影响。相同分区布局下仍可使用下述保留配对升级方式。

解压本地 Demo 的配套电脑组件，在解压目录或本分支根目录执行：

```powershell
python installer\install_companion.py --non-interactive --skip-key
```

安装器创建独立 `.venv`，安装插件与桥接器，并更新受管 DSH profile 块。安装后完全退出并重启 DSH，输入 `/whale` 查看连接。v1.3.0 的全局空闲判断与工作提醒需要同步更新桥接器；旧桥接器仅保留手动陪伴。

本地电脑组件包只负责安装桥接器。完整固件单独交付。使用下方完整刷写器前，将同一版合并镜像复制至 installer/firmware/Harness-Whale-ESP32C3-8MB.bin 并核对同版 manifest：

```powershell
python -m venv .flash-venv
.\.flash-venv\Scripts\python.exe -m pip install -r installer\requirements-flash.txt
.\.flash-venv\Scripts\python.exe installer\flash_firmware.py
```

标准安装器校验 SHA-256 后整片擦除并从 **0x0** 写入。旧固件、NVS、设置与配对会被清除。刷写前确认目标串口与数据影响。

已有相同分区布局的鲸鱼固件时，可按 [升级步骤](docs/UPGRADE.md) 使用 `Harness-Whale-v0.15.0-demo-app-0x10000.bin` 保留配对。应用镜像只写 **0x10000**，不可交给完整镜像安装器，也不可从 0x0 写入。

## 语音输入

在 DSH 中启用本地语音识别（SenseVoice），等待模型准备完成。从会话详情短按确认开始录音，再按确认停止。核对识别文字，选择发送或取消。文字只发往当前主会话，运行中的任务会排队接收。

设备边录边通过蓝牙传输，停止后在电脑本地转文字，不上传云端语音服务。老版本升级后若语音尚未就绪，检查本地识别是否准备完成；蓝牙缓存旧服务时需电脑和设备两端解绑后重新配对。详情见 [R11 说明](device-firmware/docs/whale-r11.zh_CN.md) 和 [术语校正边界](docs/VOICE-TERMS.zh_CN.md)。

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

## 验证范围与历史版本

本地 v0.15.0-demo 的镜像、调试归档、桥接器和验证记录在本次交付目录内。以下实机记录属于此前版本。

R10.1 已通过构建、主机回归及实机启动/BLE/名单同步；用户确认最新回复、连续滚动、收起与重复展开正常。长时间稳定性、新回复实时替换及缓存请求次数仍需进一步实机验证，详见 [验收说明](docs/RELEASE-v1.2.0.zh_CN.md)。这是个人 Demo，硬件兼容范围有限。

## 来源与许可

基于 [1ikeMcFlurry/harness-whale-companion](https://github.com/1ikeMcFlurry/harness-whale-companion) 继续开发，复用 [FoloToy AI Passport](https://gitee.com/FoloToy/ai-passport) BSP，角色设计参考 [dsh-whale-musume](https://github.com/Sutera-Diffusus/dsh-whale-musume)。保留原作者版权与第三方组件各自许可；本仓库新增部分以 [MIT](LICENSE) 授权。
