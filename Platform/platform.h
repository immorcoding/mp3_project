/**
  ******************************************************************************
  * @file    platform.h
  * @brief   Platform 层公共状态和整机初始化入口。
  ******************************************************************************
  */

#ifndef PLATFORM_H
#define PLATFORM_H

#include "Adapters/cortex/cache/cortex_m7_dcache_adapter.h"

/**
 * @brief 当前 STM32H743 产品目标的 DMA 缓冲区最低对齐要求，单位为字节。
 * @note  该值由 Cortex-M7 D-Cache Adapter 的 Cache line 大小导出。所有需要由
 *        CPU 与 DMA 共享的数据缓冲区都应满足此要求，避免 Cache 维护影响相邻数据。
 */
#define PLATFORM_DMA_BUFFER_ALIGNMENT  CORTEX_M7_DCACHE_LINE_SIZE

/**
 * @brief Platform 层函数使用的统一状态码。
  * @note  具体设备的持续状态和详细错误由各 Platform Module 单独提供。
  */
typedef enum
{
    PLATFORM_OK = 0,       /**< Platform 操作成功。 */
    PLATFORM_LOG_ERROR,    /**< 日志装配或初始化失败。 */
    PLATFORM_PMIC_ERROR,   /**< PMIC 操作失败。 */
    PLATFORM_AUDIO_ERROR,  /**< Audio 操作失败。 */
    PLATFORM_LCD_ERROR,    /**< LCD 操作失败。 */
    PLATFORM_SD_ERROR,     /**< SD 操作失败。 */
    PLATFORM_TEMP_ERROR,   /**< MCU 内部温度采样失败。 */
    PLATFORM_SDRAM_ERROR,  /**< SDRAM 初始化失败。 */
    PLATFORM_TOUCH_ERROR,  /**< 触摸控制器初始化或访问失败。 */
    PLATFORM_BUSY          /**< 当前产品能力正执行异步操作。 */
} Platform_StatusTypeDef;

Platform_StatusTypeDef Platform_Init(void);

#endif // PLATFORM_H
