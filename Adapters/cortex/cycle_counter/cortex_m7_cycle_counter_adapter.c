/**
  ******************************************************************************
  * @file    cortex_m7_cycle_counter_adapter.c
  * @brief   Cortex-M7 DWT 周期计数器的具体实现。
  *
  * @details
  *          CoreDebug 的追踪开关、DWT 周期计数器和执行屏障集中在此处。上层只需在
  *          一次测量开始时复位并启动计数器，再读取单调递增的 32 位周期数。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"

#include "stm32h7xx.h"

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  复位并启动 Cortex-M7 DWT 的 32 位周期计数器。
  * @retval true 周期计数器已可用且已从 0 开始计数。
  * @retval false 当前 Cortex-M 实现未提供 CYCCNT，或使能请求未生效。
  * @note   CYCCNT 是整颗 CPU 共享的全局资源。本函数会清零已有计数，因此调用者
  *         必须保证没有其他 Module 同时使用该计数器进行测量。
  */
bool CortexM7CycleCounter_Start(void)
{
    if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0U)
    {
        return false;
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* 确保寄存器写入在调用者读取 CYCCNT 前已经对内核生效。 */
    __DSB();
    __ISB();

    return (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U;
}

/**
  * @brief  读取当前 Cortex-M7 DWT 周期计数值。
  * @retval 自上次 CortexM7CycleCounter_Start() 起累积的 32 位核心周期数。
  * @note   计数器按 SystemCoreClock 递增，32 位自然回绕；调用者应以无符号减法计算
  *         短时间区间，且不应跨越一个完整回绕周期。
  */
uint32_t CortexM7CycleCounter_Read(void)
{
    return DWT->CYCCNT;
}

/**
  * @brief  返回当前 Cortex-M7 核心频率。
  * @retval 当前由系统时钟初始化维护的核心频率，单位 Hz；不可用时为 0。
  * @note   该值供周期计数结果换算使用，不表示 FMC、SDMMC 或其他外设时钟。
  */
uint32_t CortexM7CycleCounter_GetFrequencyHz(void)
{
    return SystemCoreClock;
}
