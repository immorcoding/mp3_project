# Service

本目录包含跨 Platform 和 Component 的产品流程 Module。当前有异步日志投递和 FatFs 卷操作；未来播放、媒体库或存储命令可在此增加独立目录。

## 公开 Interface

- `LogService_*`：普通任务上下文的异步日志投递与消费；
- `Filesystem_*`：Storage Task 独占期间的 FatFs 初始化、挂载、卸载和格式化。

## 调用的 Interface

- `Platform` 与 `Components` 的公开 Interface；
- FreeRTOS；
- 仅文件系统 Module 可直接使用 FatFs `f_*`。

## 禁止依赖

- 不包含 Adapter 私有头、HAL Handle 或直接操作 PCB GPIO；
- 不在 ISR 中执行流程逻辑；
- 不向其他任务泄漏 FatFs 对象、队列句柄或静态消息块地址。

## 命名

跨 Module 的公开 Interface 使用 Pascal 分段命名，例如 `LogService_Post()`；目录内 Implementation 使用 `snake_case`。
