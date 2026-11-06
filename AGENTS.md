# Harness Whale Companion — AI 操作说明

这是 FoloToy AI Passport 与 DeepSeek Harness 的个人 BLE 陪伴 Demo。当前分支为 v1.3.0，协议 2.2；公开社区版本以发布回执为准。先读 [README](README.md) 与任务相关说明；修改固件还须读 [固件 AGENTS](device-firmware/AGENTS.md)。

## 安全与授权

- 保留已有修改，先检查 `git status --short --branch`，在功能或发布分支工作。
- 不读取、打印、记录、复制或提交 API Key、密码、Token、私钥；使用凭据管理器正常认证，不把凭据写入 remote URL。
- 不提交本机身份、真实会话、`.dsh` 内容或原始日志，使用合成测试数据。
- 不修改无关 DSH 插件、profile 或系统 Python，使用独立 `.venv`。
- 本版隐藏余额，安装使用 `--non-interactive --skip-key`。如用户主动配置旧版余额接口，必须由用户在隐藏提示中输入 Key。
- 构建、提交和发布不会授权刷机。刷写前只读识别串口，确认镜像、偏移及数据影响，并获得用户授权。
- 标准 `installer/flash_firmware.py` 只支持完整镜像：校验后整片擦除并写入 0x0。相同布局的应用升级按 [UPGRADE](docs/UPGRADE.md) 写入 0x10000；两种镜像不得混用。

## 产品约束

- BLE 连接本机电脑，无需 Wi-Fi；桥接器由插件自动启动。
- 只显示未归档主会话，排除子代理；归档、删除同步移除条目和缓存。
- 主页显示鲸鱼娘、标题、状态、用时和设备电量；进度仅在有真实 Todo 时显示。
- 每会话只读最近一次最终回复，默认收起；↓ 展开，↑↓ 连续滚动，无页码，确认收起。
- 内存缓存 32 个分段，约 13 KB，首段优先保留。断电、新修订、删除会话、桥接器 epoch 变化和解绑按范围清理缓存；正文不写 NVS。
- 仅普通单选支持硬件作答，多选、自由文本与复杂补充回电脑。
- BLE 名称 `HARNESS-WHALE`，诊断 `/whale`；插件 ID `harness-whale-companion` 保留安装兼容。

## 源码地图

- `harness/plugin/index.ts`：事件、会话生命周期、单选及桥接器管理。
- `harness/bridge/harness_ble_bridge.py`：UDP、DSH 名单与本地记录、BLE 连接。
- `harness/bridge/whale_v2.py`：当前 2.2 协议；`harness_ble_protocol.py` 保留旧版兼容和文本清洗。
- `device-firmware/main/whale_app.c`：应用入口、状态机与按键。
- `device-firmware/main/whale_ui.c`：LVGL 布局与动效。
- `device-firmware/main/whale_reply_cache.c`：内存缓存。
- `device-firmware/components/bsp/`：硬件支持。

## 验证与发布

电脑端运行 `python -m unittest discover -s tests -v`、`python -m compileall -q harness installer tests` 和 README 中的 esbuild 命令；插件回放需 Node.js 22.18+。

固件激活 ESP-IDF 5.5.3，在 `device-firmware/` 运行 `./tools/validate.sh`。协议变更同步 Python `whale_v2.py`、C `whale_wire.*` 与跨语言回放；UI 变更运行原生 LVGL 测试。

根目录工作流是整合 CI 入口。发布同步源码、manifest、版本说明和附件，验证源码来源及镜像/ELF 身份；二进制放 Release，不提交 Git，保留已发布标签。分别报告 Build、Host tests、Device tests、Unverified，不把无故障日志视为全部硬件验收。
