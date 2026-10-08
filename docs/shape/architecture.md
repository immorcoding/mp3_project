# Architecture

分层、Interface、装配与并发的当前规则；硬件和调用链事实按 [文档地图](README.md) 阅读。

Next id: ARC-19

## Pillars

- 功能所有权、编译期依赖、运行时路径是三张图，不能互相推导。
- 生成器维护产物，HAL 差异在 Adapter 内吸收。
- 公开 Interface 隐藏实现，装配与执行分开。

## Titles

- [ownership](architecture.ownership.md): 各层的抽象、板级资源和产品策略归属。
- [generation](architecture.generation.md): 生成代码与自维护代码的修改边界。
- [binding](architecture.binding.md): 实例装配与调用期间的对象寿命。
- [errors](architecture.errors.md): 调用结果、生命周期状态与诊断原因。
- [interrupts](architecture.interrupts.md): 中断发布事件与任务执行的交界。
- [storage-seams](architecture.storage-seams.md): 文件系统与持久化层的公开接缝。
- [evolution](architecture.evolution.md): 模块所有权判定、构建封装与长期决策。
- [module-growth](architecture.module-growth.md): 新增模块与目录分类的准入条件。
