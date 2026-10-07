# Adapters 工作约定

继承根入口。修改前读 [本层说明](README.md)、当前类别及目标 Adapter README；按 [ROUTES](../docs/shape/ROUTES.md) 读 architecture（ARC-1、ARC-5、ARC-8 至 ARC-10）。类别、接口归属、Context 注入与 IRQ 约束以这些正文为准。

- 新增 Adapter 同步类别与目标 README，以及必要的验证路径映射。
- 修改 Bind、DMA、Cache、ISR、HAL 回调或硬件生命周期时，在 `code-review` 中核对嵌入式清单并保留上板验证状态，验证流程见根入口。
