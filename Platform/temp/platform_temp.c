/**
  ******************************************************************************
  * @file    platform_temp.c
  * @brief   当前 PCB 的 MCU 结温 Platform 装配。
  ******************************************************************************
  */

#include "Platform/temp/platform_temp.h"

#include "Adapters/stm32_hal/temp/temp_stm32_hal_adapter.h"

#include <stdbool.h>

#include "adc.h"

/* 校准有效标志由本 Module 维护，防止调用者在首次校准前读取未校正的 ADC 数据。 */
static bool platform_temp_is_calibrated;

/**
  * @brief  校准当前 PCB 的 ADC3 温度采样通道。
  * @retval PLATFORM_OK 校准成功；PLATFORM_TEMP_ERROR 表示 ADC 校准失败。
  * @note   成功后校准结果由 ADC 保持。HAL_ADC_Stop() 不会使其失效；只有 ADC
  *         被 DeInit、进入 deep-power-down 或复位后才必须重新初始化。
  */
Platform_StatusTypeDef Platform_Temp_Init(void)
{
    platform_temp_is_calibrated = Temp_STM32HAL_Calibrate(&hadc3);

    return platform_temp_is_calibrated ? PLATFORM_OK : PLATFORM_TEMP_ERROR;
}

/**
  * @brief  读取当前 MCU 的片内温度传感器结温。
  * @param  temperature_mC 接收结温的有效地址，单位为毫摄氏度。
  * @retval PLATFORM_OK 成功；PLATFORM_TEMP_ERROR 表示 ADC 采样或换算失败。
  * @note   调用前必须成功执行 Platform_Temp_Init()。本 Module 只选择当前 PCB
  *         的 ADC3 实例并翻译状态；ADC 的采样顺序和工厂标定换算由 Adapter 负责。
  */
Platform_StatusTypeDef Platform_Temp_Read(int32_t *temperature_mC)
{
    if ((temperature_mC == NULL) ||
        !platform_temp_is_calibrated ||
        !Temp_STM32HAL_Read(&hadc3, temperature_mC))
    {
        return PLATFORM_TEMP_ERROR;
    }

    return PLATFORM_OK;
}
