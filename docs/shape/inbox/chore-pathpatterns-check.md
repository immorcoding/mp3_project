# Inbox: chore/pathpatterns-check

> 以下是 2026-10-19 新增 `scripts/check-test-paths.ps1`（接入 FAST）并补齐 `verification.psd1` 漂移时记下的 Signal 与待办；规则正文未改。drain 时提给用户。本分支基于 `chore/check-doxygen`（PR #91）。

## Signal

- 2026-10-19 · harness · cite · HAR-11 · 主机模块的 `PathPatterns` 靠手写，会随 include 演进而漂移：`gui_task` 缺 `APP/tasks/playback/playback_contract.h`（`gui_music_transport` 的 reducer 测试依赖它，三张 ticket 的 inbox 都报过未修），`playback_task` 的 Service/playback 头也曾手补。`scripts/check-test-paths.ps1` 现在按真实编译结果核对：对每个模块 `cmake -G Ninja` 配置 `Tests/<模块>/`（只配置不构建，并行）、读 `compile_commands.json`、对每条命令加 `-MM` 取依赖，要求每个仓库内依赖都被该模块某个 PathPatterns 命中（自身 `Tests/<模块>/` 树豁免），否则以 `模块 <- 路径` 报告并失败。首次运行发现 22 处缺口（flash_ftl 8、gui_task 5、gui_theme 4、playback_service 4、gui_canvas 1），已补进 `verification.psd1` 并加路由断言。
- 2026-10-19 · harness · friction · HAR-5 · 缺口集中在跨层聚合头与配置头（`Service/service.h`、`Platform/platform.h`、`Platform/*/platform_*.h`、`cortex_m7_dcache_adapter_config.h`、`APP/app_config.h`、`Service/log/log_service*.h`），不是业务头。这些头一改就会选中 4~6 个模块，CHANGED 变慢但正确；它们本来就在 HardwareSensitive 内，不影响上板判定。
- 2026-10-19 · harness · signal · HAR-5 · 检查器盲区（写在脚本头注释）：只在别的宏定义下才被包含的头不会被发现；CMake `file(READ)` 复制改写的输入（`flash_ftl` 的 `ffconf.h`/`fatfs.h`）、链接脚本、测试数据与 `CMakeLists.txt` 本身不经 `#include`，仍靠手写模式或 `Tests/<模块>/` 整树豁免；PathPatterns 写得过宽（反方向）不检查。FAST 因此新增对 `cmake`、`ninja` 与主机 GCC 的依赖（FULL 本就需要；`scripts/AGENTS.md` 写的“不为规则读取或 Hook 引入额外依赖”针对规则读取与 Hook，未改）。
- 2026-10-19 · harness · cite · HAR-5 · 运行耗时约 4.7 s（10 模块并行配置约 1 s，共 120 条编译命令，去重后按 8 路并行跑 `gcc -MM`）；自测用临时仓库 + 真实 cmake/gcc，约 2 s。

## 待办（writer branch drain 时执行）

- `docs/shape/sources/verification.md` 入口表补一行：`./scripts/check-test-paths.ps1` · 核对 `verification.psd1` 中每个主机模块的 `PathPatterns` 覆盖其测试工程实际编译的全部仓库头文件与源文件（需 cmake、ninja、主机 GCC）；同页 `check_fast.ps1` 一行“含文档机械检查”可顺带加上“主机测试路径覆盖”。盲区见脚本头注释。
- `docs/shape/harness.md` HAR-11 的 _Check:_ 可补充：新增或调整主机测试工程的源文件/include 路径后，以 `check-test-paths.ps1`（FAST）发现 `PathPatterns` 漂移；修复时同步 `harness_helpers.Tests.ps1` 路由断言。HAR-5 若列检查脚本，FAST 一栏加 `check-test-paths.ps1`，否则以 verification.md 为准。
- 已在三张 ticket 的 inbox 重复出现的“gui_task 缺 playback_contract.h”条目，drain 时可直接关闭为已修（commit 见本分支）。
