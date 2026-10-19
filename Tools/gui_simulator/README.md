# GUI 模拟器

在 Windows PC 上运行**真实的** LVGL v8.3.11、`Service/gui`（含手写界面 `view/`）与 APP 音乐分区 `APP/tasks/gui/music`，用 SDL2 窗口替代 ST7789 + FT6X36。目的是在不烧录的情况下迭代界面与 GUI 运行时（Canvas、Pager、主题、Queue、Transport），并用场景回归守住界面等价性（ADR-0016）。

本工程是独立 host CMake 工程，不进入目标固件，不是板级验收（`PASS_HOST_ONLY` 也不算，见 `docs/shape/sources/verification.md`）。

## 构建与运行

需要 MinGW-w64 GCC、CMake ≥ 3.22、Ninja。SDL2 2.32.10 官方 MinGW 开发包由 CMake 首次配置时下载，并以 SHA256 固定。

```powershell
cmake -S Tools/gui_simulator -B build/gui_simulator -G Ninja -DCMAKE_C_COMPILER=gcc
cmake --build build/gui_simulator
./build/gui_simulator/gui_simulator.exe
```

窗口为 240×320 的 2 倍放大（`SIM_DISPLAY_ZOOM`）。鼠标左键 = 触摸；`T` 切换 Default/Solid 主题；`C` 在拔卡与重新挂载之间切换并把曲库代次 +1（拔卡：真实 `gui_music` 按窗口协议 CLEAR，Queue 清空、paused、进度与唱盘归零，游标作废后选曲/上一首/下一首/播放不生效；重新挂载：游标回到第 0 首，Queue 重新 request 并高亮第 0 首）；`G` 模拟重扫：代次 +1、游标按新代次回到第 0 首，但已显示的 Queue 窗口保留到下一次 request（代次不符，不再高亮），用于制造过期窗口；`A` 在 stdout 打印 `[anim] running N`（LVGL 运行中动画数，核对唱盘动画不叠加）；`Esc` 退出。启动后约 5 s 进入 Lock，向上拖动解锁。

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

`scenarios/*.args` 每行一个参数（`#` 开头为注释），`scenarios/*.expected` 记录每张截图的帧哈希。当前场景：`boot_lock`（开机序列）、`unlock_main`、`music_transport`（播放/暂停/切歌/拖进度）、`queue`（选曲、滚动、Library）、`pager`（循环翻页与回弹）、`theme_toggle`（运行时切主题）、`theme_default`（Default 毛玻璃随横滑重裁剪）、`vinyl_rotation`（唱盘播放起转、暂停保角、恢复续转、切歌归零、翻页进出、反复切换与清空）。改动 `Service/gui/`、`APP/tasks/gui/music/` 或替身实现的 Storage 头后必须跑（CHANGED 按 `scripts/rules/verification.psd1` 的 `GuiScenarioPathPatterns` 自动加跑）；新增界面时补场景。`vinyl_rotation` 的 `--key a` 行在 stdout 打印动画数，不参与哈希比对，作为日志证据。

```powershell
./Tools/gui_simulator/run-scenarios.ps1              # 全部场景与基线比对
./Tools/gui_simulator/run-scenarios.ps1 -Scenario queue
./Tools/gui_simulator/run-scenarios.ps1 -Update      # 有意的视觉变化确认后重写基线
./Tools/gui_simulator/run-scenarios.ps1 -SdlSourceDir <已解压的 SDL2 包>  # 复用已下载的 SDL2，不联网
```

截图保存在 `build/gui_simulator/shots/<场景>/`。哈希不一致时打开对应 BMP 与上一版比对；只有确认变化是有意的，才用 `-Update` 重写基线，并在提交说明里写明原因。PC 上 FPS/内存浮层已关闭，避免实现细节让基线漂移。

## 替身边界

编译真实的 `Service/gui` 与 APP 音乐分区（`gui_music*.c`），以及真实的 `storage_playback_cursor.c`；只替换它们已有的 Seam，不改生产代码：

| 产品依赖 | 模拟器替身 |
| --- | --- |
| `Platform/lcd`：`SetTransferCallback` / `StartWrite` | `fakes/platform_lcd_sim.c`：同步拷入 SDL 帧缓冲，随即发布 `TRANSFER_COMPLETE` |
| `Platform/touch`：`IsAvailable` / `ReadRawPoint` | `fakes/platform_touch_sim.c`：鼠标左键或脚本触点 |
| FreeRTOS `FreeRTOS.h` / `task.h` | `shim/` 同路径头 + `fakes/freertos_sim.c`：Tick 取模拟器时钟（`sim_clock.c`，交互时为 `SDL_GetTicks()`，脚本时为虚拟时钟），通知为计数器 |
| 链接脚本资源区 `__external_resource_vinyl_*` 与壁纸像素区 | `fakes/resource_sim.c`：asm 定义 vinyl 同名符号、定义 `service_gui_view_wallpaper_pixels`，启动时拷入 `Resources/imgs/` 的唱盘与壁纸打包源；固件专用的 `gui_service_view_wallpaper_region.c` 不编译 |
| `lv_conf.h` | `config/sim_lv_conf.h`：包装产品配置，只关 DMA2D、`.lvgl_large_ram_array` 段与 FPS/内存浮层 |
| `APP/tasks/storage` 中音乐分区用到的部分：`storage_listbuffer` 全局对象、`storage_listbuffer_*`、`storage_task_sd_is_ready/mounted()` | `fakes/storage_sim.c`：10 首固定曲目；`sim_main.c` 每圈先于 `gui_music_step()` 调 `storage_sim_step()`，上一圈 request 的窗口本圈填成 READY；挂载、拔卡与重扫时代替 `storage_catalog` 调用游标 `init/invalidate`（游标退场后删除） |

## 已知差异

- LCD 写入是同步的，不覆盖 SPI DMA 时序、`wait_cb` 阻塞与 D-Cache 行为。
- 单线程运行，不覆盖 GUI Task 与其他 Task 的并发。
- Canvas 模糊在 PC 上几乎不耗时，耗时与帧率不代表板上表现。
- 假 Storage 没有 Storage Task 与 SD 消抖：已挂载即就绪，窗口固定晚一圈 READY，拔卡/挂载/重扫在按键当圈完成，不覆盖真实扫描耗时与消抖期间“已挂载但不可 request”的状态。
- 真实 `gui_music`（游标版）不按时间走进度条，播放中进度条停在当前值，直到播放快照接入。
