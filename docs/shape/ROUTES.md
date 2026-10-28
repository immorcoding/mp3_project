# Routes

按改动目录读取接口、领域规则和技术正文。分层目录按层通配：先读层 README，再读目标模块 README；目录更深处的 README 解释局部实现。

Routed: `Components/*/`, `Platform/*/`, `Service/*/`, `APP/tasks/*/`, `Tools/*/`, `Adapters/*/`, `Resources/fonts/`, `Tests/`, `scripts/`

- `Components/*/`: 可复用设备协议与算法，每模块一目录。Entry: [层说明](../../Components/README.md) → 目标模块 README。Areas: [Architecture](architecture.md) · [Code style](code-style.md)；另按模块：[Hardware](hardware.md)（axp2101、ft6x36、soft_i2c）· [Storage](storage.md)（flash_ftl、id3_reader、sd、w25qxx）· [Diagnostics](diagnostics.md)（log）· [Resources](resources.md)（resource_pack）· [GUI](gui.md)（st7789）
- `Platform/*/`: 本板资源装配与产品硬件能力。Entry: [层说明](../../Platform/README.md) → 目标模块 README。Areas: [Architecture](architecture.md) · [Code style](code-style.md)；另按模块：[Hardware](hardware.md)（power、sdram、temp、touch）· [Storage](storage.md)（flash、sd）· [Diagnostics](diagnostics.md)（log）· [GUI](gui.md)（lcd）
- `Service/*/`: 跨模块产品流程。Entry: [层说明](../../Service/README.md) → 目标模块 README。Areas: [Architecture](architecture.md) · [Code style](code-style.md)；另按模块：[Storage](storage.md)（filesystem）· [GUI](gui.md)（gui）· [Diagnostics](diagnostics.md)（log）· [Resources](resources.md)（resource）
- `APP/tasks/*/`: 任务外壳、状态机与跨任务交接。Entry: [任务总览](../../APP/tasks/README.md) → 目标 Task README。Areas: [Architecture](architecture.md)；另按任务：[Storage](storage.md)（storage）· [GUI](gui.md)（gui）· [Hardware](hardware.md)（monitor、playback）· [Diagnostics](diagnostics.md)（log、playback）
- `Adapters/*/`: SDK/HAL 适配，分 bridge、cortex、stm32_hal 三类。Entry: [层说明](../../Adapters/README.md) → 类别与目标 Adapter README。Areas: [Architecture](architecture.md) · [Code style](code-style.md) · [Harness](harness.md)；`stm32_hal/` 另加 [Hardware](hardware.md)，其中 sd、w25qxx_qspi 另加 [Storage](storage.md)
- `Tools/gui_simulator/`: 手写 GUI 的 PC 场景回归。Entry: [说明](../../Tools/gui_simulator/README.md)。Areas: [Architecture](architecture.md) · [GUI](gui.md)
- `Tools/package_maker/`: 资源包构建工具。Entry: [说明](../../Tools/package_maker/main.py)。Areas: [Architecture](architecture.md) · [Resources](resources.md)
- `Tools/external_loader/`: 独立 NOR 下载器。Entry: [说明](../../Tools/external_loader/README.md)。Areas: [Architecture](architecture.md) · [Storage](storage.md)
- `Resources/fonts/`: 字体生成输入与生成结果（生成脚本、字表规则、`OFL.txt`、入库的 bin）。Entry: [说明](../../Resources/fonts/README.md)。Areas: [Resources](resources.md)
- `Tools/board_test_card/`: 板级验收样本 MP3 生成。Entry: [说明](../../Tools/board_test_card/README.md)。Areas: [Workflow](workflow.md)
- `Tests/`: 独立 host 行为测试。Entry: [入口](../../Tests/AGENTS.md)。Areas: [Architecture](architecture.md) · [Harness](harness.md)
- `scripts/`: 构建、验证与 Hook。Entry: [入口](../../scripts/AGENTS.md)。Areas: [Architecture](architecture.md) · [Harness](harness.md)
