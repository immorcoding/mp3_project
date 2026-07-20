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
    (void)LOG_Init();
    (void)LOG_Printf(LOG_LEVEL_INFO, "LOG", "initialization successful");

    if (Board_PMIC_Init() != PMIC_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR, "PMIC", "initialization failed");
        Error_Handler();
    }

    (void)LOG_Printf(LOG_LEVEL_INFO, "PMIC", "initialization successful");
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
