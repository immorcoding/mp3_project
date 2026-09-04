# Service 工作约定

本文件对 `Service/` 全部子目录生效，并继承根 `AGENTS.md`。开始修改前，完整阅读 `Service/README.md` 与目标 Module README；涉及公开 Seam 时再读取相关 ADR。

## 本地规则

- Service 组织多个 Platform/Component 能力形成产品流程；不要把纯转发包装成新的浅 Module。
- 公开 Interface 使用 `Service_<Capability>_<Verb>`，只暴露调用者需要的流程状态、生命周期和错误语义。
- 禁止包含 Adapter 私有头、HAL Handle 或直接操作 PCB GPIO；硬件访问通常经 Platform Interface。
- FatFs 对象、队列句柄、静态消息块和私有执行器不得越过 Module Interface。
- ISR 回调只唤醒或推进普通上下文；不得在 ISR 中执行 FatFs、日志格式化或产品流程。
- 第三方 Override Seam 是运行时入站路径，不构成 Service 依赖生成 Implementation 的许可。

## 完成条件

- 通过公开 Interface 测试流程行为和错误路径；涉及文件系统时覆盖卷、路径、句柄代次和失败恢复。
- 新增 Service Module 前确认它隐藏了真实复杂度并有多个调用步骤可获得 Leverage；同步 README 和验证映射。
- 完成非 trivial 变化时运行 FULL；经 Platform 改变真实设备行为时继续要求上板。
