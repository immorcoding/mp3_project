# FATFS Target

本目录是 CubeMX 生成的 FatFs DiskIO 与 BSP 模板，负责将逻辑卷操作下传到 Driver Link。

## 约束

- 除明确 USER CODE 接缝外不手改，重新生成后可被覆盖；
- 生成模板调用的 `BSP_SD_*` 由 `FATFS/App/fatfs.c` 的 USER CODE 桥接；
- 不在此实现 Storage task、挂载策略或 SD 卡热插拔逻辑。

