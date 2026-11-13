<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, sound effects, and battery profiles, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

## Whale character

The whale companion uses [the approved R06 character atlas](images/whale-atlas.png).
Its 16 poses retain their original row-major order. Run
`python tools/make_whale_poses.py` with Pillow to generate `main/poses.rgb` as
196 × 196 RGB565A8 images. The existing project design supplied this artwork
without separate license metadata. The generated image data stays in Flash.

## Battery profiles

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
contains the supplied CW2017 profile for the Wanli New Energy 500 mAh cell
(profile2, XIL40X, 2026-10-20). It is an unchanged copy of the project-supplied
file, retained at the maintainer's request for this firmware update. Its
original filename is recorded in the paired Chinese document. No separate
license was included in the supplied file.

The format is 80 lines of two hexadecimal digits, one byte per line in register
order. The BSP embeds those bytes in `components/bsp/src/bsp_battery.c`; the
host gate checks the source file and initializer for an exact match. Text-file
SHA-256: `7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`.
Decoded 80-byte SHA-256:
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`.
This is a fuel-gauge profile, not a charging configuration or a generic profile
for every 500 mAh cell; see the [hardware guide](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md#9-cw2017-fuel-gauge).

## Whale companion v1.3 materials

Approved six poses and two coffee props: `images/companion-v130/`. Approved four original chiptune cues and four fixed synthetic voice clips: `music/whale-cues-v130/`. Art origin, generation prompt, voice selection and review evidence remain in [`design/v1.3.0`](../../design/v1.3.0). Coffee PNGs are rasterizations of the reviewed SVG props. Voice clips use the approved Xiaoyi synthesis and offline clockwork processing, without sampled game-character audio. `tools/make_whale_companion.py` converts these sources into the Flash-backed `main/companion.rgb`, `main/cues.pcm` and offset descriptors; no network operation is required to rebuild them.

## Achievement badge accessories

[`images/accessories-v180.png`](images/accessories-v180.png) is a 784 × 784 PNG RGBA 4 × 4 overlay atlas storing the four mutually exclusive wearable accessory sets across all four skins (maid, apprentice witch, long-sleeved pajamas, hooded loungewear) on their 196 × 196 canvas: hollow-lens gold glasses + collar star brooch (`W1`), lily-of-the-valley hairpin + pearl ear communicator (`W2`), starlight tail ribbon (`W3`), and lap baby-whale plushie (`W4`). Generated for this project from the approved character and skin references and reviewed in chat; character attribution remains in `LICENSES/ATTRIBUTION.txt`. Run `python tools/make_whale_accessories.py` with Pillow to pack the 25 cropped 4-byte-aligned RGB565A8 patches (`119,272` bytes in `main/accessories.rgb`) and the `4 × 4 × 10` pose-family placement map (`poses {1, 3, 10, 15, 16, 17, 18, 19, 20, 21}` in `main/whale_accessories.h`, with glasses/mic removed during sleep/table rest and `W4` resting beside the character when the hands are busy, remaining visible throughout all four skins' HOME activities).

The standalone plush source [`images/accessories-v180-plush.png`](images/accessories-v180-plush.png) was reconstructed with imagegen from the existing blue plush reference, removing human hands and sleeves while preserving transparency; the project character attribution still applies. The packer crops and scales it into a shared 42-by-27 patch. Existing held-plush patches remain in use for compatible poses, without adding LVGL objects or dynamic image buffers.
