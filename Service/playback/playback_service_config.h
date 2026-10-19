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
#define PLAYBACK_SERVICE_PCM_SECTION         ".playback_pcm" /* PCM 半区所在链接段；链接脚本独立的 AXI SRAM NOLOAD 段。 */

#endif /* PLAYBACK_SERVICE_CONFIG_H */
