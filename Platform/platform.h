#ifndef PLATFORM_H
#define PLATFORM_H

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
    PLATFORM_IRQ_ERROR     /**< IRQ Dispatcher 操作失败。 */
} Platform_StatusTypeDef;

Platform_StatusTypeDef Platform_Init(void);

#endif // PLATFORM_H
