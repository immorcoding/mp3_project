/**
  ******************************************************************************
  * @file    foreground_stats.h
  * @brief   前台任务的耗时统计累加器：次数、最小、平均、最大（spike #191）。
  ******************************************************************************
  */

#ifndef FOREGROUND_STATS_H
#define FOREGROUND_STATS_H

#include <stdint.h>

/** @brief 一组耗时样本的汇总，单位微秒。 */
typedef struct
{
    uint32_t Count;  /**< 样本数。 */
    uint64_t SumUs;  /**< 总和。 */
    uint32_t MinUs;  /**< 最小值；没有样本时为 0。 */
    uint32_t MaxUs;  /**< 最大值。 */
} ForegroundStatTypeDef;

/**
 * @brief 清零累加器。
 * @param[out] stat 要清零的累加器。
 */
static inline void foreground_stat_reset(ForegroundStatTypeDef *stat)
{
    stat->Count = 0U;
    stat->SumUs = 0U;
    stat->MinUs = 0U;
    stat->MaxUs = 0U;
}

/**
 * @brief 加入一个样本。
 * @param[in,out] stat 累加器。
 * @param[in] us 样本，微秒。
 */
static inline void foreground_stat_add(ForegroundStatTypeDef *stat, uint32_t us)
{
    if ((stat->Count == 0U) || (us < stat->MinUs))
    {
        stat->MinUs = us;
    }
    if (us > stat->MaxUs)
    {
        stat->MaxUs = us;
    }
    stat->SumUs += us;
    stat->Count++;
}

/**
 * @brief 平均值。
 * @param[in] stat 累加器。
 * @return 平均微秒；没有样本时为 0。
 */
static inline uint32_t foreground_stat_avg_us(const ForegroundStatTypeDef *stat)
{
    return (stat->Count == 0U) ? 0U : (uint32_t)(stat->SumUs / stat->Count);
}

#endif /* FOREGROUND_STATS_H */
