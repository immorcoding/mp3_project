# ADR-0010：USER DiskIO 契约与 Filesystem Service 执行所有权

- 状态：已接受、已实施；主机回归与固件构建通过，硬件验收待完成
- 日期：2026-08-31
- 技术方案：[Flash FTL 首版设计](../shape/storage.md#durability)
- 相关决定：[ADR-0002](0002-layering-and-interface-ownership.md)、[ADR-0005](0005-sd-filesystem-task-ownership.md)

## 背景

CubeMX 为 H743 生成 USER DiskIO 骨架，但不自动提供 SD BSP 那样的 Flash 后端。FatFs 要求同步块访问，现有原始 Flash 完成等待却由 APP 持有；在生成文件中直接调用 Service 或安排等待会耦合中间件与产品实现。

## 决定

在 FATFS Target 自维护 `bsp_driver_user_diskio` 契约和安全弱默认定义；生成 USER DiskIO 只在 USER CODE 区调用契约。Filesystem Service 提供强定义与私有 Flash 同步执行器，中间件不依赖 Service 头；链接时验证强定义接管。

Filesystem 保持一个 Module，内部划分 SD/Flash 子目录，SD 专属 BSP 以 SD 命名。Flash 唯一完成回调与等待职责从 APP 迁入 Service，Storage Task 仍是唯一执行上下文，APP 决定启动、挂载及维护时机。FTL 算法仍在独立 Component，不能因 SD 内置映射控制器而省略。

## 不采用的方案与后果

- 不在生成 DiskIO 放入 RTOS 等待或直接包含 Service 头，避免重生成风险与反向实现依赖。
- 不在 USER 生成函数上另加一套强弱覆盖，避免绕开驱动表和 USER CODE 接缝。
- 不提前创建通用 BlockDevice 或 Service/storage；当前产品不提供 USB MSC 仲裁。
- 弱后端明确报告未就绪，不得无操作返回成功；Service 同步返回不改变下层异步推进模型。
- APP 原始诊断也通过同一个执行所有者，不保留第二条回调注册路径。
