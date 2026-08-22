/**
  ******************************************************************************
  * @file    cortex_m7_cycle_counter_adapter.h
  * @brief   Cortex-M7 DWT 周期计数器的公共 Adapter Interface。
  *
  * @details
  *          本 Module 封装 CoreDebug、DWT 与 SystemCoreClock，使 Platform 诊断无需
  *          直接了解 Cortex-M7 核心寄存器。周期计数器为全局硬件资源；调用者必须在
  *          普通上下文串行使用 Start() 与 Read()，并在 32 位计数器回绕前完成单次测量。
  ******************************************************************************
  */

#ifndef CORTEX_M7_CYCLE_COUNTER_ADAPTER_H
#define CORTEX_M7_CYCLE_COUNTER_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

bool CortexM7CycleCounter_Start(void);
uint32_t CortexM7CycleCounter_Read(void);
uint32_t CortexM7CycleCounter_GetFrequencyHz(void);

#endif /* CORTEX_M7_CYCLE_COUNTER_ADAPTER_H */
