/**
  ******************************************************************************
  * @file    platform_touch.c
  * @brief   当前 PCB FT6X36、I2C2 与 TP_RST 的 Platform 装配实现。
  *
  * @details
  *          本 Module 长期持有 FT6X36 Device 和 STM32 HAL I2C/GPIO Adapter
  *          Context，将 CubeMX 的 I2C2、TP_RST 与本板触摸地址装配为上层只需
  *          了解的初始化和 Chip ID 读取能力。TP_IRQ 暂不参与轮询式最小链路。
  ******************************************************************************
  */

#include "Platform/touch/platform_touch.h"
#include "Platform/touch/platform_touch_config.h"

#include "Adapters/stm32_hal/ft6x36_i2c/ft6x36_i2c_stm32_hal_adapter.h"
#include "Components/ft6x36/ft6x36.h"
#include "Components/log/log.h"
#include "i2c.h"
#include "main.h"

/** @brief 当前 PCB 唯一的 FT6X36 Device 实例。 */
static FT6X36_HandleTypeDef hplatform_touch = {
    .Address7Bit = PLATFORM_TOUCH_I2C_ADDRESS_7BIT
};

/**
  * @brief 本板 FT6X36 使用的 I2C2 和复位 GPIO Adapter Context。
  * @note  I2C Handle、TP_RST 引脚与低有效极性均由 Platform 装配；Adapter 不会
  *        自行选择 CubeMX 实例或 PCB 资源。
  */
static FT6X36_I2C_STM32HALAdapterTypeDef hplatform_touch_adapter = {
    .I2CHandle = &hi2c2,
    .ResetPort = TP_RST_GPIO_Port,
    .ResetPin = TP_RST_Pin,
    .ResetAssertState = GPIO_PIN_RESET,
    .ProbeTrials = PLATFORM_TOUCH_I2C_PROBE_TRIALS,
    .TimeoutMs = PLATFORM_TOUCH_I2C_TIMEOUT_MS
};

/* 仅在启动与 GUI Task 中读写；false 表示不得再进入触摸 I2C 访问路径。 */
static bool platform_touch_available;

/**
  * @brief  初始化当前 PCB 的 FT6X36 并确认 I2C2 通信可用。
  * @retval PLATFORM_OK 触摸控制器已完成启动初始化且可读取寄存器。
  * @retval PLATFORM_TOUCH_ERROR Adapter 绑定、初始化或 I2C 地址探测失败。
  * @note   本 Module 不设置 LCD 电源轨；调用者必须先完成当前显示模组所需的
  *         供电。当前实现只允许由 app_init() 经 Platform_Init() 在 FreeRTOS
  *         调度器启动前调用，这样触摸的供电策略仍由 Platform LCD/Power 单独
  *         拥有。
  */
Platform_StatusTypeDef Platform_Touch_Init(void)
{
    /* 重新初始化失败时，不能泄漏上一次成功初始化留下的可用状态。 */
    platform_touch_available = false;

    if (FT6X36_I2C_STM32HALAdapter_Bind(&hplatform_touch,
                                         &hplatform_touch_adapter) != FT6X36_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR, "TOUCH", "I2C adapter bind failed.");
        return PLATFORM_TOUCH_ERROR;
    }

    if (FT6X36_Init(&hplatform_touch) != FT6X36_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "TOUCH",
                         "initialization failed: error=%u, port=%u.",
                         (unsigned int)hplatform_touch.ErrorCode,
                         (unsigned int)hplatform_touch.LastPortStatus);
        return PLATFORM_TOUCH_ERROR;
    }

    platform_touch_available = true;
    return PLATFORM_OK;
}

/**
  * @brief  查询当前触摸硬件是否仍可供上层读取。
  * @retval true 最近一次初始化成功且未发生读取失败。
  * @retval false 初始化失败，或读取触点时已经发生通信失败。
  * @note   该查询不访问 I2C。当前只由启动阶段和唯一 GUI Task 使用，故无需额外
  *         同步；未来若由多个 Task 共享触摸能力，应重新定义状态并发规则。
  */
bool Platform_Touch_IsAvailable(void)
{
    return platform_touch_available;
}

/**
  * @brief  读取当前触摸控制器返回的原始 Chip ID 字节。
  * @param  chip_id 接收 Chip ID 的有效地址。
  * @retval PLATFORM_OK 成功读取寄存器 0xA3。
  * @retval PLATFORM_TOUCH_ERROR 参数无效、控制器未初始化或 I2C 读取失败。
  * @note   当前不比较固定 ID 值；日志中的实际结果将用于确认当前模组的具体
  *         FT6X36 系列型号，再决定后续是否增加型号校验。
  */
Platform_StatusTypeDef Platform_Touch_ReadID(uint8_t *chip_id)
{
    if (FT6X36_ReadID(&hplatform_touch, chip_id) != FT6X36_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "TOUCH",
                         "Chip ID read failed: error=%u, port=%u, reg=0x%02X.",
                         (unsigned int)hplatform_touch.ErrorCode,
                         (unsigned int)hplatform_touch.LastPortStatus,
                         (unsigned int)hplatform_touch.LastFailedRegister);
        return PLATFORM_TOUCH_ERROR;
    }

    return PLATFORM_OK;
}

/**
  * @brief  读取当前触摸控制器的第一触点原始状态和坐标。
  * @param  point 接收原始按下状态与 X/Y 的有效地址。
  * @retval PLATFORM_OK 触点帧读取成功；无触摸时 IsPressed 为 false。
  * @retval PLATFORM_TOUCH_ERROR 参数无效、控制器未就绪或 I2C 读取失败。
  * @note   本 Module 不转换坐标方向或解释多指手势；这些 UI 语义由调用者按
  *         当前显示方向决定。当前实现只转发 FT6X36 的第一触点。
  */
Platform_StatusTypeDef Platform_Touch_ReadRawPoint(
    Platform_Touch_RawPointTypeDef *point)
{
    FT6X36_RawPointTypeDef raw_point;

    if (point == NULL)
    {
        return PLATFORM_TOUCH_ERROR;
    }

    if (!platform_touch_available)
    {
        return PLATFORM_TOUCH_ERROR;
    }

    if (FT6X36_ReadRawPoint(&hplatform_touch, &raw_point) != FT6X36_OK)
    {
        /* 失败后的 Device 已不再 READY，后续轮询既无意义也会反复占用 I2C。 */
        platform_touch_available = false;
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "TOUCH",
                         "Touch point read failed: error=%u, port=%u, reg=0x%02X.",
                         (unsigned int)hplatform_touch.ErrorCode,
                         (unsigned int)hplatform_touch.LastPortStatus,
                         (unsigned int)hplatform_touch.LastFailedRegister);
        return PLATFORM_TOUCH_ERROR;
    }

    point->IsPressed = raw_point.IsPressed;
    point->X = raw_point.X;
    point->Y = raw_point.Y;
    return PLATFORM_OK;
}
