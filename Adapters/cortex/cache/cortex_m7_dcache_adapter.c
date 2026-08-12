/**
  ******************************************************************************
  * @file    cortex_m7_dcache_adapter.c
  * @brief   Cortex-M7 D-Cache 与 DMA 缓冲区一致性的具体实现。
  *
  * @details
  *          Cortex-M7 的 DMA 外设直接访问 RAM，不能观察 CPU 私有 D-Cache 中的
  *          脏数据。对齐校验和 SCB Cache 维护集中在此处，避免每个外设 Adapter
  *          各自复制易错的 Clean/Invalidate 顺序。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "Adapters/cortex/cache/cortex_m7_dcache_adapter.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "stm32h7xx.h"

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  校验一个 DMA 缓冲区可被按完整 Cortex-M7 Cache line 维护。
  * @param  buffer DMA 将读取或写入的内存首地址。
  * @param  byte_count DMA 访问的连续字节数。
  * @retval true 参数可安全传入 SCB D-Cache range Interface。
  * @retval false 地址未对齐、长度非法或参数为空。
  * @details
  *          CMSIS 的按地址 Cache 操作以完整 Cache line 为粒度。若未经调用者
  *          同意而向两端扩展范围，可能把相邻变量的脏数据写回或失效，故本 Module
  *          明确拒绝非整条 Cache line 的范围，而不是尝试“自动修正”。
  */
static bool cortex_m7_dcache_range_is_valid(const void *buffer,
                                            uint32_t byte_count)
{
    if ((buffer == NULL) ||
        (byte_count == 0U) ||
        (((uintptr_t)buffer % CORTEX_M7_DCACHE_LINE_SIZE) != 0U) ||
        ((byte_count % CORTEX_M7_DCACHE_LINE_SIZE) != 0U) ||
        (byte_count > (uint32_t)INT32_MAX))
    {
        return false;
    }

    return true;
}

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  为一次外设到 RAM 的 DMA 接收准备目标缓冲区。
  * @param  buffer DMA 即将覆盖的 RAM 目标缓冲区。
  * @param  byte_count 本次 DMA 将写入的连续字节数。
  * @retval true 缓冲区已完成 Clean、Invalidate 和数据同步屏障。
  * @retval false 缓冲区范围不满足完整 Cache line 的安全约束。
  * @details
  *          Clean 防止旧脏 Cache line 在 DMA 进行期间被替换并写回 RAM，从而覆盖
  *          外设刚写入的数据；Invalidate 则使 CPU 不会继续命中 DMA 开始前的旧
  *          Cache 副本。调用者仍拥有 buffer，且必须保证 DMA 完成前不由 CPU 改写。
  */
bool CortexM7DCache_PrepareDMARx(void *buffer, uint32_t byte_count)
{
    if (!cortex_m7_dcache_range_is_valid(buffer, byte_count))
    {
        return false;
    }

    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)buffer, (int32_t)byte_count);
    __DSB();
    return true;
}

/**
  * @brief  在外设到 RAM 的 DMA 接收完成后使 CPU 读取新数据。
  * @param  buffer 已被 DMA 写入的 RAM 目标缓冲区。
  * @param  byte_count 已完成 DMA 的连续字节数。
  * @retval true 缓冲区已完成 Invalidate 和数据同步屏障。
  * @retval false 缓冲区范围不满足完整 Cache line 的安全约束。
  * @note   调用者必须先确认 DMA 数据阶段已经结束；本函数不等待中断，也不得从
  *         ISR 调用。
  */
bool CortexM7DCache_CompleteDMARx(void *buffer, uint32_t byte_count)
{
    if (!cortex_m7_dcache_range_is_valid(buffer, byte_count))
    {
        return false;
    }

    SCB_InvalidateDCache_by_Addr((uint32_t *)buffer, (int32_t)byte_count);
    __DSB();
    return true;
}

/**
  * @brief  为一次 RAM 到外设的 DMA 发送准备源缓冲区。
  * @param  buffer DMA 即将读取的 RAM 源缓冲区。
  * @param  byte_count 本次 DMA 将读取的连续字节数。
  * @retval true 缓冲区已完成 Clean 和数据同步屏障。
  * @retval false 缓冲区范围不满足完整 Cache line 的安全约束。
  * @details
  *          CPU 刚写入的数据可能只存在于 D-Cache。Clean 会在 DMA 启动前把它
  *          同步到 RAM，使外设读取到当前内容；发送方向完成后通常不需要额外的
  *          Cache 操作。
  */
bool CortexM7DCache_PrepareDMATx(const void *buffer, uint32_t byte_count)
{
    if (!cortex_m7_dcache_range_is_valid(buffer, byte_count))
    {
        return false;
    }

    SCB_CleanDCache_by_Addr((uint32_t *)(uintptr_t)buffer, (int32_t)byte_count);
    __DSB();
    return true;
}
