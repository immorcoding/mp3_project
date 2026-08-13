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
#include "Platform/lcd/platform_lcd_config.h"

#include <stddef.h>

#include "Adapters/stm32_hal/st7789_spi/st7789_spi_stm32_hal_adapter.h"
#include "Components/log/log.h"
#include "Components/st7789/st7789.h"
#include "Platform/power/platform_power.h"
#include "main.h"
#include "spi.h"

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
 * @brief  开启 LCD 电源、装配 SPI/GPIO Adapter 并进入可见 RGB565 显示状态。
 * @retval PLATFORM_OK LCD 已完成复位、显示初始化并打开背光。
 * @retval PLATFORM_LCD_ERROR LCD 电源、Adapter 绑定、控制器初始化或显示打开失败。
 * @note   背光只在 `SLPOUT`、RGB565、正常显示和 `DISPON` 全部成功后打开，
 *         因此初始化失败时屏幕不会以未配置状态发光。
  */
Platform_StatusTypeDef Platform_LCD_Init(void)
{
    /* 先熄灭背光，保证复位或配置失败时不会显示未定义画面。 */
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port,
                      LCD_BL_Pin,
                      PLATFORM_LCD_BACKLIGHT_DISABLED_STATE);

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

    if (ST7789_DisplayInit(&hplatform_lcd) != ST7789_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "LCD",
                         "display init failed: error=%u, port=%u.",
                         (unsigned int)hplatform_lcd.ErrorCode,
                         (unsigned int)hplatform_lcd.LastPortStatus);
        return PLATFORM_LCD_ERROR;
    }

    HAL_GPIO_WritePin(LCD_BL_GPIO_Port,
                      LCD_BL_Pin,
                      PLATFORM_LCD_BACKLIGHT_ENABLED_STATE);

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

/**
 * @brief  向当前板 LCD 的单个坐标点写入 RGB565 像素。
 * @param  x 像素 X 坐标，左上角为 `(0, 0)`。
 * @param  y 像素 Y 坐标。
 * @param  color RGB565 颜色。
 * @retval PLATFORM_OK 像素已写入 ST7789 Display RAM。
 * @retval PLATFORM_LCD_ERROR LCD 未就绪、坐标非法或 SPI 写入失败。
 * @note   本函数不接触 SPI Handle 或 GPIO；控制器协议由 ST7789 Device 完成。
 */
Platform_StatusTypeDef Platform_LCD_DrawPixel(uint16_t x,
                                              uint16_t y,
                                              uint16_t color)
{
    if (ST7789_DrawPixel(&hplatform_lcd, x, y, color) != ST7789_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "LCD",
                         "draw pixel failed: error=%u, port=%u.",
                         (unsigned int)hplatform_lcd.ErrorCode,
                         (unsigned int)hplatform_lcd.LastPortStatus);
        return PLATFORM_LCD_ERROR;
    }

    return PLATFORM_OK;
}

/**
 * @brief  向当前板 LCD 的包含边界矩形连续写入同一 RGB565 颜色。
 * @param  x_start 矩形左边界。
 * @param  y_start 矩形上边界。
 * @param  x_end 矩形右边界，包含该像素。
 * @param  y_end 矩形下边界，包含该像素。
 * @param  color RGB565 颜色。
 * @retval PLATFORM_OK 整个矩形已写入 Display RAM。
 * @retval PLATFORM_LCD_ERROR LCD 未就绪、坐标非法或 SPI 写入失败。
 */
Platform_StatusTypeDef Platform_LCD_FillRect(uint16_t x_start,
                                             uint16_t y_start,
                                             uint16_t x_end,
                                             uint16_t y_end,
                                             uint16_t color)
{
    if (ST7789_FillRect(&hplatform_lcd,
                         x_start,
                         y_start,
                         x_end,
                         y_end,
                         color) != ST7789_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "LCD",
                         "fill rect failed: error=%u, port=%u.",
                         (unsigned int)hplatform_lcd.ErrorCode,
                         (unsigned int)hplatform_lcd.LastPortStatus);
        return PLATFORM_LCD_ERROR;
    }

    return PLATFORM_OK;
}

/**
 * @brief  使用单一 RGB565 颜色填满当前 LCD 的全部可见区域。
 * @param  color RGB565 颜色。
 * @retval PLATFORM_OK 全屏像素已写入 Display RAM。
 * @retval PLATFORM_LCD_ERROR LCD 未就绪或 SPI 写入失败。
 * @note   尺寸由 ST7789 Device 统一定义，Platform 不重复保存分辨率常量。
 */
Platform_StatusTypeDef Platform_LCD_FillScreen(uint16_t color)
{
    return Platform_LCD_FillRect(0u,
                                 0u,
                                 (uint16_t)(ST7789_WIDTH - 1u),
                                 (uint16_t)(ST7789_HEIGHT - 1u),
                                 color);
}
