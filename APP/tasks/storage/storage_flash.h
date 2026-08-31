/**
  ******************************************************************************
  * @file    storage_flash.h
  * @brief   Storage Task 内部的 Platform Flash 异步操作协调 Interface。
  *
  * @details
  *          本 Module 长期持有 Platform Flash 的唯一 QSPI IRQ 订阅，并把 IRQ
  *          事件转换为 Storage Task 的索引任务通知。它只组织 FreeRTOS 等待和
  *          Platform Flash 普通上下文收尾；不包含 W25Qxx 命令、QSPI HAL、
  *          Flash 物理分区或未来 FTL 逻辑。
  ******************************************************************************
  */

#ifndef STORAGE_FLASH_H
#define STORAGE_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Platform/flash/platform_flash.h"

void storage_flash_init(TaskHandle_t task_handle);

bool storage_flash_read_array(uint32_t address,
                              uint8_t *data,
                              uint32_t data_length,
                              uint32_t timeout_ms);

bool storage_flash_read_diagnostic(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint8_t *data,
    uint32_t data_length,
    uint32_t timeout_ms);

bool storage_flash_erase_diagnostic(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint32_t timeout_ms);

bool storage_flash_program_diagnostic_page(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint32_t page_offset,
    const uint8_t *data,
    uint32_t data_length,
    uint32_t timeout_ms);

#endif /* STORAGE_FLASH_H */
