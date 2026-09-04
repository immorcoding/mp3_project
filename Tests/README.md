# 主机回归

`flash_ftl`、`w25qxx` 与 `resource_pack` 是三个独立的 CMake 工程，共 18 个 CTest：`flash_ftl` 16 个（3 个固定测试、12 个文件场景测试、1 个卷感知 Service 测试），`w25qxx` 1 个，`resource_pack` 1 个。

## 环境

需要 CMake 3.22 或更新版本、CMake 套件中可从 `PATH` 找到的 `ctest`、Ninja，以及可在 Windows 本机运行测试的主机 GCC。可显式指定编译器（以下为参数示例，不是独立命令）：

```powershell
-HostCompiler E:/mingw64/bin/gcc.exe
```

未指定时，脚本依次使用 `CC` 和 `PATH` 中的 `gcc`。仅接受 Windows x86/x64 本机 GCC：目标三元组须包含 `mingw`、`msys`、`cygwin` 或 `windows`，且 CPU 须为 `x86_64`、`amd64`、`x64`、`i386`、`i486`、`i586`、`i686` 或 `x86`。拒绝 ARM 交叉 GCC、RISC-V、MIPS、Clang 与其他目标，因为它们不能在当前 Windows x86/x64 主机运行。
编译器选择优先级固定为：显式 `-HostCompiler` 优先；其次为已设置且非空白的 `CC`；仅当 `CC` 未设置或为空白时，才回退到 `PATH` 中的 `gcc`。显式 `-HostCompiler` 或非空白 `CC` 无法解析或不符合上述要求时立即失败，绝不回退到其他工具链。

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
测试影响映射、未知路径回退和状态语义见 [分层验证与 Agent Harness](../docs/verification.md)。
