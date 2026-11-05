/**
 ******************************************************************************
 * @file    storage_audio_result.h
 * @brief   音频流对文件调用结果的分类（纯函数，主机可测）。
 *
 * @details
 *          #195 第 9 条的失败约定：`NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 不是文件错误，说明卡没了或卷任务正在对账。
 *          读块得到它们时流“停住”：不写 `READ_ERROR`、不对同一个句柄重试，等卷事件，序号变了由
 *          `storage_audio_stream_card_removed()` 写 `CARD_REMOVED`；开流和探测得到它们按 `NOT_READY` 应答。
 *          其他错误表示卡还在（没有边沿就不会有卷事件），立刻按原来的方式报错。
 ******************************************************************************
 */

#ifndef STORAGE_AUDIO_RESULT_H
#define STORAGE_AUDIO_RESULT_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/tasks/storage/stream/storage_audio_stream.h"
#include "Service/service.h"

/** @brief 一次读块结果的分类。 */
typedef enum
{
    STORAGE_AUDIO_READ_CONTINUE = 0U, /**< 读到了字节，块照常入队。 */
    STORAGE_AUDIO_READ_STALL,         /**< 卷不可用：流停住，等卷事件，不写读错误。 */
    STORAGE_AUDIO_READ_ERROR          /**< 文件错误（卡还在）或读到 0 字节：写 READ_ERROR。 */
} Storage_AudioReadVerdictTypeDef;

Storage_AudioReadVerdictTypeDef storage_audio_classify_read(Service_StatusTypeDef status, uint32_t got);
Storage_AudioOpenResultTypeDef storage_audio_classify_open_status(Service_StatusTypeDef status, bool open_failure);

#endif /* STORAGE_AUDIO_RESULT_H */
