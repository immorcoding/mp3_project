# CubeMX FatFs Target 生成目录

本生成目录提供 FatFs DiskIO Glue，并声明 Cube 的 `BSP_SD_*` 外部 Override Seam。

## 公开 Interface

- `bsp_driver_sd.h`：声明 `BSP_SD_*`、`BSP_SD_CardInfo` 和 Cube 状态值。
- `sd_diskio.c`：从 FatFs `disk_*` 操作中同步调用该 Interface。

## 调用的 Interface

- 由 `Service/filesystem/filesystem_fatfs_bsp.c` 实现的 `BSP_SD_*` 强定义 Adapter。

## 约束

- 不修改生成的 `sd_diskio.c` 来嵌入项目任务、Cache 或板级细节。
- `bsp_driver_sd.c` 提供弱默认实现；Service Adapter 有意以强定义覆盖它们。
- HAL 全局完成回调不是第二条 FatFs 通知路径；SDMMC 完成事件只由已注册的 IRQ Adapter 管理。
