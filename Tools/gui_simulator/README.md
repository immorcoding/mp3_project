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

窗口为 240×320 的 2 倍放大（`SIM_DISPLAY_ZOOM`）。鼠标左键 = 触摸；`T` 切换 Default/Solid 主题；`C` 在拔卡与重新挂载之间切换并把曲库代次 +1（拔卡：真实 `gui_music` 按窗口协议 CLEAR 清空 Queue；假播放后端在播放或暂停中按拔卡停止，无当前曲，播放键、时间、进度与唱盘随快照归零；无卡再按播放，开启约 100 ms 后按 NOT_READY 停止；重新挂载：Queue 重新 request，无当前曲故无高亮）；`G` 模拟重扫：代次 +1，但已显示的 Queue 窗口保留到下一次 request，用于制造过期窗口（点旧窗口的行，假后端按 CATALOG_CHANGED 停止）；`E` 模拟重扫得到空库（再按一次恢复固定曲库），代次 +1，之后按播放得到 `No tracks` 与曲名 `--`；`S` 挂起/恢复假播放后端的命令处理（挂起时命令只入队、位置照走，FIFO 满后 `playback_command_send()` 返回队列满，stdout 打印 `[playback] queue full` 与 `[log]` 行；恢复后一圈处理完）；`B` 让假后端之后一次开启应答为坏文件（连按 k 次则之后 k 次开启都坏）：按 B5 沿最近一次移动的方向跳到下一首、跳过数 +1，连跳数达到曲目数（16）即按 ALL_BAD 停止；`R` 让假后端按 K1 枚举顺序以下一个非 NONE 原因立即停止（停止态也再停一次，停止次数 +1；CARD_REMOVED、NOT_READY、NO_TRACK、CATALOG_CHANGED 变为无当前曲，AUDIO_FAULT 锁存，之后停止态按播放或点行只让停止次数 +1，重启模拟器才解除）；`A` 在 stdout 打印 `[anim] running N`（LVGL 运行中动画数，核对唱盘旋转与状态行闪烁不叠加）；`L` 以 300 ms 淡出回到 Lock（产品暂无重新锁屏入口，只用于核对 Lock 呼吸随屏幕重启）；`Esc` 退出。启动后约 5 s 进入 Lock，向上拖动解锁。

## 脚本参数（无人值守截图）

时间以 `Service_GUI_Init()` 完成为 0，单位为 ms；坐标为 LCD 像素。给出任一动作时，默认改用**确定性虚拟时钟**（每圈主循环固定推进 5 ms，与墙钟无关，同样输入总得到逐像素相同的帧），最后一个动作完成后自动退出。

| 参数 | 含义 |
| --- | --- |
| `--shot T:name` | T 时保存 `<out-dir>/name.bmp`，并打印 `[shot] name <帧哈希>` |
| `--tap T:x,y` | T 时点按（按住 80 ms） |
| `--drag T:x0,y0:x1,y1:D` | T 时按下，D ms 内线性拖动后松开 |
| `--anims T:name` | T 时打印 `[anims] name N`（LVGL 运行中动画数）；`run-scenarios.ps1` 以键 `anims.name` 与基线比对 |
| `--key T:c` | T 时触发按键 `c` |
| `--name-fail T:N` | T 时切换假 Storage 第 N 首（播放列表位置）的名字是否取不到：取不到时按生产防御分支应答代次 0，stdout 打印 `[storage] Name fail idx=N gen=G` |
| `--name-delay T:K` | T 起假 Storage 名称槽 PENDING 后多等 K 圈（每圈 5 ms）再应答；0 恢复为下一圈应答。用于拍空白帧与过期应答丢弃 |
| `--out-dir DIR` | 截图目录，默认当前目录 |
| `--hidden` | 不显示窗口 |
| `--realtime` | 脚本模式下仍按墙钟运行 |
| `--font-unavailable` | 启动时不安装字体资源：`Service_Resource_GetFont()` 报告不可用（同板上仍是资源包 v2），GUI 记 WARN，曲名回退 Montserrat 12 |

示例（解锁、播放、切到 Queue 并截图）：

```powershell
./build/gui_simulator/gui_simulator.exe `
  --drag 6000:120,290:120,80:250 --tap 7500:120,260 `
  --tap 9000:125,28 --shot 9500:queue
```

## 场景回归

`scenarios/*.args` 每行一个参数（`#` 开头为注释），`scenarios/*.expected` 记录每张截图的帧哈希。当前场景：`boot_lock`（开机序列）、`unlock_main`、`music_transport`（播放/暂停/切歌/拖进度）、`queue`（选曲、滚动、Library）、`pager`（循环翻页与回弹）、`theme_toggle`（运行时切主题）、`theme_default`（Default 毛玻璃随横滑重裁剪）、`vinyl_rotation`（唱盘播放起转、暂停保角、恢复续转、切歌归零、翻页进出、反复切换与清空）、`music_snapshot`（停止态开播、总时长未知到已知、暂停冻结、恢复、切歌归零、Queue 不自动跟随）、`music_paused_switch`（暂停中上一首/下一首保持暂停、暂停中点行开播）、`music_echo`（挂起后端时冻结与位置照走、点行触发队列满撤回高亮、恢复后追上）、`music_seek`（拖动跟手、松手回位：总时长未知、播放中、暂停中）、`music_card_removed`（拔卡停止归零、状态行 `No SD card` 与闪烁、Queue 清空、无卡按播放得到 `SD card not ready`、重新挂载后开播）、`music_stop_reasons`（`G` 后点旧行得到 `Library changed`、`R` 逐个拍 9 个停止原因短语、停止后按下一首清除短语、AUDIO_FAULT 锁存后再按播放闪一次）、`music_bad_file`（`Skipped 1 file` 提示与 2 s 到期、连续跳过累加并重新计时、整轮坏文件 `No playable files` 取代提示）、`music_title`（曲名：无当前曲 `--`、短名静止居中、超长英文名循环滚动、拔卡 `--`、空库开播 `--` 与 `No tracks`）、`music_title_play_start`（应答延迟 8 圈时开播：开启应答把代次 0 → 1 后名字保留、不闪空白）、`music_title_switch`（应答延迟 20 圈时切歌：应答前曲名区留空、时间 `0:00/--:--`，应答后显示新名）、`music_title_next_burst`（应答延迟 20 圈时连按三次 NEXT：过期应答丢弃、全程留空，只显示最后一首）、`music_title_bad_file`（坏文件跳过：候选名与 `Skipped 1 file` 同时显示）、`music_title_unavailable`（`--name-fail` 让第 2 首取不到：曲名 `--`、播放照常，下一首恢复）、`music_title_cjk_short`（中文短名 `晴天` 静止居中，Default 与 Solid 外观）、`music_title_mixed`（中英混排 `Jay Chou 周杰伦 - 晴天` 同一行基线一致）、`music_title_cjk_long`（中文长名循环滚动，相隔 2 s 两帧）、`music_title_missing_glyph`（`繁體 龍 喆 镕`：字集外的 4 个字各画占位框，镕不得画成同一 FORMAT0 子表的首字锝；`a.b.MP3` 显示 `a.b`）、`music_title_font_fallback`（`--font-unavailable`：英文名用 Montserrat 正常、中文画占位框）、`music_title_tagged`（曲名来源：无标签的位置 4 回退文件主名，位置 5 显示假 TIT2 `Experience (Einaudi Live)`）、`queue_tags`（Queue 行三类行态同屏：标题 + 歌手、标题无歌手、回退文件主名；滚到末尾换窗后 `a.b` 与中文文件主名均无 `Music/` 前缀，中文在 Montserrat 下画占位框）、`lock_anim`（Lock 呼吸随屏幕启停：Lock 显示时动画数 1、解锁后 Main 显示期间 0、按 `L` 回到 Lock 后恢复为 1）；共 29 个场景、160 帧与 3 个动画数断言。`scenarios/*.expected` 中 `anims.<name> N` 行是 `--anims` 的动画数断言，与帧哈希一同比对。改动 `Service/gui/`、`APP/tasks/gui/music/`、播放合同头或替身实现的 Storage/Log 头后必须跑（CHANGED 按 `scripts/rules/verification.psd1` 的 `GuiScenarioPathPatterns` 自动加跑）；新增界面时补场景。`vinyl_rotation`、`music_card_removed`、`music_stop_reasons`、`music_title`、`music_title_cjk_long` 的 `--key a` 行在 stdout 打印动画数（`music_stop_reasons` 连按两次播放后只比静止时多一条闪烁动画），`music_echo` 打印队列满日志，均不参与哈希比对，作为日志证据。名为 `*_flash` 的帧拍在约 300 ms 闪烁的中途。

```powershell
./Tools/gui_simulator/run-scenarios.ps1              # 全部场景与基线比对
./Tools/gui_simulator/run-scenarios.ps1 -Scenario queue
./Tools/gui_simulator/run-scenarios.ps1 -Update      # 有意的视觉变化确认后重写基线
./Tools/gui_simulator/run-scenarios.ps1 -SdlSourceDir <已解压的 SDL2 包>  # 复用已下载的 SDL2，不联网
```

截图保存在 `build/gui_simulator/shots/<场景>/`。哈希不一致时打开对应 BMP 与上一版比对；只有确认变化是有意的，才用 `-Update` 重写基线，并在提交说明里写明原因。PC 上 FPS/内存浮层已关闭，避免实现细节让基线漂移。

LVGL 在动画链表为空时暂停动画定时器、新动画开始时恢复，恢复后的首拍不落在原有 `LV_DISP_DEF_REFR_PERIOD` 节拍上；因此「播放前是否还有别的动画在跑」会改变唱盘角度、状态行闪烁透明度与翻页回弹位置的采样相位。#106 停止解锁后的 Lock 呼吸后，正是这一相位变化使 12 个场景的 50 帧重录（布局、颜色、文字不变）。曲名超长时循环滚动是常驻动画，同样会移动其他动画的采样相位。

刷新节拍同样决定曲名的空白能否被拍到：屏幕约每 20 ms 刷新一次，而默认名字晚一圈（5 ms）READY，切歌后的空白通常落不进任何一帧。要拍空白帧或证明“没有空白帧”，场景先用 `--name-delay` 把应答拉长到超过一个刷新周期（`music_title_play_start`、`music_title_switch`、`music_title_next_burst`）。

## 替身边界

编译真实的 `Service/gui` 与 APP 音乐分区（`gui_music*.c`）；只替换它们已有的 Seam，不改生产代码：

| 产品依赖 | 模拟器替身 |
| --- | --- |
| `Platform/lcd`：`SetTransferCallback` / `StartWrite` | `fakes/platform_lcd_sim.c`：同步拷入 SDL 帧缓冲，随即发布 `TRANSFER_COMPLETE` |
| `Platform/touch`：`IsAvailable` / `ReadRawPoint` | `fakes/platform_touch_sim.c`：鼠标左键或脚本触点 |
| FreeRTOS `FreeRTOS.h` / `task.h` | `shim/` 同路径头 + `fakes/freertos_sim.c`：Tick 取模拟器时钟（`sim_clock.c`，交互时为 `SDL_GetTicks()`，脚本时为虚拟时钟），通知为计数器 |
| 链接脚本资源区 `__external_resource_vinyl_*`、`__external_resource_font_*`、GUI 字体解码区 `__gui_font_decode_*` 与壁纸像素区；`Service/resource` 的 `Service_Resource_GetFont()` | `fakes/resource_sim.c`：asm 定义与固件同名、同大小的符号（FONT 区与解码区放 `.bss`），定义 `service_gui_view_wallpaper_pixels`，启动时拷入 `Resources/imgs/` 的唱盘与壁纸打包源，以及 `Resources/fonts/noto_sans_cjk_sc_12_4bpp.bin` 的同一份字节（构建期由 `cmake/embed_bin.cmake` 生成字节数组，不入库）；按生产头实现字体查询，`--font-unavailable` 时报告不可用。真实的 `Service/gui/font` 解码器与字体子模块照常运行；固件专用的 `gui_service_view_wallpaper_region.c` 不编译 |
| Cortex-M7 周期计数器 Adapter | `fakes/cycle_counter_sim.c`：SDL 高精度计数器，只用于字体解码耗时日志（不参与帧哈希，PC 耗时不代表板上） |
| `lv_conf.h` | `config/sim_lv_conf.h`：包装产品配置，只关 DMA2D、`.lvgl_large_ram_array` 段与 FPS/内存浮层 |
| `APP/tasks/storage` 中音乐分区用到的部分：`storage_listbuffer` / `storage_namebuffer` 全局对象、`storage_listbuffer_*`、`storage_namebuffer_*`、`storage_task_sd_is_ready/mounted()` | `fakes/storage_sim.c`：16 首固定曲目——原 10 首英文曲目（第 0–9 首，没有目录与扩展名）、5 个中文与文件名规则样本（第 10–14 首：`Music/a.b.MP3`、`Music/繁體 龍 喆 镕.mp3`、中文长名、`Music/Jay Chou 周杰伦 - 晴天.mp3`、`Music/晴天.mp3`）、最后一首超长英文名（第 15 首，需要滚动；开播后按上一首先绕到它，再依次到第 14、13…首）；超过一窗 12 条，Queue 滚到末尾时按窗口协议换窗。`E` 键切换空库；窗口行与名称槽都按生产头给出 {曲目显示名 `Title`, 歌手 `Artist`}（窗口行不含路径）：`storage_sim_tags` 中的第 5 首（`Experience`）给出假 TIT2 `Experience (Einaudi Live)` 与 TPE1 `Ludovico Einaudi`，第 7 首（`Kiss the Rain`）给出假 TIT2 `Kiss the Rain (Live)`、无 TPE1（不读标签字节，替代生产 `storage_track_text_read()` 的“取到”结果），其余曲目经真实的 `storage_catalog_display_name()` 回退文件主名、歌手为空；窗口一次填完，不模拟生产的逐圈一行（生产窗口填满前保持 `PENDING`，GUI 只见整窗，二者在 GUI 侧不可区分；逐圈排程进板级验收）；不模拟打开失败、读取错误等标签侧回退与读取耗时（这些在板级验收）；`sim_main.c` 每圈先于 `gui_music_step()` 调 `storage_sim_step()`，上一圈 request 的窗口与名字本圈填成 READY；名称槽同生产：代次不符、越界或未挂载应答代次 0；脚本开关 `--name-fail` 让指定位置走防御分支（代次 0），`--name-delay` 让名称槽应答多等若干圈；另给假播放后端提供当前代次与曲目数 |
| `APP/tasks/playback`：`playback_contract.h` 的 `playback_command_send()` / `playback_snapshot_read()` | `fakes/playback_sim.c`：命令 FIFO 深度取 `PLAYBACK_COMMAND_QUEUE_DEPTH`（8）；`sim_main.c` 每圈在 `gui_music_step()` 之前调 `playback_sim_step()`，把 FIFO 取空并回显序号（挂起时跳过），再按虚拟时钟推进；按 #71 K3 命令表、K4 字段与 K5 转换点响应；开启后约 100 ms 应答：确认代次（开机首播从代次 0 变为当前代次）、给出总时长（第 k 首 2:30 + k × 10 s），或按 NOT_READY / CATALOG_CHANGED / NO_TRACK 停止，或按 `B` 键排队的坏文件跳过（B5）；播放中位置按虚拟时钟前进，播完自动前进；拔卡时有流则按 CARD_REMOVED 停止；`R` 键注入其余停止原因，AUDIO_FAULT 锁存（K3、B10） |
| `Service/log`：`Service_Log_Post()` | `fakes/log_sim.c`：同步打印 `[log] <等级> <tag>: <text>` |

## 已知差异

- LCD 写入是同步的，不覆盖 SPI DMA 时序、`wait_cb` 阻塞与 D-Cache 行为。
- 单线程运行，不覆盖 GUI Task 与其他 Task 的并发。
- Canvas 模糊在 PC 上几乎不耗时，耗时与帧率不代表板上表现。
- 字体解码在 PC（64 位指针）上需要 619,640 B，板上（32 位）为 618,952 B，两者都放进同样大小的解码区；字形渲染与板上同为 LVGL 软件渲染，但不覆盖 SDRAM/Cache 行为与板上解码耗时，启动耗时与真屏观感进板级验收。
- 假 Storage 没有 Storage Task 与 SD 消抖：已挂载即就绪，窗口与名字默认晚一圈 READY（名字可用 `--name-delay` 加长），拔卡/挂载/重扫在按键当圈完成，不覆盖真实扫描耗时与消抖期间“已挂载但不可 request”的状态。
- 假播放后端是按命令响应的简化状态机：不解码、不读块，没有 T_data/T_out 计时；坏文件只由 `B` 键产生，以开启 OK 代替输出 Start 清零连跳计数；READ_ERROR、NO_DATA、AUDIO_ERROR、AUDIO_FAULT、ALL_BAD 等原因可由 `R` 键直接注入（停止态也可再停），不代表真实后端会在停止态产生这些事实；开启应答固定约 100 ms。状态行提示与闪烁按虚拟时钟计时，真机 tick 下的时长与可读性进板级验收。命令在发出后的下一圈（5 ms）回显，不代表真机优先级下的回显延迟，回显时序进板级验收。
