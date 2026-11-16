/**
  ******************************************************************************
  * @file    sd_config.h
  * @brief   SD Card Device 的私有默认时序配置。
  *
  * @details
  *          本文件不属于 SD Card 的跨 Module Interface，只供本目录内的
  *          Implementation 选择同步 Port 调用的默认等待时间。
  ******************************************************************************
  */

#ifndef SDCARD_CONFIG_H
#define SDCARD_CONFIG_H

/* sd.c */
#define SDCARD_TRANSFER_TIMEOUT_MS  1000u  /* 单次阻塞块传输允许的默认时间，单位为毫秒。 */
#define SDCARD_SYNC_TIMEOUT_MS      1000u  /* 等待卡完成内部编程并回到 TRANSFER 状态的默认时间，单位为毫秒。 */
#define SDCARD_ABORT_TIMEOUT_MS      100u  /* 显式中止在飞 DMA 传输的上界，单位为毫秒；到点还没停就由 Port 反初始化，Device 保持 ERROR。 */

#endif /* SDCARD_CONFIG_H */
