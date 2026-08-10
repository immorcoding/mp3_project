# FATFS

本目录由 CubeMX 的 FatFs 包生成。`App` 与 `Target` 的生成逻辑共同提供 FatFs Driver Link；产品代码只在明确的 USER CODE 接缝进行桥接。

## 接缝

- `App/fatfs.c`：CubeMX 卷对象与 `BSP_SD_*` 强定义接缝；
- `Target/sd_diskio.c`：生成的 DiskIO 模板，不直接写入产品策略；
- `Service/filesystem`：唯一的 FatFs 卷操作 Module。

## 约束

- 重新生成 CubeMX 代码后必须复核 USER CODE 是否保留；
- 不让 App/Platform 直接调用 FatFs `f_*`；
- 不修改 Middlewares 中 FatFs 源码来适配产品逻辑。

