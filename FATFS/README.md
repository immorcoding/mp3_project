# FATFS

CubeMX 生成本目录下的 `App` 与 `Target` 子目录；它们共同提供 FatFs 卷对象、Driver Link、DiskIO Glue 和外部 `BSP_SD_*` Override Seam。

## Interface

- `App/fatfs.c` 只持有 CubeMX 逻辑卷对象和 Driver Link。
- `Target/bsp_driver_sd.h` 声明由生成的 `Target/sd_diskio.c` 消费的 BSP Override Seam。
- `Service/filesystem/filesystem_fatfs_bsp.c` 是该 Seam 的强定义 Adapter，也是项目内唯一的 `BSP_SD_*` Implementation。
- 只有 `Service/filesystem` 调用 FatFs `f_*` Interface。

## 约束

- CubeMX 管理的文件保持生成状态；仅在 USER CODE 区加入需要随重新生成保留的内容。
- 不在生成目录放入 APP 任务所有权、Platform 装配、DMA 等待或 D-Cache 维护。
- APP 和 Platform 不直接调用 FatFs `f_*` 函数。
