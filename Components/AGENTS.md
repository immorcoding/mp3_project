# Components 工作约定

本文件对 `Components/` 全部子目录生效，并继承根 `AGENTS.md`。开始修改前，完整阅读 `Components/README.md` 与目标 Module 的 `README.md`；只按其中指针读取相关架构文档和 ADR。

## 本地规则

- Component 是可复用核心；Interface、状态机、语义错误以及它所消费的 Ops Interface 由自身拥有。
- 外部能力只通过 `Ops + Context` 注入。Context 生命周期、必需回调和失败语义必须在 Bind/Init 处防御性检查。
- 禁止包含或调用 HAL、CubeMX、USB Device、FreeRTOS，以及任何 Adapter、Platform、Service、APP Interface 或 Implementation。
- 禁止保存 PCB 引脚、具体 Handle、供电顺序和产品策略；换 MCU 后核心 Implementation 应可复用。
- 新增公开 Interface 前先搜索全部调用者和测试；不要为单一实现预建假想 Seam。

## 完成条件

- 行为变化优先通过公开 Interface 增补 `Tests/` 下的 host fake 测试。
- 新增 Module 时同步本目录 README、Module README，并更新 `scripts/rules/verification.psd1` 的路径到测试映射。
- 先运行 FAST；完成非 trivial 变化时运行 FULL。涉及真实器件协议、时序或 DMA 协作时保留硬件验证状态。
