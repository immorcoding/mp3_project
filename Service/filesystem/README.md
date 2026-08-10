# Filesystem

Filesystem Module 封装 CubeMX FatFs 接缝所需的路径转换、挂载、卸载和 FAT32 格式化。它不管理 SD 卡检测、消抖或任务调度，这些属于 Storage task。

## 公开 Interface

- `Filesystem_Init()`：确认 CubeMX DiskIO Driver 已连接；
- `Filesystem_MountSD()`、`Filesystem_UnmountSD()`；
- `Filesystem_FormatSD()`：使用静态工作区格式化当前 SD 卷。

## 调用的 Interface

- `FATFS/App/fatfs.h` 提供的 CubeMX 逻辑卷对象和 Driver Link；
- FatFs `f_mount()`、`f_mkfs()` 等 Interface。

## 资源与约束

- 仅 Storage task 可调用；调用者必须已经取得 SD 和 FatFs 的独占权；
- 不初始化 `Platform_SD`，不持有 `hsd1`；
- 格式化会销毁数据，必须由上层受控命令触发；
- 工作区为静态对象，避免占用 Storage task 栈。

## 命名

公开 Interface 使用 `Filesystem_*`；路径和工作区辅助 Implementation 使用 `filesystem_*`。
