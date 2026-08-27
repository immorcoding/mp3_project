# GUI Canvas Compositor 测试

本目录以主机 CMake 工程验证 `Service/gui/canvas` 的局部毛玻璃像素合成和 Canvas
输出描述符行为，不依赖 STM32 HAL、FreeRTOS 或显示硬件。

当前覆盖：

- 矩形区域只替换区域内的清晰像素；
- 圆形区域的四角保持清晰，避免产生方形模糊块；
- 圆角矩形的四角保持清晰，同时保持边缘连续。
- 越出壁纸边界的区域被拒绝，避免像素访问越界。
- LVGL Canvas 未填写 `data_size` 时，GUI Service 返回的模糊图描述符会补齐其
  实际字节数，以便局部合成器安全校验并使用。

测试中的 `fakes/` 只提供该私有 Canvas Interface 所需的最小 LVGL 与 LCD 定义，
不替代固件使用的真实 LVGL。运行方式：

```text
cmake -S Tests/gui_canvas_compositor -B Tests/gui_canvas_compositor/build
cmake --build Tests/gui_canvas_compositor/build --config Debug
ctest --test-dir Tests/gui_canvas_compositor/build -C Debug --output-on-failure
```
