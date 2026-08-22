# Service

本目录包含跨 Platform 和 Component 的产品流程 Module。当前有异步日志投递和 FatFs 卷操作；未来播放、媒体库或存储命令可在此增加独立目录。

## 公开 Interface

- `service.h`：Service 对上层公开的通用操作结果；
- `Service_Log_*`：普通任务上下文的异步日志投递与消费；
- `Service_Filesystem_*`：Storage Task 独占期间的 FatFs 初始化、挂载、卸载和格式化。

## 编译期依赖

- `Platform` 与 `Components` 的公开 Interface；
- FreeRTOS；
- 仅文件系统 Module 可直接使用 FatFs `f_*`。

## 运行时请求路径

Service 在任务或产品流程上下文编排能力；进入硬件通常经 `Platform_*`，再由 Platform 调用 Component。`Service/filesystem` 经 FatFs 声明的 `BSP_SD_*` Override Seam 接收 DiskIO 的运行时入站调用；这不是 Service 反向包含生成 DiskIO Implementation。

## 事件/ISR 路径

Service 可以向 Platform 注册其声明的强类型回调。回调只唤醒或推进 Service 自己的任务逻辑；ISR 中不执行 FatFs、日志格式化或业务流程。

## 禁止依赖

- 不包含 Adapter 私有头、HAL Handle 或直接操作 PCB GPIO；
- 不在 ISR 中执行流程逻辑；
- 不向其他任务泄漏 FatFs 对象、队列句柄或静态消息块地址。

## 状态约定

Service 的公开函数统一返回 `Service_StatusTypeDef`。`SERVICE_OK` 表示成功；其余
枚举只保留调用者需要的流程语义，例如未就绪、忙、超时或未格式化的文件系统。
Platform、Component 与 Middlewares 的原始状态值不得出现在 Service 的公开头文件中。

## 命名

跨 Module 的公开 Interface 固定使用 `Service_<Capability>_<Verb>`，例如 `Service_Log_Post()`、`Service_Filesystem_MountSD()`；目录内 Implementation 使用 `snake_case`。
