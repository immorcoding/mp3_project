/**
  ******************************************************************************
  * @file    playback_service_config.h
  * @brief   播放模块的私有固定资源配置：线性输入窗口、解码门槛与同步预算。
  ******************************************************************************
  */

#ifndef PLAYBACK_SERVICE_CONFIG_H
#define PLAYBACK_SERVICE_CONFIG_H

/* playback_service.c */
#define PLAYBACK_SERVICE_WINDOW_BYTES        (4096U)   /* 线性输入窗口容量，静态位于 DTCM；下限为 W_min。 */
#define PLAYBACK_SERVICE_WINDOW_MIN_BYTES    (2308U)   /* W_min：窗口字节数低于它且输入未结束时不解码；下限 2308 B（最大标准帧 1729 B 加下一帧头与对齐余量，#46）。 */
#define PLAYBACK_SERVICE_SYNC_BUDGET_BYTES   (65536U)  /* R：自音频区间开头或上一个好帧起，连续这么多字节解不出帧即回报同步预算耗尽。 */

/* playback_service.c、playback_service_decoder_minimp3.c */
#define PLAYBACK_SERVICE_FRAME_MAX_SAMPLES   (1152U)   /* 单帧每声道最多采样数；帧输出缓冲按 2 声道容纳一帧（1152 × 2 × 16 bit = 4608 B），位于 DTCM。 */

/* playback_service.c 输出管线 */
#define PLAYBACK_SERVICE_PCM_FRAMES_PER_HALF (1152U)   /* 每个 PCM 半区的立体声帧数，须为 1152 的整数倍；欠载时先改为 2304（E8）。 */
#define PLAYBACK_SERVICE_PCM_SECTION         ".sd_dma_buffer" /* PCM 半区所在链接段；链接脚本现有的 AXI SRAM NOLOAD 段，见下注。 */

/*
 * 链接脚本 stm32h743zgtx_flash.ld 受 CubeMX 守卫（ARC-2），本票未改。PCM 半区暂借 .sd_dma_buffer
 * （AXI SRAM、NOLOAD、32 字节对齐）。待用户授权后在链接脚本新增独立的 .playback_pcm (NOLOAD) >RAM
 * 段，再把 PLAYBACK_SERVICE_PCM_SECTION 改为 ".playback_pcm"。
 */

#endif /* PLAYBACK_SERVICE_CONFIG_H */
