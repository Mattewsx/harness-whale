[English](RELEASE-v0.18.0-dev2.md)

# DeepSeek Harness 鲸鱼搭子 v0.18.0-dev2

相对已公开的 v0.17.0-demo：

1. 新增连续答题：多个会话等待回答时，按问题到达顺序处理，答完自动打开下一题；看过但未回答的问题仍可继续处理，已读角标保持清除。
2. 新增鲸鱼娘情绪反馈：任务完成时开心庆祝，问题等待较久时表现着急；阅读、录音和互动期间不抢占画面。情绪表现复用现有角色姿态。
3. 修复题目显示：补齐箭头、乘除等常用符号，修复部分选择题只能回电脑处理的问题。
4. 修复选择页返回按钮被标题遮挡的问题；长标题省略显示。

## 使用与升级

从本仓库 main 下载源码 ZIP，解压运行 `python installer/install_companion.py --non-interactive --skip-key`，完全退出并重开 DSH。连续答题需要同步更新电脑端，旧桥接器回退普通列表。无需配网。配对及主要操作见 [README](../README.md)。

完整镜像用于从 0x0 刷入，会重置原设置与配对。已确认分区相同的设备，可按 [升级说明](UPGRADE.md) 仅向 0x10000 写入同版应用镜像以保留数据。应用镜像不可写入 0x0。

## 验证范围

- Build：PASS，ESP-IDF 5.5.3 完整门禁、合并镜像及匹配调试归档验证通过。
- Host tests：PASS，132 项电脑端测试、字体覆盖、原生 LVGL 题目渲染回归及独立代码复审通过。
- Device tests：PASS（本轮范围），下方标识的同一应用镜像已刷入，启动身份及蓝牙重连正常；用户确认题目显示、返回／暂不回答、真实答案提交和自动接下一题正常，原好感、服饰与设置保留。
- Unverified：情绪效果、长期运行、跨日及全部服饰动作组合未在本轮重新做专项实机验收。

## 构建身份

- Full image: `c5f97c1388eab812ad42175d44a78e971f0b3963a3ba91273ba02afbf3afdb2a`
- Application: `0642fe8619d5f61babbd81661238041423164f4d6fde7947b1bc1bee559114f9`
- ELF: `85bbf36f724db8b68b616c6e419279da4234bb014932ecb176c41acfcfd658fc`

固件输入由 [release-source.json](release-source.json) 固定。社区目标为已有玩法 1966，实际审核状态以服务端回执为准；本次未创建 GitHub Release。
