# 主机回归

`external_loader`、`flash_ftl`、`filesystem_locator`、`gui_task`、`music_gui`、`gui_theme`、`gui_canvas`、`gui_font`、`w25qxx`、`resource_pack`、`storage_audio_stream`、`storage_catalog`、`ui_cell`、`playback_service`、`playback_task` 与 `monitor_warn` 是十六个独立的 CMake 工程，共 70 个 CTest：`external_loader` 1 个，`flash_ftl` 31 个（3 个固定测试、12 个文件场景测试、1 个卷感知 Service 测试、1 个完成等待测试、1 个共用 FreeRTOS 桩自测、13 个入口加锁测试：Debug 与 Release 配置各一组，断言停机与统计用例各在独立进程里），`filesystem_locator` 1 个（把 `patches/fatfs/` 的补丁应用到 `ff.c` 副本后，经 Filesystem Service 的目录扫描与“按定位符打开”入口测 12000 首同文件夹的内存 FAT32 镜像；保真边界见模块 README），`gui_task` 1 个（GUI 输入单槽），`music_gui` 5 个（音乐应用 GUI 分区 `APP/apps/music/gui/` 的窗口协议、传输归约、曲名客户、Queue 行两列文本与进度条跳转归约 `gui_music_seek_reducer`），`gui_theme` 1 个，`gui_canvas` 2 个，`gui_font` 2 个（字体 bin 解码器：真实字体对独立参照加构造样本与失败输入，只编译解码器与 Component，不链接 LVGL，保真边界见模块 README），`w25qxx` 1 个，`resource_pack` 1 个，`storage_audio_stream` 1 个（越过标签纯函数与跳转偏移钳位），`storage_catalog` 3 个（曲目显示名提取纯函数；曲目文本纯函数经它链接真实 `id3_reader` 与 `cc936.c`；曲库按文件定位符打开：真实曲库扫描、`storage_track_open`/`storage_track_text_read` 与诊断出口，Filesystem/Log Service 与 DWT 用假实现；保真边界见模块 README），`ui_cell` 1 个（APP 私有的界面数据短锁：整份拷贝、序号、没变不拷贝不碰锁、拿不到锁返回假且数据不变、写后在锁外叫醒、传空不叫醒、调试版写者唯一断言；链接真实 `ui_cell.c` 与共用 FreeRTOS 桩，保真边界见模块 README），`playback_service` 6 个（真实 minimp3 加小 MP3 夹具，输出管线链接假 Platform 音频；含毫秒到字节映射与从落点重开的精度测试），`playback_task` 11 个（纯 C 状态机 `playback_machine`，假存储窗口加假播放模块；含跳转 `playback_task_seek` 与切歌时延 `playback_task_switch`），`monitor_warn` 2 个（Monitor 未登记任务告警的去重记录；5 s 批次的 CPU 占比与 Health 行，均纯 C）。

## 环境

需要 CMake 3.22 或更新版本、CMake 套件中可从 `PATH` 找到的 `ctest`、Ninja，以及可在 Windows 本机运行测试的主机 GCC。可显式指定编译器（以下为参数示例，不是独立命令）：

```powershell
-HostCompiler E:/mingw64/bin/gcc.exe
```

未指定时，脚本依次使用 `MP3_HOST_CC` 和 `PATH` 中的 `gcc`；不读通用的 `CC`，以免本机其他项目设置的 Clang 误伤本仓库。仅接受 Windows x86/x64 本机 GCC：目标三元组须包含 `mingw`、`msys`、`cygwin` 或 `windows`，且 CPU 须为 `x86_64`、`amd64`、`x64`、`i386`、`i486`、`i586`、`i686` 或 `x86`。拒绝 ARM 交叉 GCC、RISC-V、MIPS、Clang 与其他目标，因为它们不能在当前 Windows x86/x64 主机运行。
编译器选择优先级固定为：显式 `-HostCompiler` 优先；其次为已设置且非空白的 `MP3_HOST_CC`；仅当 `MP3_HOST_CC` 未设置或为空白时，才回退到 `PATH` 中的 `gcc`。显式 `-HostCompiler` 或非空白 `MP3_HOST_CC` 无法解析或不符合上述要求时立即失败，绝不回退到其他工具链。

每个模块构建到 `build/host/<module>/`。在仓库根目录执行：

```powershell
# 完整回归
./scripts/test-host.ps1 -HostCompiler E:/mingw64/bin/gcc.exe

# 按模块回归
./scripts/test-host.ps1 -HostCompiler E:/mingw64/bin/gcc.exe -Module flash_ftl

# 提交前核对分层 include、生成目录写保护、固件 Debug/Release 构建与主机回归
./scripts/verify.ps1 -HostCompiler E:/mingw64/bin/gcc.exe

# 手动按当前工作树变化选择受影响模块（pre-push 会按实际推送提交自动执行）
./scripts/verify_changed.ps1 -HostCompiler E:/mingw64/bin/gcc.exe
```

这些命令只在主机上配置、构建和运行测试；不会烧录固件、安装软件或访问硬件。
主机工程共用的 FreeRTOS 桩在 [`stubs/freertos/`](stubs/freertos/README.md)：`flash_ftl` 的文件基准、完成等待与入口加锁测试、`filesystem_locator` 经它取头文件、互斥锁和假 tick，桩自身的行为由 `flash_ftl` 工程里的 `freertos_stub_behavior` 测试。Filesystem 的持锁统计依赖的周期计数器与 Service Log 的替身在 [`stubs/lock_env/`](stubs/lock_env/lock_env_stub.h)（假时间只在测试调用 `lock_env_advance_us` 时前进，日志行可读）。
测试影响映射、未知路径回退和状态语义见 [分层验证与 Agent Harness](../docs/shape/sources/verification.md)。
