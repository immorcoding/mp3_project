# Platform 工作约定

本文件对 `Platform/` 全部子目录生效，并继承根 `AGENTS.md`。开始修改前，完整阅读 `Platform/README.md`、目标 Module README，以及它指向的架构文档或 ADR。

## 本地规则

- Platform 是当前 PCB 唯一的对象装配层，长期持有 Component Handle、Adapter Context 和回调节点。
- 本板 Handle、GPIO、极性、总线参数、供电轨映射和启动顺序只在这里收敛。
- `Bind()` 负责装配，Component `Init()` 负责设备访问；不得把短生命周期对象绑定给长期 Handle。
- 对上只发布 `Platform_<Capability>_<Verb>` 产品硬件能力，不泄漏 HAL Handle、Adapter Context、GPIO 或寄存器。
- 禁止包含 APP/Service 头，禁止拥有任务通知编号、FatFs 策略或产品流程。
- 中断路径只记录结果并发布轻量事件；等待、恢复和业务决策回到拥有请求的普通上下文。

## 完成条件

- 任何 Platform 代码变化都按硬件敏感处理：主机验证通过后仍是 `NEEDS_HARDWARE_VALIDATION`，除非已有本轮上板证据。
- 修改初始化顺序、资源生命周期、DMA/Cache 或回调所有权时必须调用只读 `embedded-reviewer`。
