/**
  ******************************************************************************
  * @file    platform.h
  * @brief   Platform 层公共状态和整机初始化入口。
  ******************************************************************************
  */

#ifndef PLATFORM_H
#define PLATFORM_H

#define PLATFORM_DMA_BUFFER_ALIGNMENT  32U /* Cortex-M7 D-Cache line；CPU 与 DMA 共享缓冲应对齐。 */

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
    PLATFORM_LED_ERROR,    /**< LED 初始化或控制失败。 */
    PLATFORM_SD_ERROR,     /**< SD 操作失败。 */
    PLATFORM_FLASH_ERROR,  /**< W25Q256 外部 NOR Flash 操作失败。 */
    PLATFORM_TEMP_ERROR,   /**< MCU 内部温度采样失败。 */
    PLATFORM_SDRAM_ERROR,  /**< SDRAM 初始化失败。 */
    PLATFORM_TOUCH_ERROR,  /**< 触摸控制器初始化或访问失败。 */
    PLATFORM_BUSY          /**< 当前产品能力正执行异步操作。 */
} Platform_StatusTypeDef;

Platform_StatusTypeDef Platform_Init(void);

#endif /* PLATFORM_H */
