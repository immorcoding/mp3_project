# GUI 模拟器（实验）

在 Windows PC 上运行**真实的** LVGL v8.3.11、SquareLine 导出的 `GUI/` 与 `Service/gui`，用 SDL2 窗口替代 ST7789 + FT6X36。目的是在不烧录的情况下迭代 GUI 运行时（Canvas、Pager、主题、Queue、Transport）。

本工程是独立 host CMake 工程，不进入目标固件，不是板级验收（`PASS_HOST_ONLY` 也不算，见 `docs/verification.md`）。

## 构建与运行

需要 MinGW-w64 GCC、CMake ≥ 3.22、Ninja。SDL2 2.32.10 官方 MinGW 开发包由 CMake 首次配置时下载，并以 SHA256 固定。

```powershell
cmake -S Tools/gui_simulator -B build/gui_simulator -G Ninja -DCMAKE_C_COMPILER=gcc
cmake --build build/gui_simulator
./build/gui_simulator/gui_simulator.exe
```

窗口为 240×320 的 2 倍放大（`SIM_DISPLAY_ZOOM`）。鼠标左键 = 触摸；`T` 切换 Default/Solid 主题；`Esc` 退出。启动后约 7 s 进入 Lock，向上拖动解锁。

## 脚本参数（无人值守截图）

时间以 `Service_GUI_Init()` 完成为 0，单位为 ms；坐标为 LCD 像素。给出任一动作时，默认改用**确定性虚拟时钟**（每圈主循环固定推进 5 ms，与墙钟无关，同样输入总得到逐像素相同的帧），最后一个动作完成后自动退出。

| 参数 | 含义 |
| --- | --- |
| `--shot T:name` | T 时保存 `<out-dir>/name.bmp`，并打印 `[shot] name <帧哈希>` |
| `--tap T:x,y` | T 时点按（按住 80 ms） |
| `--drag T:x0,y0:x1,y1:D` | T 时按下，D ms 内线性拖动后松开 |
| `--key T:c` | T 时触发按键 `c` |
| `--out-dir DIR` | 截图目录，默认当前目录 |
| `--hidden` | 不显示窗口 |
| `--realtime` | 脚本模式下仍按墙钟运行 |

示例（解锁、播放、切到 Queue 并截图）：

```powershell
./build/gui_simulator/gui_simulator.exe `
  --drag 6000:120,290:120,80:250 --tap 7500:120,260 `
  --tap 9000:125,28 --shot 9500:queue
```

## 场景回归

`scenarios/*.args` 每行一个参数（`#` 开头为注释），`scenarios/*.expected` 记录每张截图的帧哈希。

```powershell
./Tools/gui_simulator/run-scenarios.ps1              # 全部场景与基线比对
./Tools/gui_simulator/run-scenarios.ps1 -Scenario queue
./Tools/gui_simulator/run-scenarios.ps1 -Update      # 有意的视觉变化确认后重写基线
```

截图保存在 `build/gui_simulator/shots/<场景>/`。哈希不一致时打开对应 BMP 与上一版比对；只有确认变化是有意的，才用 `-Update` 重写基线，并在提交说明里写明原因。PC 上 FPS/内存浮层已关闭，避免实现细节让基线漂移。

## 替身边界

只替换 `Service/gui` 已有的 Seam，不改生产代码：

| 产品依赖 | 模拟器替身 |
| --- | --- |
| `Platform/lcd`：`SetTransferCallback` / `StartWrite` | `fakes/platform_lcd_sim.c`：同步拷入 SDL 帧缓冲，随即发布 `TRANSFER_COMPLETE` |
| `Platform/touch`：`IsAvailable` / `ReadRawPoint` | `fakes/platform_touch_sim.c`：鼠标左键或脚本触点 |
| FreeRTOS `FreeRTOS.h` / `task.h` | `shim/` 同路径头 + `fakes/freertos_sim.c`：Tick 取 `SDL_GetTicks()`，通知为计数器 |
| 链接脚本资源区 `__external_resource_vinyl_*` | `fakes/resource_sim.c`：asm 定义同名符号，启动时拷入 `Resources/imgs/vinyl_original_144px.c` |
| `lv_conf.h` | `config/sim_lv_conf.h`：包装产品配置，只关 DMA2D、`.lvgl_large_ram_array` 段与 FPS/内存浮层 |
| APP `gui_music`（依赖 Storage） | `sim_main.c` 内最小演示分区：10 首固定曲目、播放/暂停、上一首/下一首、假进度、选曲 |

## 已知差异

- LCD 写入是同步的，不覆盖 SPI DMA 时序、`wait_cb` 阻塞与 D-Cache 行为。
- 单线程运行，不覆盖 GUI Task 与其他 Task 的并发。
- Canvas 模糊在 PC 上几乎不耗时，耗时与帧率不代表板上表现。
