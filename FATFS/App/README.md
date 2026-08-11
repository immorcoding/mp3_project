# CubeMX FatFs App 生成目录

本生成目录只持有 FatFs 逻辑卷对象与 `MX_FATFS_Init()`；后者把 CubeMX DiskIO Driver Link 到 `SDPath` 和 `USERPath`。

## 公开 Interface

- `MX_FATFS_Init()` 与 `fatfs.h` 声明的生成逻辑卷对象。

## 调用的 Interface

- CubeMX 生成的 FatFs Driver Link Interface。

## 约束

- 不在此处放入 Storage Task 所有权、FreeRTOS 通知处理、SDMMC DMA 或 D-Cache 维护。
- Cube/FatFs 块访问通过 `FATFS/Target/bsp_driver_sd.h` 声明的外部 Override Seam 到达项目的 `BSP_SD_*` 强定义；其 Implementation 位于 `Service/filesystem/filesystem_fatfs_bsp.c`。
- 自定义内容必须保留在 CubeMX USER CODE 区中，保证重新生成后仍存在。
