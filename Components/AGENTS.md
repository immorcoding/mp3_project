# Components 工作约定

继承根入口。修改前读 [本层说明](README.md)，再按 [ROUTES](../docs/shape/ROUTES.md) 读目标模块 README、architecture（ARC-1、ARC-8、ARC-13）与 code-style；组件所有权、Ops 注入和依赖边界以这些正文为准。

- 新增公开接口前搜索全部调用者和测试，确认已有接缝可复用。
- 行为变化通过公开接口补充 `Tests/` 的 host fake 测试。
- 新增模块同步本层与模块 README，并更新 `scripts/rules/verification.psd1` 的生产路径到测试映射。
- 涉及真实器件协议、时序或 DMA 协作时，按根入口保留硬件验证状态。
