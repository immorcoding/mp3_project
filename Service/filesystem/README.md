# Filesystem

Filesystem Module 封装 CubeMX FatFs 接缝所需的路径转换、挂载、卸载和 FAT32 格式化。它不管理 SD 卡检测、消抖或任务调度，这些属于 Storage Task。

## 公开 Interface

- `Filesystem_Init()`：确认 CubeMX DiskIO Driver 已连接，并将 FatFs SD 访问权绑定给当前 Storage Task；
- `Filesystem_MountSD()`、`Filesystem_UnmountSD()`；
- `Filesystem_FormatSD()`：使用静态工作区格式化当前 SD 卷。

## 调用的 Interface

- `FATFS/App/fatfs.h` 的 CubeMX 逻辑卷对象、Driver Link 和 `FatFs_SD_BindCurrentTask()`；
- FatFs `f_mount()`、`f_mkfs()` 等 Interface；
- Platform SD 的就绪状态查询。

## 约束

- 仅 Storage Task 可调用；调用者必须已经取得 SD 与 FatFs 的独占权；
- `FATFS/App/fatfs.c` 的 USER CODE 区是 CubeMX 支持的 `BSP_SD_*` 覆盖接缝：它将同步 DiskIO 调用桥接成“启动 SDMMC DMA → 等待 Storage Task 索引 1 通知 → 返回同步结果”；
- DMA 中转缓冲区属于该桥接层的 AXI SRAM 静态存储，不属于任务栈；Cache Clean/Invalidate 必须与每次 DMA 方向配对；
- 不从 ISR 或普通业务任务调用 FatFs；格式化必须是显式受控请求，不能放在启动或热插拔路径。

公开 Interface 使用 `Filesystem_*`；路径、工作区和卷辅助 Implementation 使用 `filesystem_*`。
