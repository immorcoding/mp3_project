/**
 * @file storage_audio_config.h
 * @brief Storage Task stream/ 分区的固定尺寸：音频块与块池。
 */

#ifndef STORAGE_AUDIO_CONFIG_H
#define STORAGE_AUDIO_CONFIG_H

/* storage_audio_block.h */
#define STORAGE_AUDIO_BLOCK_BYTES  (32U * 1024U)  /* 单块数据容量，单位为字节；512 B 整数倍且不超过 SD bounce buffer（32 KiB）。 */
#define STORAGE_AUDIO_BLOCK_COUNT  32U            /* 块池块数；初值 32 块共 1 MiB，320 kbps 下约 26 s。须不小于 3。 */

#define STORAGE_AUDIO_POOL_SECTION ".storage_audio_pool" /* 块池所在的链接段；链接脚本独立的 SDRAM NOLOAD 段，位于帧缓冲之后。 */

#endif /* STORAGE_AUDIO_CONFIG_H */
