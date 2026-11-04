**English** · [简体中文](README.zh_CN.md)

# Native rendering regression

This test uses the production UI, fonts, poses, and LVGL with a 240 × 320 display
and the BSP's 40-line draw buffer. It checks unchanged pages do not redraw, clock
and battery updates redraw only their regions, and reply/motion changes remain
visible. It also switches pages 300 times and checks the 32 KB graphics pool.

After ESP-IDF has downloaded managed components, build with a native C compiler:

```sh
cmake -S tests/whale_render -B build/whale-render -DCMAKE_BUILD_TYPE=Release
cmake --build build/whale-render
ctest --test-dir build/whale-render --output-on-failure
```

Use a host compiler, not the ESP32-C3 cross compiler. Assertions stay enabled in
the test even in Release builds. The test does not prove hardware timing or RF
behavior; observe the actual device after a verified firmware build.
