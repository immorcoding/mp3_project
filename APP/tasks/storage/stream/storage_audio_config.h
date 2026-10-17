/**
 * @file storage_audio_config.h
 * @brief Storage Task stream/ 分区的固定尺寸：音频块与块池。
 */

#ifndef STORAGE_AUDIO_CONFIG_H
#define STORAGE_AUDIO_CONFIG_H

/* storage_audio_block.h */
#define STORAGE_AUDIO_BLOCK_BYTES  (32U * 1024U)  /* 单块数据容量，单位为字节；512 B 整数倍且不超过 SD bounce buffer（32 KiB）。 */
#define STORAGE_AUDIO_BLOCK_COUNT  32U            /* 块池块数；初值 32 块共 1 MiB，320 kbps 下约 26 s。须不小于 3。 */

#define STORAGE_AUDIO_POOL_SECTION ".storage_catalog" /* 块池所在的链接段；链接脚本现有的 SDRAM NOLOAD 段，见下注。 */

/*
 * 链接脚本 stm32h743zgtx_flash.ld 受 CubeMX 守卫（ARC-2），本票未改。块池暂借 .storage_catalog
 * （SDRAM、NOLOAD、32 字节对齐）。待用户授权后在链接脚本新增独立的 .storage_audio_pool (NOLOAD) >SDRAM
 * 段，再把本宏改为 ".storage_audio_pool"。
 */

#endif /* STORAGE_AUDIO_CONFIG_H */
