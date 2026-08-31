/**
 ******************************************************************************
 * @file    storage_flash.h
 * @brief   Storage Task 内部的 Flash 诊断包装 Interface。
 *
 * @details
 *          请求交给 Filesystem Service 唯一 Flash 执行器，由 Service 订阅
 *          QSPI IRQ、等待索引通知并推进 Platform 收尾。APP 只保留诊断编排，
 *          不再持有第二条回调或等待路径，不包含芯片命令、物理分区或 FTL。
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

bool storage_flash_read_diagnostic(Platform_Flash_DiagnosticRegionTypeDef region,
                                   uint8_t *data,
                                   uint32_t data_length,
                                   uint32_t timeout_ms);

bool storage_flash_erase_diagnostic(Platform_Flash_DiagnosticRegionTypeDef region,
                                    uint32_t timeout_ms);

bool storage_flash_program_diagnostic_page(Platform_Flash_DiagnosticRegionTypeDef region,
                                           uint32_t page_offset,
                                           const uint8_t *data,
                                           uint32_t data_length,
                                           uint32_t timeout_ms);

#endif /* STORAGE_FLASH_H */
