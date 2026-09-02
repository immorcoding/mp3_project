/**
 * @file filesystem_flash_transfer.h
 * @brief Filesystem 内部唯一 Flash 执行器，不作为跨 Module 接口。
 */

#ifndef FILESYSTEM_FLASH_TRANSFER_H
#define FILESYSTEM_FLASH_TRANSFER_H

#include <stdbool.h>
#include <stdint.h>
#include "Platform/flash/platform_flash.h"
#include "Service/service.h"
bool filesystem_flash_transfer_init(uint32_t notify_index);
bool filesystem_flash_transfer_is_owner(void);
Service_StatusTypeDef filesystem_flash_transfer_finish(Platform_StatusTypeDef started,
                                                       uint32_t timeout_ms);
Service_StatusTypeDef filesystem_flash_transfer_open(void);
Service_StatusTypeDef filesystem_flash_transfer_format(void);
Service_StatusTypeDef filesystem_flash_transfer_read(uint32_t lba, uint8_t *data, uint32_t count);
Service_StatusTypeDef filesystem_flash_transfer_write(uint32_t lba,
                                                      const uint8_t *data,
                                                      uint32_t count);
Service_StatusTypeDef filesystem_flash_transfer_sync(void);
Service_StatusTypeDef filesystem_flash_transfer_reclaim(void);
Service_StatusTypeDef filesystem_flash_transfer_recover(void);
#endif
