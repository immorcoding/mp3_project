# Platform 工作约定

继承根入口。修改前读 [本层说明](README.md)，按 [ROUTES](../docs/shape/ROUTES.md) 读目标模块 README、architecture（ARC-6、ARC-8、ARC-10）及相关 ADR；装配、资源和对上接口以这些正文为准。

- Platform 代码变化按硬件敏感处理：主机通过后仍为 `NEEDS_HARDWARE_VALIDATION`，除非已有本轮上板证据。
- 初始化顺序、资源生命周期、DMA/Cache 或回调所有权变化在 `code-review` 中核对嵌入式清单；其余验证沿用根入口。
