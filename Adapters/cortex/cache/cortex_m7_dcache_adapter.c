/**
  ******************************************************************************
  * @file    cortex_m7_dcache_adapter.c
 * @brief   Cortex-M7 D-Cache 按范围维护的具体实现。
  *
  * @details
  *          对齐校验与 CMSIS Cache 操作集中在此处，避免每个调用者各自复制易错的
  *          Clean/Invalidate 顺序。当前 CMSIS 按地址 Cache Interface 已在内部执行
  *          所需的 DSB/ISB，因此本 Module 不额外叠加冗余屏障。DMA Adapter 可以据
  *          传输方向组合本 Module 的操作；SDRAM 诊断也可以使用相同 Interface 强制
  *          访问外部存储器。
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
 * @brief  校验一个内存范围可被按完整 Cortex-M7 Cache line 维护。
 * @param  buffer 要维护的内存首地址。
 * @param  byte_count 要维护的连续字节数。
  * @retval true 参数可安全传入 SCB D-Cache range Interface。
  * @retval false 地址未对齐、长度非法或参数为空。
  * @details
  *          CMSIS 的按地址 Cache 操作以完整 Cache line 为粒度。若未经调用者
  *          同意而向两端扩展范围，可能把相邻变量的脏数据写回或失效，故本 Module
  *          明确拒绝非整条 Cache line 的范围，而不是尝试“自动修正”。
  */
static bool cortex_m7_dcache_aligned_range_is_valid(const void *buffer,
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

/**
 * @brief  校验并计算一个仅向后补齐的 D-Cache Clean 范围。
 * @param  buffer 待 Clean 的内存首地址，必须按 Cache line 对齐。
 * @param  byte_count 调用者实际写入的连续字节数。
 * @param  rounded_byte_count 输出向后补齐至 Cache line 后的字节数。
 * @retval true 范围可安全传入 CMSIS Clean Interface。
 * @retval false 参数为空、首地址未对齐、长度非法或补齐后超出 CMSIS 长度上限。
 * @note   调用者必须独占从 buffer 起、长度为 rounded_byte_count 的全部范围。
 *         此 Interface 不允许用于 Invalidate，因为丢弃向后扩展 Cache line 中的
 *         相邻脏数据可能造成数据丢失。
 */
static bool cortex_m7_dcache_rounded_clean_range_is_valid(
    const void *buffer,
    uint32_t byte_count,
    uint32_t *rounded_byte_count)
{
    if ((buffer == NULL) ||
        (rounded_byte_count == NULL) ||
        (byte_count == 0U) ||
        (((uintptr_t)buffer % CORTEX_M7_DCACHE_LINE_SIZE) != 0U) ||
        (byte_count > ((uint32_t)INT32_MAX -
                       (CORTEX_M7_DCACHE_LINE_SIZE - 1U))))
    {
        return false;
    }

    *rounded_byte_count =
        (byte_count + CORTEX_M7_DCACHE_LINE_SIZE - 1U) &
        ~(CORTEX_M7_DCACHE_LINE_SIZE - 1U);
    return true;
}

/* Exported functions --------------------------------------------------------*/
/**
 * @brief  严格按完整 Cache line 将脏数据写回底层存储器。
  * @param  buffer 要 Clean 的内存范围首地址。
  * @param  byte_count 要维护的连续字节数。
  * @retval true 范围已完成 Clean 和数据同步屏障。
 * @retval false 首地址、长度或参数不满足完整 Cache line 的安全约束。
  * @details
  *          用于 CPU 写入的数据必须先对 DMA 或其他总线主设备可见的情形。调用方
  *          负责保证该范围在维护期间仍归自己独占。
  */
bool CortexM7DCache_Clean_Aligned(const void *buffer, uint32_t byte_count)
{
    if (!cortex_m7_dcache_aligned_range_is_valid(buffer, byte_count))
    {
        return false;
    }

    SCB_CleanDCache_by_Addr((uint32_t *)(uintptr_t)buffer, (int32_t)byte_count);
    return true;
}

/**
 * @brief  将一个首地址对齐但长度可不对齐的范围向后补齐后 Clean。
 * @param  buffer 要 Clean 的内存首地址，必须按 Cache line 对齐。
 * @param  byte_count 调用者实际写入的连续字节数。
 * @retval true 覆盖最后一条 Cache line 的 Clean 已完成。
 * @retval false 参数非法、首地址未对齐或补齐后长度过大。
 * @note   本函数实际维护 [buffer, buffer + rounded_byte_count)，其中
 *         rounded_byte_count 向上补齐到 Cache line 的整数倍。调用者必须拥有该
 *         扩展范围；本函数仅适用于 DMA 读取内存前的 Clean，不适用于 Invalidate。
 */
bool CortexM7DCache_Clean_Rounded(const void *buffer, uint32_t byte_count)
{
    uint32_t rounded_byte_count;

    if (!cortex_m7_dcache_rounded_clean_range_is_valid(buffer,
                                                        byte_count,
                                                        &rounded_byte_count))
    {
        return false;
    }

    SCB_CleanDCache_by_Addr((uint32_t *)(uintptr_t)buffer,
                            (int32_t)rounded_byte_count);
    return true;
}

/**
 * @brief  严格按完整 Cache line 使 CPU Cache 副本失效。
  * @param  buffer 要 Invalidate 的内存范围首地址。
  * @param  byte_count 要维护的连续字节数。
  * @retval true 范围已完成 Invalidate 和数据同步屏障。
 * @retval false 首地址、长度或参数不满足完整 Cache line 的安全约束。
  * @note   必须确保调用方允许丢弃该范围尚未提交的 Cache 数据；典型场景是 DMA 已
  *         经写入 RAM，CPU 需要重新从底层存储器读取新内容。
  */
bool CortexM7DCache_Invalidate_Aligned(const void *buffer,
                                        uint32_t byte_count)
{
    if (!cortex_m7_dcache_aligned_range_is_valid(buffer, byte_count))
    {
        return false;
    }

    SCB_InvalidateDCache_by_Addr((uint32_t *)(uintptr_t)buffer,
                                  (int32_t)byte_count);
    return true;
}

/**
 * @brief  严格按完整 Cache line 提交脏数据后使 Cache 副本失效。
  * @param  buffer 要 Clean + Invalidate 的内存范围首地址。
  * @param  byte_count 要维护的连续字节数。
  * @retval true 范围已完成 Clean、Invalidate 和数据同步屏障。
 * @retval false 首地址、长度或参数不满足完整 Cache line 的安全约束。
  * @details
  *          此顺序先保证旧脏数据不会在后续时刻回写覆盖底层存储器，再避免 CPU
  *          继续命中旧 Cache 副本。DMA 写入 RAM 前和 SDRAM 单点硬件诊断均可使用。
  */
bool CortexM7DCache_CleanInvalidate_Aligned(const void *buffer,
                                             uint32_t byte_count)
{
    if (!cortex_m7_dcache_aligned_range_is_valid(buffer, byte_count))
    {
        return false;
    }

    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)(uintptr_t)buffer,
                                       (int32_t)byte_count);
    return true;
}
