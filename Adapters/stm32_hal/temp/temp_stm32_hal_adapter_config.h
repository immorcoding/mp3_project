/**
  ******************************************************************************
  * @file    temp_stm32_hal_adapter_config.h
  * @brief   STM32 HAL 内部温度采样 Adapter 的固定配置。
  ******************************************************************************
  */

#ifndef TEMP_STM32_HAL_ADAPTER_CONFIG_H
#define TEMP_STM32_HAL_ADAPTER_CONFIG_H

/**
 * @brief HAL ADC 单次转换等待的最长时间，单位为毫秒。
 * @note  当前 ADC3 以 10 MHz 时钟采集两个内部通道，实际耗时远小于该上限。
 *        该超时只防止外设异常时 Monitor Task 长时间阻塞。
 */
#define TEMP_STM32HAL_POLL_TIMEOUT_MS                 10U

/** @brief 摄氏度转换为毫摄氏度的倍率。 */
#define TEMP_STM32HAL_MILLICELSIUS_PER_CELSIUS         1000LL

#endif // TEMP_STM32_HAL_ADAPTER_CONFIG_H
