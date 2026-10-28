# 主机回归

`external_loader`、`flash_ftl`、`gui_task`、`gui_theme`、`gui_canvas`、`gui_font`、`w25qxx`、`resource_pack`、`storage_audio_stream`、`storage_catalog`、`playback_service`、`playback_task` 与 `monitor_warn` 是十三个独立的 CMake 工程，共 45 个 CTest：`external_loader` 1 个，`flash_ftl` 16 个（3 个固定测试、12 个文件场景测试、1 个卷感知 Service 测试），`gui_task` 4 个（含音乐分区曲名客户），`gui_theme` 1 个，`gui_canvas` 2 个，`gui_font` 2 个（字体 bin 解码器：真实字体对独立参照加构造样本与失败输入，只编译解码器与 Component，不链接 LVGL，保真边界见模块 README），`w25qxx` 1 个，`resource_pack` 1 个，`storage_audio_stream` 1 个，`storage_catalog` 2 个（曲目显示名提取纯函数；曲目文本纯函数经它链接真实 `id3_reader` 与 `cc936.c`；保真边界见模块 README），`playback_service` 5 个（真实 minimp3 加小 MP3 夹具，输出管线链接假 Platform 音频），`playback_task` 8 个（纯 C 状态机 `playback_machine`，假存储窗口加假播放模块），`monitor_warn` 1 个（Monitor 未登记任务告警的去重记录，纯 C）。

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
测试影响映射、未知路径回退和状态语义见 [分层验证与 Agent Harness](../docs/shape/sources/verification.md)。
