# FatFs Target 接缝

CubeMX 生成 `sd_diskio.c/.h`、`user_diskio.c/.h` 等 Glue。SD 保留既有 `bsp_driver_sd.h` 契约；新增的 `bsp_driver_user_diskio.c/.h` 是项目自维护文件，不由 CubeMX 生成。

USER 在 USER CODE 内薄转发 Init/GetStatus/ReadBlocks/WriteBlocks/Ioctl。契约只使用 FatFs 类型，默认弱定义返回 STA_NOINIT 或 RES_NOTRDY，绝不伪报读写成功。Service 在 `filesystem/sd/`、`filesystem/flash/` 分别提供 BSP_SD_* 与 BSP_USER_DISKIO_* 强定义；弱属性不放公共声明。

本目录不包含 Service 头，不实现 RTOS 等待、HAL 板级逻辑、FTL 或 Cache 维护。HAL 全局回调不是第二条文件系统通知路径。

USER 回调的 lun 是驱动内编号（当前为 0），不是全局卷号（当前 USERPath 为 1:/）。CTRL_SYNC 下沉同步执行器；逻辑扇区为 512 B，GET_BLOCK_SIZE 返回 1，因为 FatFs 要求该值为二次幂，FTL 不向其暴露七扇区物理组约束。未知命令/非法参数失败。

ARM 固件链接已检查强定义接管；主机测试验证真实 Glue 转发与默认失败行为。CubeMX 再生成后仍须复核 USER CODE、链接顺序和自维护源收集。详细错误与硬件验收见 [FTL 设计](../../docs/flash_ftl_design.md)。
