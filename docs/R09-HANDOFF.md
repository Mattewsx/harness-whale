# R09 发布基线（v0.09）

本仓库衍生自 [1ikeMcFlurry/harness-whale-companion](https://github.com/1ikeMcFlurry/harness-whale-companion)，在其基础上继续开发 R06–R09。上游代码版权归原作者所有，本仓库新增部分以 [MIT](../LICENSE) 授权。

## 仓库组成

| 目录 | 内容 |
| --- | --- |
| `harness/` | DeepSeek Harness 插件（TypeScript） |
| `installer/` | Windows BLE 桥接器与一键安装器（Python） |
| `tests/` | 回归测试与固件回放 |
| `device-firmware/` | ESP32-C3 固件源码（ESP-IDF 5.5.3） |

## 发布物

| 项 | 值 |
| --- | --- |
| 版本 | `0.09` |
| 标签 | `v0.09` |
| 固件内置版本 | `whale-R09-2.2` |
| 固件文件 | `Harness-Whale-ESP32C3-8MB.bin` |
| 写入地址 | `0x0`（完整镜像） |
| SHA-256 | `2d9e978f1271a88f96177431e4b986e21fcb6f292f59fa94b5b42b582018a526` |

完整镜像会覆盖 NVS 与蓝牙配对，刷写后需要重新配对。R09 没有发布应用分区增量镜像。

## 已验收范围

- 电脑端 31 项测试通过，含重启后仅一条心跳、归档与恢复、删除、并发实时状态优先、分段历史正文读取。
- Python 编译与 TypeScript 插件打包通过。
- ESP32-C3 交叉编译、合并镜像与 ELF 归档校验通过；固件 C 状态机主机回放覆盖正文滚动、跨段回读、同任务新回复、忽略其他会话回复、任务归档与既有单选流程。
- 实机记录：COM3 写入 `0x10000` 应用分区验证通过，启动版本 `whale-R09-2.2`，LVGL 就绪，CW2017 报告电量 96%，配对与认证握手正常。实体按键跨段下翻/回翻未在该次实机测试中确认，仅由状态机回放覆盖。

## 接手续跑

- 电脑端：在独立 Python 环境安装 `installer/requirements-bridge.txt`，再在仓库根目录执行 `python -m unittest discover -s tests -v`。插件回放需要支持 TypeScript 类型剥离的 Node.js。
- 固件端：以 ESP-IDF 5.5.3 构建 `device-firmware/`，跨端回放见 `tests/firmware-replay/RUN.txt`。
- 安装电脑端：`python installer/install_companion.py --non-interactive --skip-key`，然后重启 DSH。

## 已知限制

- 插件 ID 仍为 `harness-whale-companion`，与仓库名 `harness-whale` 不一致，属有意保留以兼容已安装环境。
- Release 只托管 `.bin`；插件、桥接器、安装器、固件源码与 manifest 均在代码仓库中。
- 历史基线见 [R08 交接说明](R08-HANDOFF.md)。
