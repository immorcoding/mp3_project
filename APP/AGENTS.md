# APP 工作约定

本文件对 `APP/` 全部子目录生效，并继承根 `AGENTS.md`。开始修改前，完整阅读 `APP/README.md`、目标 Task README 和相关产品架构文档。

## 本地规则

- APP 只拥有启动顺序、顶层产品策略、Task 创建与 Task 内部编排；`APP/tasks/` 是 APP 的私有 Implementation。
- APP 可调用 Service/Platform 的公开 Interface，但不得构造 Component Ops、执行 Adapter Bind 或解释 HAL 状态。
- 禁止直接调用 HAL、FatFs `f_*`、DiskIO `BSP_*` 或访问寄存器和具体硬件 Handle。
- Task 通知槽由对应 Task 枚举持有；不同 Task 的相同数值不代表同一语义，不得共享无类型通知。
- ISR 只发布通知；消抖、等待、文件访问、日志格式化和状态推进都在普通 Task 上下文完成。
- 可复用产品能力应下沉到已有深 Module；不要增加逐函数转发层。

## 完成条件

- 修改任务启动、通知、资源所有权或跨 Module 流程时，核对完整运行时路径和失败清理。
- 更新目标 Task README/架构文档中的当前事实；先运行 FAST，非 trivial 变化运行 FULL，硬件路径保留上板状态。
