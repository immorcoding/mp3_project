# GUI Canvas Compositor 测试

本目录以主机 CMake 工程验证 `Service/gui/canvas` 的局部毛玻璃像素合成和 Canvas
输出描述符行为，不依赖 STM32 HAL、FreeRTOS 或显示硬件。

当前覆盖：

- 矩形区域只替换区域内的清晰像素；
- 圆形区域的四角保持清晰，避免产生方形模糊块；
- 圆角矩形的四角保持清晰，同时保持边缘连续。
- 从完整模糊帧裁剪出的子图使用连续像素布局，尺寸与目标对象区域完全一致，可安全绑定为
  该对象的 Background image；对象平移时子图会随对象一起绘制。
- 带透明越界填充的裁剪在区域部分移出壁纸时，仍保留目标对象尺寸和全局像素坐标；越界部分
  为全零透明像素，避免访问越界。
- 清晰/模糊区域合成仍拒绝越界区域，避免调用方传入无效的全局合成坐标。
- LVGL Canvas 未填写 `data_size` 时，GUI Service 返回的模糊图描述符会补齐其
  实际字节数，以便局部合成器安全校验并使用。

测试中的 `fakes/` 只提供该私有 Canvas Interface 所需的最小 LVGL 与 LCD 定义，
不替代固件使用的真实 LVGL。运行方式：

```text
cmake -S Tests/gui_canvas_compositor -B Tests/gui_canvas_compositor/build
cmake --build Tests/gui_canvas_compositor/build --config Debug
ctest --test-dir Tests/gui_canvas_compositor/build -C Debug --output-on-failure
```
