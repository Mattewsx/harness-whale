[English](README.md) · **简体中文**

# 本机渲染回归

测试使用正式 UI、字体、角色素材及 LVGL，显示尺寸为 240 × 320，使用与 BSP
一致的 40 行绘制缓冲。检查不变页面不重绘、计时和电量仅重绘相应区域，并确认
回复更新与角色动效仍显示。另连续切换页面 300 次，检查 32 KB 图形内存池。

ESP-IDF 下载完托管依赖后，用本机 C 编译器运行：

```sh
cmake -S tests/whale_render -B build/whale-render -DCMAKE_BUILD_TYPE=Release
cmake --build build/whale-render
ctest --test-dir build/whale-render --output-on-failure
```

使用本机编译器，不要使用 ESP32-C3 交叉编译器。Release 构建中测试断言仍然
启用。此测试不代替实际硬件时序或射频验证；固件验证通过后仍需观察实机。
