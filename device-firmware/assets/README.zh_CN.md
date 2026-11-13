<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐、电池参数等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## 鲸鱼角色

鲸鱼搭子复用[已审核的 R06 角色图集](images/whale-atlas.png)，16 个姿态保持原有逐行顺序。
安装 Pillow 后运行 `python tools/make_whale_poses.py`，生成
`main/poses.rgb` 中的 196 × 196 RGB565A8 图像。素材来自项目已有设计，
未附带单独的许可元数据。生成的图像数据存放在 Flash。

## 电池参数（battery）

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
保存万锂新能源 500 mAh 电芯的 CW2017 参数（profile2、XIL40X、2026-10-20）。
该文件是项目提供的 `喜梨_智能工卡_万锂新能源500mAh_profile2_XIL40X_20261020.txt`
的原样副本，按维护者要求保留用于本次固件更新；原文件没有附带单独的许可说明。

格式为 80 行两位十六进制数字，每行一个字节，按寄存器顺序排列。
BSP 在 `components/bsp/src/bsp_battery.c` 中嵌入这 80 字节，主机检查会验证
源文件及数组内容完全一致。文本文件 SHA-256：
`7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`。
解码后 80 字节 SHA-256：
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`。
这是电量计参数，不是充电配置，也不能通用于所有 500 mAh 电芯；详见
[硬件指南](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md#9-cw2017-电池计)。

## 鲸鱼陪伴 v1.3 素材

已确认的六张姿态与两件咖啡道具存于 `images/companion-v130/`；四个原创 8-bit 音效与四句固定合成语音存于 `music/whale-cues-v130/`。原始图集、生成提示、语音选择及评审记录见 [`design/v1.3.0`](../../design/v1.3.0)。咖啡 PNG 由已评审 SVG 渲染；语音沿用已确认的晓伊合成与离线机械感处理，没有采样游戏角色原声。`tools/make_whale_companion.py` 将这些源素材转换成 Flash 中的 `main/companion.rgb`、`main/cues.pcm` 和偏移表，无需联网即可重建。

## 成就徽章穿戴饰品

[`images/accessories-v180.png`](images/accessories-v180.png) 为 784 × 784 的 PNG RGBA 4 × 4 叠加图集，在 196 × 196 画布上保存四套皮肤（女仆装、见习魔女、长袖睡衣、连帽家居服）对应的四组互斥穿戴饰品图层：镂空镜片金丝眼镜＋星芒领饰（`W1`）、铃兰发饰＋珍珠耳麦（`W2`）、星光尾绶带（`W3`）与怀抱小鲸玩偶（`W4`）。素材基于项目既有形象与皮肤参考生成并在对话中完成评审；角色署名仍见 `LICENSES/ATTRIBUTION.txt`。安装 Pillow 后运行 `python tools/make_whale_accessories.py`，即可将 25 个 4 字节对齐的 RGB565A8 贴片（`main/accessories.rgb` 共 `119,272` 字节）及 `4 × 4 × 10` 姿态族锚点映射（覆盖姿态 `{1, 3, 10, 15, 16, 17, 18, 19, 20, 21}`，趴睡/伏案休息时摘下眼镜与麦克风仅留发饰与尾绶带，手部占用及其他非怀抱姿态将 `W4` 玩偶放在身侧，四套皮肤的主页活动均保持可见）生成至 `main/whale_accessories.h`。

独立身侧玩偶源图 [`images/accessories-v180-plush.png`](images/accessories-v180-plush.png) 使用 imagegen 从现有图集中的蓝色玩偶重建，移除人物双手与衣袖，保留透明背景；沿用本项目角色素材的归属说明。生成器将其裁切并缩放为 42×27 的共用贴片，原有站立怀抱贴片继续保留；不增加 LVGL 对象或动态图片缓冲区。
