#ifndef BOARD_H
#define BOARD_H

/**
  * @brief Board 层函数使用的统一状态码。
  * @note  具体设备的持续状态和详细错误由各 Board Module 单独提供。
  */
typedef enum
{
    BOARD_OK = 0,       /**< Board 操作成功。 */
    BOARD_PMIC_ERROR,  /**< Board PMIC 操作失败。 */
    BOARD_AUDIO_ERROR, /**< Board Audio 操作失败。 */
    BOARD_LCD_ERROR,   /**< Board LCD 操作失败。 */
    BOARD_SD_ERROR     /**< Board SD 操作失败。 */
} Board_StatusTypeDef;

/**
  * @brief  初始化整机启动所必需的板级设备。
  * @note   可移除 SD 卡不属于强制启动依赖，由 APP 单独初始化。
  */
Board_StatusTypeDef Board_Init(void);

#endif // BOARD_H
