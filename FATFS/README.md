# FATFS

本目录是中间件集成层。CubeMX 维护 App 卷对象、Driver Link 和 Target DiskIO Glue；自维护的 `Target/bsp_driver_user_diskio.c/.h` 提供 USER 外部后端契约。

## 编译依赖与运行接缝

SD Glue 调用 BSP_SD_*，由 `Service/filesystem/sd/filesystem_sd_bsp.c` 强定义。USER Glue 只在 USER CODE 区调用 BSP_USER_DISKIO_*，由 `Service/filesystem/flash/filesystem_flash_bsp.c` 强定义。Target 的默认弱实现用于后端缺失时安全失败；弱属性仅位于定义处。

运行时进入 Service 强定义，不等于 FATFS 编译期包含 Service 实现。只有 Service 使用 FatFs f_* 产品流程；不在中间件添加任务等待、FTL、板级装配或 Cache 策略。APP/Platform 不直接调用 f_*。

## 生成与验证

保持 CubeMX 文件的 USER CODE 接缝；USER 契约不是自动生成文件，由根 CMake 显式收集。当前 ARM 链接已确认五个 USER 符号由 Service 强定义接管，真实 FatFs 主机测试通过。重新生成后仍需复核 Driver Link 和转发保留，硬件验收尚未完成。见 [ADR-0010](../docs/adr/0010-fatfs-user-diskio-service-ownership.md) 和 [FTL 设计](../docs/flash_ftl_design.md)。
