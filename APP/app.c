/**
 * @file app.c
 * @brief Application implementation file
 */
#include "app.h"
#include "app_config.h"

#include "main.h"

#include "BSP/Board/board_pmic.h"
#include "System/Log/log.h"
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
    static uint32_t last_led_toggle_ms = 0U;
    uint32_t now_ms = HAL_GetTick();

    /* 高频、非阻塞地尝试发送一条已排队日志。 */
    (void)LOG_Process();

    /* 使用系统节拍控制 LED，避免阻塞日志队列排空。 */
    if ((uint32_t)(now_ms - last_led_toggle_ms) >= 500U)
    {
        last_led_toggle_ms = now_ms;
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }

}

/**
 * @brief  Handle application errors
 */
void app_error(void)
{

}
