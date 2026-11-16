# Routes

按改动目录读取接口、领域规则和技术正文。分层目录按层通配：先读层 README，再读目标模块 README；目录更深处的 README 解释局部实现。

Routed: `Components/*/`, `Platform/*/`, `Service/*/`, `APP/tasks/*/`, `APP/apps/*/`, `APP/ui_cell/`, `Tools/*/`, `Adapters/*/`, `Resources/fonts/`, `Tests/`, `scripts/`, `patches/`

- `Components/*/`: 可复用设备协议与算法，每模块一目录。Entry: [层说明](../../Components/README.md) → 目标模块 README。Areas: [Architecture](architecture.md) · [Code style](code-style.md)；另按模块：[Hardware](hardware.md)（axp2101、ft6x36、soft_i2c）· [Storage](storage.md)（flash_ftl、id3_reader、sd、w25qxx）· [Diagnostics](diagnostics.md)（log）· [Resources](resources.md)（resource_pack）· [GUI](gui.md)（st7789）
- `Platform/*/`: 本板资源装配与产品硬件能力。Entry: [层说明](../../Platform/README.md) → 目标模块 README。Areas: [Architecture](architecture.md) · [Code style](code-style.md)；另按模块：[Hardware](hardware.md)（power、sdram、temp、touch）· [Storage](storage.md)（flash、sd）· [Diagnostics](diagnostics.md)（log）· [GUI](gui.md)（lcd）
- `Service/*/`: 跨模块产品流程。Entry: [层说明](../../Service/README.md) → 目标模块 README。Areas: [Architecture](architecture.md) · [Code style](code-style.md)；另按模块：[Storage](storage.md)（filesystem）· [GUI](gui.md)（gui）· [Diagnostics](diagnostics.md)（log）· [Resources](resources.md)（resource）
- `APP/tasks/*/`: 任务外壳、任务循环与跨任务交接；0.10.0 起没有 Storage Task，各任务直接调用 Filesystem。Entry: [任务总览](../../APP/tasks/README.md) → 目标 Task README。Areas: [Architecture](architecture.md)（ARC-17 静态栈与优先级、ARC-21 文件锁）；另按任务：[Storage](storage.md)（volume：STOR-8、STOR-9 卷生命周期；audio_reader：STOR-8 失败约定、STOR-17 按定位符打开；background：STOR-12 回收）· [Architecture](architecture.apps.md)（background：ARC-22 后台作业；foreground：ARC-23 至 ARC-27 前台 step）· [GUI](gui.md)（gui：GUI-28）· [Hardware](hardware.md)（volume、audio_reader、background、foreground：HWD-2 `.app_axi_ram` 与 NOLOAD 整块写入；monitor、playback）· [Diagnostics](diagnostics.md)（volume、audio_reader、background、foreground：DIAG-3 消息池、DIAG-6 证据出口、DIAG-7 测试钩子；log、playback）
- `APP/apps/*/`: 应用目录，注册表 `app_table.c` 加每应用一目录（音乐含 `catalog/` 曲库、`foreground/` 前台流程、`gui/` 界面、`music_background.c` 后台入口）。Entry: [应用接入约定](../../APP/apps/README.md) → 目标应用 README（现有 [music](../../APP/apps/music/README.md)，曲库见 [catalog](../../APP/apps/music/catalog/README.md)）。Areas: [Architecture](architecture.apps.md)（ARC-23 至 ARC-28）· [Storage](storage.md)（catalog：STOR-1、STOR-7、STOR-15 至 STOR-17）· [Diagnostics](diagnostics.md)（DIAG-3、DIAG-4、DIAG-6）；音乐另加 [GUI](gui.md)
- `APP/ui_cell/`: 界面数据短锁模块，GUI 与任务之间的交接，APP 私有。Entry: [ui_cell](../../APP/ui_cell/README.md)。Areas: [Architecture](architecture.apps.md)（ARC-24、ARC-25）
- `Adapters/*/`: SDK/HAL 适配，分 bridge、cortex、stm32_hal 三类。Entry: [层说明](../../Adapters/README.md) → 类别与目标 Adapter README。Areas: [Architecture](architecture.md) · [Code style](code-style.md) · [Harness](harness.md)；`stm32_hal/` 另加 [Hardware](hardware.md)，其中 sd、w25qxx_qspi 另加 [Storage](storage.md)
- `Tools/gui_simulator/`: 手写 GUI 的 PC 场景回归。Entry: [说明](../../Tools/gui_simulator/README.md)。Areas: [Architecture](architecture.md) · [GUI](gui.md)
- `Tools/package_maker/`: 资源包构建工具。Entry: [说明](../../Tools/package_maker/main.py)。Areas: [Architecture](architecture.md) · [Resources](resources.md)
- `Tools/external_loader/`: 独立 NOR 下载器。Entry: [说明](../../Tools/external_loader/README.md)。Areas: [Architecture](architecture.md) · [Storage](storage.md)
- `Resources/fonts/`: 字体生成输入与生成结果（生成脚本、字表规则、`OFL.txt`、入库的 bin）。Entry: [说明](../../Resources/fonts/README.md)。Areas: [Resources](resources.md)
- `Tools/board_test_card/`: 板级验收样本 MP3 生成。Entry: [说明](../../Tools/board_test_card/README.md)。Areas: [Workflow](workflow.md)
- `Tests/`: 独立 host 行为测试。Entry: [入口](../../Tests/AGENTS.md)。Areas: [Architecture](architecture.md) · [Harness](harness.md)
- `scripts/`: 构建、验证与 Hook。Entry: [入口](../../scripts/AGENTS.md)。Areas: [Architecture](architecture.md) · [Harness](harness.md)
- `patches/`: 上游 Vendor 补丁（patch 文件维护，重新导出后重打，见 HAR-15、ADR-0022）。Entry: [说明](../../patches/fatfs/README.md)。Areas: [Architecture](architecture.md) · [Harness](harness.md) · [Storage](storage.md)
