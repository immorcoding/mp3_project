# 文件系统 Service

本 Module 持有当前 FatFs 卷的生命周期，并把 FatFs 的同步块访问契约转换为 SDMMC DMA 执行。它只在 Storage Task 上下文中使用；卡检测、消抖和挂载策略不属于本 Module。

## 公开 Interface

- `Filesystem_Init()`：校验 CubeMX Driver Link，绑定当前 Storage Task 为唯一 DMA 执行者，并订阅 Platform SD 传输事件。
- `Filesystem_MountSD()`、`Filesystem_UnmountSD()`：挂载或注销 FatFs 卷。
- `Filesystem_FormatSD()`：使用 Service 私有静态工作区执行显式 FAT32 格式化。

## 内部 Interface

- `filesystem_sd_transfer.h` 仅限本目录使用；它只向 FatFs BSP Adapter 提供同步逻辑块读写。
- `filesystem_fatfs_bsp.c` 实现 CubeMX 外部 `BSP_SD_*` Override Seam。它没有项目公开头文件；`FATFS/Target/sd_diskio.c` 只通过既有 Cube 声明和链接符号到达它。

## 编译期依赖

- `FATFS/App/fatfs.h` 的 CubeMX Driver Link 对象。
- FatFs 的 `f_mount()`、`f_mkfs()` Interface。
- Platform SD 的生命周期、DMA 启动、传输完成和事件订阅 Interface。
- 原生 FreeRTOS 索引任务通知 Interface，仅由私有 DMA 执行器使用。

## 运行时请求与事件路径

Filesystem 在 Storage Task 独占期向下调用 `Platform_SD_*` 与 FatFs `f_*`。FatFs 的 `disk_*` 则通过 `BSP_SD_*` Override Seam 运行时进入本目录的私有 bridge；这不表示本 Module 反向包含生成的 `FATFS/Target` Implementation。SDMMC IRQ 事件经 Platform SD 的已注册传输回调到达私有执行器，再唤醒同一 Storage Task 完成同步等待。

## 约束

- 第一次成功的 `Filesystem_Init()` 把执行器绑定到当前 Storage Task；其他任务不能消费其传输通知或使用其 bounce buffer。
- 同时最多一笔 SD DMA 在飞。索引 `1` 表示该传输的一个带类型完成/错误/中止事件；索引 `0` 仍保留给卡检测消抖。
- 从 FatFs 的视角，`BSP_SD_ReadBlocks()` 与 `BSP_SD_WriteBlocks()` 仍是同步的：只有 DMA 完成、Cache 维护完成并提交 Platform SD 状态后才返回。
- 私有的 32 字节对齐 AXI SRAM bounce buffer 向 FatFs 调用者隐藏 DMA 可达性、对齐和 D-Cache 规则。不使用 `volatile`；数据可见性由 Cache 维护保证。
- ISR 仅发布事件。FatFs 调用、Cache 维护、缓冲区复制和 `Platform_SD_CompleteTransfer()` 都在任务上下文执行。

## 命名

跨层 Interface 使用 `Filesystem_*`；Implementation 与目录内部 Interface 使用 `filesystem_*`。
