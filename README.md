# 🐋 Harness Whale Companion

把 DeepSeek Harness 的多个工作会话变成一只陪你工作的鲸鱼娘。电脑通过 BLE 连接 FoloToy AI Passport，插件自动管理桥接器，无需 Wi-Fi。

当前 GitHub Release 为 **v0.17.0-demo**。完整固件、保留设置的应用镜像和配套电脑组件见 [GitHub Release](https://github.com/Mattewsx/harness-whale/releases/tag/v0.17.0-demo)。升级时请同时更新电脑组件。

`main` 为已通过本轮验收的 **v0.18.0-dev2**，整合页面调度、连续答题和着急／庆祝情绪，并修复题目符号缺失和返回按钮遮挡。连续答题独立按问题到达顺序取题，不受已读角标和列表分页影响；普通列表保持未读优先。使用此版社区固件时，下载本仓库 main 源码 ZIP，解压运行下方电脑端安装命令；旧桥接器回退普通列表。情绪暂用现有姿态，详见 [调度说明](docs/DIRECTOR.zh_CN.md) 和 [本轮验证范围](docs/RELEASE-v0.18.0-dev2.zh_CN.md)。此版尚无 GitHub Release，社区审核状态以服务端为准；本分支安装清单对应 dev2 镜像，请勿混用旧版固件清单。

## 小房间与会话

- 开机进入鲸鱼娘的小房间。左上角为蓝牙状态，中央爱心的填充表示亲密度，右上角显示设备电量。日常动作随心情和昼夜变化。
- 底部默认选中会话。↑ 往左、↓ 往右、确认进入。互动、服饰、会话列表和设置的第一项上方有返回入口，长按 ↑ 可直接回主页。配对与正在提交的操作有独立确认流程。
- 互动包含打招呼、摸摸、喂小鱼、喝咖啡和服饰。亲密度从 0 开始，和真实工作挂钩：在设备上回答的单选被接受 +3，会话完成或有新回复 +2，语音发送成功 +2，每次互动 +1（每天最多 +5，可离线玩）。每天合计最多 +10，工作奖励和互动次数分别保存，重启后保留额度，上限 99（按电脑同步的本地日期计算；离线所得也计入，校时回拨不会刷新额度）。一天里既没有互动也没有完成工作，亲密度会冷掉 1 点，但不会低于已解锁配饰的门槛。服饰改为整套换装：默认女仆装，亲密度到 20 解锁见习魔女，到 40 解锁鲸鱼头套长袖睡衣，到 60 解锁普通软兜帽家居服，解锁后互动页会提示一次。换装只影响主页和互动页，会话页仍只显示工作状态。三套皮肤各有完整的待机和互动姿态；保留小蓝蝴蝶结，去除王冠。衣橱选中可预览，未解锁时确认会提示门槛；已穿皮肤再次确认恢复默认女仆装。
- 会话图标角标统计全局有未读事项的主会话数。浏览列表保留标记；看过题目即清除此条角标，暂不回答仍可回到列表找到问号；看过最近回复或错误后清除相应标记。会话详情的角色只展示工作状态。
- 在列表用 ↑↓ 选择，按确认打开。待单选的会话直接显示题目和选项，↑↓ 选择、再次确认提交，第一项上方可选“暂不回答”；长题干可长按 ↓ 阅读全文。有未读新回复时直接打开正文，确认收起回详情。其余会话进入详情，↑ 回列表、↓ 读最近回复、确认进入语音。权限审批默认选中拒绝；允许前须阅读完整请求并再次确认。多选题按确认勾选，选完在末尾提交。工具名、全部参数及权限理由无法完整展示时，审批回到电脑处理；自由文本与多题组合也在电脑处理。
- 语音边录边传，按确认结束。电脑本地转写后可选发送、重新录音或取消；长文本先上下阅读，再按确认进入操作。回复连续滚动，已读分段缓存在设备内存中。
- 声音、蓝牙和版本由主页设置进入。保留两分钟空闲时钟、五分钟息屏和原有状态提醒。手动互动时工作提醒更新角标，时钟遇到工作事件可返回相关会话。

待机页新增今日小结，可在“设置 → 待机画面”切换为纯时钟。统计电脑桥接器实际观察到的已结束任务，每天本地时间 05:00 换日；运行中的任务和重启前未观察到的任务不会补算。蓝牙连接前累计的数据会在首次连接时保存，较宽的数字自动分成两行显示。

小房间在白天、亮屏且所有会话空闲时，会偶尔发出打招呼、摸摸、喂小鱼或喝咖啡邀请。选左侧互动图标进入，默认选中她邀请的动作，再按确认回应；也可以选择别的动作或不理会。忽略后安静收尾，不扣亲密度、不播放提示音，也不会延长调暗和息屏。详细规则见[随机互动](docs/RANDOM-INTERACTIONS.zh_CN.md)。

## 硬件与电脑要求

预编译固件针对 **FoloToy AI Passport：ESP32-C3、8 MB Flash、240 × 320 屏幕、三按键**。不要直接刷入其他 ESP32-C3 板卡；引脚以 [BSP](device-firmware/components/bsp/include/bsp_pins.h) 为准。

安装流程已在 Windows 10/11 64 位验证，要求 Python 3.10～3.13、蓝牙和 DeepSeek Harness。USB 数据线用于刷机，日常同步使用 BLE。

## 安装与升级

后续版本继续通过 USB 更新固件与素材；BLE OTA 方案已取消（[PR #7](https://github.com/Mattewsx/harness-whale/pull/7) 已关闭，未合入）。保持现有单应用分区，素材随应用一起更新，日常蓝牙同步与语音传输不受影响。相同分区布局下仍可使用下述保留配对升级方式。

解压同版配套电脑组件，在解压目录或本分支根目录执行：

```powershell
python installer\install_companion.py --non-interactive --skip-key
```

安装器创建独立 `.venv`，安装插件与桥接器，并更新受管 DSH profile 块。安装后完全退出并重启 DSH，输入 `/whale` 查看连接。v1.3.0 的全局空闲判断与工作提醒需要同步更新桥接器；旧桥接器仅保留手动陪伴。

配套电脑组件包只负责安装桥接器。完整固件单独交付。使用下方完整刷写器前，将同一版合并镜像复制至 installer/firmware/Harness-Whale-ESP32C3-8MB.bin 并核对同版 manifest：

```powershell
python -m venv .flash-venv
.\.flash-venv\Scripts\python.exe -m pip install -r installer\requirements-flash.txt
.\.flash-venv\Scripts\python.exe installer\flash_firmware.py
```

标准安装器校验 SHA-256 后整片擦除并从 **0x0** 写入。旧固件、NVS、设置与配对会被清除。刷写前确认目标串口与数据影响。

已有相同分区布局的鲸鱼固件时，可按 [升级步骤](docs/UPGRADE.md) 使用 `Harness-Whale-v0.17.0-demo-app-0x10000.bin` 保留配对。应用镜像只写 **0x10000**，不可交给完整镜像安装器，也不可从 0x0 写入。

## 语音输入

在 DSH 中启用本地语音识别（SenseVoice），等待模型准备完成。从会话详情短按确认开始录音，再按确认停止。核对识别文字，选择发送或取消。文字只发往当前主会话，运行中的任务会排队接收。

离开电脑时也可以派新任务：打开会话列表，最上面一行是“语音新任务”（麦克风图标），在第一条会话上按 ↑ 就到；按确认开始录音，屏幕显示“我在听”后说出指令，再按确认结束，和会话里的语音回复操作一样。屏幕上方显示“新任务 · 文件夹名”，即新会话将要打开的文件夹：列表里刚才高亮的那条会话所在文件夹，列表为空时用电脑上最近活跃的会话所在文件夹。核对文字后选择发送，电脑上会新开一个会话并把这句话作为第一条消息；同一段录音最多只开一个会话。电脑上一个会话都没有时设备会提示先在电脑打开会话。新会话只开在已有会话用过的文件夹，不接受其他路径。

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
| `harness/plugin/` | DSH 事件、会话生命周期、单选、多选与权限审批代理 |
| `harness/bridge/` | 本机 UDP、DSH 本地记录、BLE 协议与回复传输 |
| `device-firmware/` | 应用、BSP、UI、字库和素材 |
| `installer/` | 安装器、完整固件刷写器与校验清单 |
| `tests/` | 电脑端回归与 Python→C 回放 |

UDP 仅绑定本机回环地址。BLE 传输会话标题、状态、问题选项和最近最终回复；回复可能包含敏感信息，请仅与自己的设备配对。设备缓存正文不写入 NVS。API Key 不进入 BLE/UDP 帧。旧版余额模块保留兼容，但本版隐藏余额，部署时使用 `--skip-key`。详见 [架构](docs/ARCHITECTURE.md) 和 [协议](docs/PROTOCOL.md)。

## 验证范围与历史版本

本地 v0.17.0-demo 的镜像、调试归档、桥接器和验证记录在本次交付目录内。以下实机记录属于此前版本。

R10.1 已通过构建、主机回归及实机启动/BLE/名单同步；用户确认最新回复、连续滚动、收起与重复展开正常。长时间稳定性、新回复实时替换及缓存请求次数仍需进一步实机验证，详见 [验收说明](docs/RELEASE-v1.2.0.zh_CN.md)。这是个人 Demo，硬件兼容范围有限。

## 来源与许可

基于 [1ikeMcFlurry/harness-whale-companion](https://github.com/1ikeMcFlurry/harness-whale-companion) 继续开发，复用 [FoloToy AI Passport](https://gitee.com/FoloToy/ai-passport) BSP，角色设计参考 [dsh-whale-musume](https://github.com/Sutera-Diffusus/dsh-whale-musume)。保留原作者版权与第三方组件各自许可；本仓库新增部分以 [MIT](LICENSE) 授权。
