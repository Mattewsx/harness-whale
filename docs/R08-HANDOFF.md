# R08 本地开发基线

本分支包含 DeepSeek Harness 插件、Windows BLE 桥接器和回归测试。配套固件在 FoloToy AI Passport 衍生仓库的 `feature/dsh-whale-ble` 分支，已验收源码提交为 `7ed69d206158273d2f14b38dc15b30903ad93eae`。固件内置版本为 `whale-R08-2.2`。

| 已验收文件 | 写入地址 | SHA-256 |
| --- | --- | --- |
| Whale-R08-app-0x10000.bin | 0x10000 | `98b9bfbd340d3387e1ffc8585ee98584d7bc32c033e13562d661993ad46d7fae` |
| Whale-R08-0x0.bin | 0x0 | `56b583fd09c9ed851f8435fbfac8758b6489cc97b629bca3528ccf9e7e36ad9f` |

同分区布局的 R06/R07 升级可仅写应用分区以保留 NVS 和配对。完整镜像覆盖 NVS。上述摘要标识已验收产物，重新构建后应重新生成校验清单。

## 接手续跑

- 电脑端在本仓库根目录执行 `python -m unittest discover -s tests -v`。插件回放需要支持 TypeScript 类型剥离的 Node.js。
- 固件端以 ESP-IDF 5.5.3 运行 AI Passport 仓库的 `tools/validate.sh --all`。跨端回放见 [运行说明](../tests/firmware-replay/RUN.txt)。
- 安装电脑端可运行 `python installer/install_companion.py --non-interactive --skip-key`，然后重启 DSH。日常设备列表按工作区成员减去归档项筛选，再排除子代理。
- 已验收范围包括应用分区升级并保留蓝牙配对、回复阅读、归档时从阅读页退回列表、实体按键答题，以及日常工作区与实际 GATT 快照成员一致。

## 正式发布前

确认发布仓库和版本号后，更新 README 下载入口、安装器镜像清单、版本及对应 SHA-256，并校验打包后的源码、镜像与匹配的 ELF/MAP。现有 `installer/firmware/manifest.json` 与 README 的上游 Release 链接对应旧版 1.1.1。
