# Adapters 工作约定

本文件对 `Adapters/` 全部子目录生效，并继承根 `AGENTS.md`。开始修改前，完整阅读 `Adapters/README.md`、当前类别 README 和目标 Adapter README。

## 先确定 Adapter 类别

- `bridge/`：只转换两个 Component Interface，不包含 HAL、CMSIS、RTOS 或板级策略。
- `cortex/`：只封装 Cortex-M 架构能力，不拥有外设实例或任务状态。
- `stm32_hal/`：集中 HAL、CubeMX Handle、USB Device 和 HAL 全局回调相关 Implementation。

## 本地规则

- Adapter 实现既有 Interface；不得自行发明由 Adapter 所有的通用 Ops。
- Context 只描述后端资源，具体实例、引脚、极性和长期对象由 Platform 注入并持有。
- 将原生状态归一化为拥有方定义的语义；不得向 Component 泄漏 HAL 类型或错误码。
- IRQ/回调路径只发布强类型轻量事件；ISR 中不得执行日志格式化、文件系统、延时或产品流程。
- 禁止依赖 APP、Service，禁止把不同硬件源压成 `ID + void *` 的无类型分发 Interface。

## 完成条件

- 修改 Bind、DMA、Cache、ISR、HAL 回调或硬件生命周期时，除 FAST/FULL 外必须调用只读 `embedded-reviewer`，并保留上板验证状态。
- 新增 Adapter 时同步类别 README、目标 README，以及必要的验证路径映射。
