/**
  ******************************************************************************
  * @file    temp_stm32_hal_adapter.c
  * @brief   STM32H7 内部温度传感器的 ADC HAL 采样与工厂标定换算。
  *
  * @details
  *          本 Module 隐藏 ADC 校准、双 Rank 轮询、内部参考电压校正及工厂标定
  *          数据的读取。调用者只取得毫摄氏度表示的 MCU 结温，不接触 ADC 原始值。
  ******************************************************************************
  */

#include "Adapters/stm32_hal/temp/temp_stm32_hal_adapter.h"
#include "Adapters/stm32_hal/temp/temp_stm32_hal_adapter_config.h"

#include <limits.h>
#include <stdint.h>

#include "stm32h7xx_ll_adc.h"

/**
  * @brief  等待当前 ADC Rank 转换完成并读取原始值。
  * @param  hadc 已启动的 ADC Handle。
  * @param  value 接收 ADC DR 原始值的有效地址。
  * @retval true 成功读取一个 Rank；false 表示等待超时或参数无效。
  * @note   本 Module 要求 CubeMX 启用 LowPowerAutoWait。读取 DR 会释放 ADC，
  *         使双 Rank 序列开始下一次转换；因此两次调用可分别取得温度与 VREFINT。
  */
static bool temp_stm32_hal_read_value(ADC_HandleTypeDef *hadc, uint32_t *value)
{
    if ((hadc == NULL) || (value == NULL))
    {
        return false;
    }

    if (HAL_ADC_PollForConversion(hadc, TEMP_STM32HAL_POLL_TIMEOUT_MS) != HAL_OK)
    {
        return false;
    }

    *value = HAL_ADC_GetValue(hadc);
    return true;
}

/**
  * @brief  以 VREFINT 样本修正供电电压，再按芯片工厂标定数据换算结温。
  * @param  raw_temperature ADC3 温度传感器 Rank 的原始值。
  * @param  raw_vrefint ADC3 VREFINT Rank 的原始值。
  * @param  temperature_mC 接收毫摄氏度结果的有效地址。
  * @retval true 换算成功；false 表示原始值或工厂标定数据异常。
  * @note   工厂校准数据以校准电压测得。必须先由 VREFINT 求得实际 VDDA，随后把
  *         温度样本缩放回校准电压，才可在线性插值中正确使用两个校准点。
  */
static bool temp_stm32_hal_calculate_temperature(uint32_t raw_temperature,
                                                   uint32_t raw_vrefint,
                                                   int32_t *temperature_mC)
{
    const uint32_t vrefint_calibration = *VREFINT_CAL_ADDR;
    const int32_t temperature_calibration_1 = (int32_t)*TEMPSENSOR_CAL1_ADDR;
    const int32_t temperature_calibration_2 = (int32_t)*TEMPSENSOR_CAL2_ADDR;
    const int32_t calibration_temperature_1 = TEMPSENSOR_CAL1_TEMP;
    const int32_t calibration_temperature_2 = TEMPSENSOR_CAL2_TEMP;
    const int32_t calibration_delta_raw =
        temperature_calibration_2 - temperature_calibration_1;
    const int32_t calibration_delta_temperature =
        calibration_temperature_2 - calibration_temperature_1;
    uint32_t vdda_mV;
    int64_t scaled_temperature_raw;
    int64_t temperature;

    if ((temperature_mC == NULL) ||
        (raw_vrefint == 0U) ||
        (vrefint_calibration == 0U) ||
        (calibration_delta_raw <= 0) ||
        (calibration_delta_temperature <= 0))
    {
        return false;
    }

    vdda_mV = (uint32_t)(((uint64_t)vrefint_calibration * VREFINT_CAL_VREF) /
                         raw_vrefint);
    if (vdda_mV == 0U)
    {
        return false;
    }

    scaled_temperature_raw = ((int64_t)raw_temperature * (int64_t)vdda_mV) /
                             (int64_t)TEMPSENSOR_CAL_VREFANALOG;

    temperature = ((int64_t)calibration_temperature_1 *
                   TEMP_STM32HAL_MILLICELSIUS_PER_CELSIUS) +
                  (((scaled_temperature_raw - (int64_t)temperature_calibration_1) *
                    (int64_t)calibration_delta_temperature *
                    TEMP_STM32HAL_MILLICELSIUS_PER_CELSIUS) /
                   (int64_t)calibration_delta_raw);

    if ((temperature < INT32_MIN) || (temperature > INT32_MAX))
    {
        return false;
    }

    *temperature_mC = (int32_t)temperature;
    return true;
}

/**
  * @brief  读取 STM32H7 内部温度传感器，并转换为毫摄氏度。
  * @param  hadc 由 CubeMX 配置的 ADC3 Handle。
  * @param  temperature_mC 接收结温的有效地址，单位为毫摄氏度。
  * @retval true 成功；false 表示 ADC 采样或换算失败。
  * @note   本函数依赖 CubeMX 将 ADC3 Regular Sequence 固定为 Rank 1 = Temperature
  *         Sensor、Rank 2 = VREFINT，并启用 LowPowerAutoWait。每次 HAL_ADC_GetValue()
  *         读取 DR 后，硬件才继续执行下一 Rank，避免后一个结果覆盖前一个结果。
  *         调用前必须成功执行 Temp_STM32HAL_Calibrate()；它不适用于高频连续采样。
  */
bool Temp_STM32HAL_Read(ADC_HandleTypeDef *hadc, int32_t *temperature_mC)
{
    uint32_t raw_temperature = 0U;
    uint32_t raw_vrefint = 0U;
    bool result = false;

    if ((hadc == NULL) ||
        (temperature_mC == NULL) ||
        (hadc->Init.ScanConvMode != ADC_SCAN_ENABLE) ||
        (hadc->Init.NbrOfConversion != 2U) ||
        (hadc->Init.EOCSelection != ADC_EOC_SINGLE_CONV) ||
        (hadc->Init.LowPowerAutoWait != ENABLE))
    {
        return false;
    }

    if (HAL_ADC_Start(hadc) != HAL_OK)
    {
        return false;
    }

    if (temp_stm32_hal_read_value(hadc, &raw_temperature) &&
        temp_stm32_hal_read_value(hadc, &raw_vrefint))
    {
        result = temp_stm32_hal_calculate_temperature(raw_temperature,
                                                       raw_vrefint,
                                                       temperature_mC);
    }

    if (HAL_ADC_Stop(hadc) != HAL_OK)
    {
        result = false;
    }

    return result;
}

/**
  * @brief  校准 STM32H7 ADC 的单端输入偏移。
  * @param  hadc 由 CubeMX 配置且未启动转换的 ADC Handle。
  * @retval true 校准成功；false 表示参数无效或 HAL 校准失败。
  * @note   校准结果在 HAL_ADC_Stop() 后仍保留。只有 ADC 复位、DeInit 或进入
  *         deep-power-down 后，才需要再次调用本函数。
  */
bool Temp_STM32HAL_Calibrate(ADC_HandleTypeDef *hadc)
{
    if (hadc == NULL)
    {
        return false;
    }

    return HAL_ADCEx_Calibration_Start(hadc,
                                       ADC_CALIB_OFFSET,
                                       ADC_SINGLE_ENDED) == HAL_OK;
}
