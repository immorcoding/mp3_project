# FATFS Target

本目录是 CubeMX 生成的 FatFs DiskIO 与 BSP 模板，负责把逻辑卷操作下传到 Driver Link；除 CubeMX USER CODE 区外，不在这里放产品策略。

## 公开 Interface

- CubeMX 生成的 `diskio`、`SD_Driver` 与 `USER_Driver` Interface；
- `bsp_driver_sd.h` 声明的 `BSP_SD_*` 外部契约。

## 调用的 Interface

- `FATFS/App/fatfs.c` USER CODE 中的唯一 `BSP_SD_*` 实现；该实现同步等待 Platform SD 的 DMA 传输完成；
- CubeMX 生成的 Driver Link 机制。

## 约束

- 不在 `sd_diskio.c` 直接访问 `hsd1` 或绕过 Platform；
- 不重复定义 HAL SD 完成回调或 `BSP_SD_*`；全局 HAL 回调分发由 `Adapters/irq/stm32_sdmmc_irq.*` 管理；
- CubeMX 重新生成后，检查生成文件仍只调用 `BSP_SD_*`，并保留 `FATFS/App/fatfs.c` USER CODE 区。
