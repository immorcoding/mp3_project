/**
  ******************************************************************************
  * @file    platform_lcd.c
  * @brief   本板 ST7789、SPI1、GPIO 与 LCD 电源的 Platform 装配实现。
  *
  * @details
  *          Platform 持有当前 PCB 唯一的 ST7789 Device 与 STM32 HAL Adapter
  *          Context，并把 SPI1、CS、D/C、RESET 和 ALDO2 LCD 电源轨装配为
  *          面向上层的最小诊断能力。
  ******************************************************************************
  */

#include "Platform/lcd/platform_lcd.h"

#include <stddef.h>

#include "Adapters/stm32_hal/st7789_spi/st7789_spi_stm32_hal_adapter.h"
#include "Components/log/log.h"
#include "Components/st7789/st7789.h"
#include "Platform/power/platform_power.h"
#include "main.h"
#include "spi.h"

/** @brief LCD 电源打开后等待 LDO 和屏模块电源稳定的时间，单位为毫秒。 */
#define PLATFORM_LCD_POWER_SETTLE_DELAY_MS     10u

/** @brief 单个 SPI 阻塞命令或 ID 读取事务的最大等待时间，单位为毫秒。 */
#define PLATFORM_LCD_SPI_TIMEOUT_MS            1000u

/** @brief 本板唯一的 ST7789 Device 实例。 */
static ST7789_HandleTypeDef hplatform_lcd;

/**
  * @brief 本板 ST7789 使用的 SPI1 和 GPIO Adapter Context。
  * @note  该对象只借用 CubeMX 生成的 hspi1 与 GPIO 定义；CS、命令 D/C 和
  *        RESET 均为低有效，由本 PCB 的引脚电平决定。
  */
static ST7789_SPI_STM32HALAdapterTypeDef hplatform_lcd_adapter = {
    .SPIHandle = &hspi1,
    .ChipSelectPort = SPI1_CS_GPIO_Port,
    .ChipSelectPin = SPI1_CS_Pin,
    .ChipSelectActiveState = GPIO_PIN_RESET,
    .DataCommandPort = SPI1_DC_GPIO_Port,
    .DataCommandPin = SPI1_DC_Pin,
    .CommandState = GPIO_PIN_RESET,
    .ResetPort = SPI1_RST_GPIO_Port,
    .ResetPin = SPI1_RST_Pin,
    .ResetAssertState = GPIO_PIN_RESET,
    .TimeoutMs = PLATFORM_LCD_SPI_TIMEOUT_MS
};

/**
  * @brief  开启 LCD 电源、装配 SPI/GPIO Adapter 并执行 ST7789 硬件复位。
  * @retval PLATFORM_OK LCD 已进入可读写命令的 READY 状态。
  * @retval PLATFORM_LCD_ERROR LCD 电源、Adapter 绑定或硬件复位失败。
  * @note   本函数不写显示初始化寄存器表，也不打开背光；因此即使后续 ID
  *         读取失败，仍不会误以为已经完成了可见显示初始化。
  */
Platform_StatusTypeDef Platform_LCD_Init(void)
{
    if (Platform_Power_SetLCD(true) != PLATFORM_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR, "LCD", "power enable failed.");
        return PLATFORM_LCD_ERROR;
    }

    HAL_Delay(PLATFORM_LCD_POWER_SETTLE_DELAY_MS);

    if (ST7789_SPI_STM32HALAdapter_Bind(&hplatform_lcd,
                                         &hplatform_lcd_adapter) != ST7789_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR, "LCD", "SPI adapter bind failed.");
        return PLATFORM_LCD_ERROR;
    }

    if (ST7789_Init(&hplatform_lcd) != ST7789_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "LCD",
                         "reset failed: error=%u, port=%u.",
                         (unsigned int)hplatform_lcd.ErrorCode,
                         (unsigned int)hplatform_lcd.LastPortStatus);
        return PLATFORM_LCD_ERROR;
    }

    return PLATFORM_OK;
}

/**
  * @brief  读取本板 ST7789 的 RDDID 并复制为 Platform 公开快照。
  * @param  id 接收 3 字节显示标识的调用者结构。
  * @retval PLATFORM_OK 已成功读取并复制 ID。
  * @retval PLATFORM_LCD_ERROR 参数为空或 ST7789 尚未就绪、读 SPI 失败。
  */
Platform_StatusTypeDef Platform_LCD_ReadID(Platform_LCD_IDTypeDef *id)
{
    ST7789_IDTypeDef device_id;

    if (id == NULL)
    {
        return PLATFORM_LCD_ERROR;
    }

    if (ST7789_ReadID(&hplatform_lcd, &device_id) != ST7789_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "LCD",
                         "RDDID failed: error=%u, port=%u.",
                         (unsigned int)hplatform_lcd.ErrorCode,
                         (unsigned int)hplatform_lcd.LastPortStatus);
        return PLATFORM_LCD_ERROR;
    }

    id->ID1 = device_id.ID1;
    id->ID2 = device_id.ID2;
    id->ID3 = device_id.ID3;
    return PLATFORM_OK;
}
