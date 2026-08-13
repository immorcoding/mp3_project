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

/** @brief 单次阻塞块传输允许的默认时间，单位为毫秒。 */
#define SDCARD_TRANSFER_TIMEOUT_MS  1000u
/** @brief 等待卡完成内部编程并回到 TRANSFER 状态的默认时间，单位为毫秒。 */
#define SDCARD_SYNC_TIMEOUT_MS      1000u

#endif /* SDCARD_CONFIG_H */
