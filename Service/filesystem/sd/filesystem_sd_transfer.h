/**
  ******************************************************************************
  * @file    filesystem_sd_transfer.h
  * @brief   Filesystem Service 私有 SDMMC 同步 DMA 执行器 Interface。
  *
  * @details
  *          该头仅供 Service/filesystem 内部的 FatFs BSP Bridge 使用；它不属于
  *          跨 Module 的公开 Interface，也不向调用者泄漏任务通知或 Cache 细节。
  ******************************************************************************
  */

#ifndef FILESYSTEM_SD_TRANSFER_H
#define FILESYSTEM_SD_TRANSFER_H

#include <stdbool.h>
#include <stdint.h>

bool filesystem_sd_transfer_init(uint32_t notify_index);
bool filesystem_sd_transfer_is_bound(void);
uint32_t filesystem_sd_transfer_notify_index(void);
bool filesystem_sd_transfer_read_blocks(uint8_t *destination,
                                        uint32_t start_block,
                                        uint32_t block_count,
                                        uint32_t timeout_ms);
bool filesystem_sd_transfer_write_blocks(const uint8_t *source,
                                         uint32_t start_block,
                                         uint32_t block_count,
                                         uint32_t timeout_ms);

#endif /* FILESYSTEM_SD_TRANSFER_H */
