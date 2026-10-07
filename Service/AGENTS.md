# Service 工作约定

继承根入口。修改前读 [本层说明](README.md)，按 [ROUTES](../docs/shape/ROUTES.md) 读目标模块 README、architecture（ARC-7、ARC-10 至 ARC-13）与 code-style；公开接缝变化读相关 ADR，GUI 变化另按根入口读取 gui 领域。

- 通过公开接口测试流程与错误路径；文件系统覆盖卷、路径、句柄代次和失败恢复。
- 新增模块确认其隐藏真实复杂度且多个调用步骤受益，同步 README 与验证映射。
- 经 Platform 改变真实设备行为时保留上板验证状态；其余验证沿用根入口。
