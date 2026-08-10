# FATFS App

本目录由 CubeMX 生成逻辑卷对象、Driver Link 初始化和可插入的 USER CODE 接缝。

## 公开 Interface

- `MX_FATFS_Init()`：由 `main.c` 在启动阶段调用一次；
- `SDFatFS`、`SDPath`、`retSD` 等生成的卷连接对象；
- USER CODE 中的 `BSP_SD_*` 桥接函数，仅供生成的 DiskIO 模板调用。

## 调用的 Interface

- `Platform_SD_*`，用于把生成的块读写请求转交至本板 SD 能力；
- FatFs 通用 Driver Interface。

## 约束

- 自维护修改只放在 CubeMX 允许的 USER CODE 区；
- 这里不是文件系统策略 Module，挂载/卸载/格式化仍在 `Service/filesystem`；
- 块读写不得直接访问 `hsd1`。
