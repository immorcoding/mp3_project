/**
 ******************************************************************************
 * @file    audio_reader_pace.h
 * @brief   音频读取任务“决定等多久”的纯函数（#194 第 2、4 条）。
 *
 * @details
 *          每圈顺序是 卷事件 → 请求信箱 → 读一块 → 决定等多久，只有一个等待点，叫醒它的只有新请求、块归还、卷事件。
 *          “等多久”的规则：
 *          - 信箱里已经有新请求：不等（取消和新流不能被让路或空等挡住）；
 *          - SERVICE_BUSY 重试在等：只等到重试时刻（约 10 ms），到了不等；
 *          - 卷宽限在等（读块或开流遇到卷不可用而卷没变，等卷事件）：只等到宽限结束（初值约 100 ms），到了不等，
 *            让这一圈判超时；
 *          - 没有打开的流，或没有空闲块：一直等（暂停时块池满了就不再读，流保持打开）；
 *          - 就绪块不到水位：不等，连续补块；
 *          - 够水位且还有空闲块：等 1 tick（让路，让的是 SD 锁）。
 *          纯函数，不依赖 FreeRTOS：一直等用 `AUDIO_READER_WAIT_FOREVER`，由任务映射成 `portMAX_DELAY`。
 ******************************************************************************
 */

#ifndef AUDIO_READER_PACE_H
#define AUDIO_READER_PACE_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/tasks/audio_reader/audio_reader_config.h"

#define AUDIO_READER_WAIT_FOREVER UINT32_MAX /**< 一直等：叫醒它的只有新请求、块归还、卷事件。 */

/** @brief 决定等多久所需的当前事实，由流模块在每圈末尾填写。 */
typedef struct
{
    bool RequestPending;            /**< 请求信箱里已有尚未处理的新请求。 */
    bool RetryActive;               /**< 有一次 SERVICE_BUSY 重试在等（开流或读块）。 */
    uint32_t RetryRemainingTicks;   /**< 距重试时刻还有多少 tick；0 表示已到时刻。仅 RetryActive 时有意义。 */
    bool GraceActive;               /**< 有一次卷宽限在等（读块或开流遇到卷不可用而卷没变）。 */
    uint32_t GraceRemainingTicks;   /**< 距宽限结束还有多少 tick；0 表示已到时刻。仅 GraceActive 时有意义。 */
    bool StreamOpen;                /**< 有打开的文件且还有字节要读（读完、读错、拔卡停住后为假）。 */
    uint32_t FreeBlocks;            /**< 空闲队列里的块数。 */
    uint32_t ReadyBlocks;           /**< 就绪队列里的块数；不含播放任务手里那块。 */
} AudioReader_PaceInputTypeDef;

uint32_t audio_reader_wait_ticks(const AudioReader_PaceInputTypeDef *input);
uint32_t audio_reader_ticks_until(uint32_t now, uint32_t deadline);

#endif /* AUDIO_READER_PACE_H */
