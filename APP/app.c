/**
 * @file app.c
 * @brief Application implementation file
 */
#include "app.h"
#include "app_config.h"

#include "main.h"

#include "Board/board_pmic.h"
#include "Log/log.h"
/**
 * @brief  Initialize the application
 */
void app_init(void)
{
#if APP_LOG_ENABLE
    if (LOG_Init() != LOG_OK)
    {
        Error_Handler();
    }
#endif

    if (Board_PMIC_Init() != PMIC_OK)
    {
        Error_Handler();
    }

#if APP_LOG_ENABLE
    (void)LOG_Printf(LOG_LEVEL_INFO, "PMIC", "initialization successful");
#endif
}

/**
 * @brief  Main application loop
 */
void app_run(void)
{
    // 运行应用程序
    HAL_Delay(500);
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
}

/**
 * @brief  Handle application errors
 */
void app_error(void)
{

}
