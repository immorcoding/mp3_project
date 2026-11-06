/**
 ******************************************************************************
 * @file    audio_reader_result.h
 * @brief   “失败结果 + 卷序号 → 写什么停止原因”（纯函数，主机可测）。
 *
 * @details
 *          #195 第 9 条的失败约定：`NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 不是文件错误，说明卡没了或卷任务正在对账。
 *          读块得到它们时流“停住”：不写 `READ_ERROR`、不对同一个句柄重试，等卷事件，序号变了由
 *          `audio_reader_end_on_volume_change()` 决定写 `CARD_REMOVED`；开流和探测得到它们按 `NOT_READY` 应答。
 *          **前提是卷确实变了**（SD 卷序号与读取任务已处理的不同，或卷已不挂，且已等过本轮对账）：卷没变而读块得到它们，
 *          是卡还在、设备出了错（例如标签读取传输出错使设备进 ERROR，FatFs 的下一次读得到 `NOT_READY`），没有边沿就
 *          不会有卷事件，一直等会静音到 T_data，所以立刻按读错误报。其他错误表示卡还在，同样立刻按原来的方式报错。
 *
 *          `SERVICE_BUSY`（文件槽暂时用尽）不是文件错误：过约 10 ms 重试并记日志，T_data 兜底（#194 修正的第 13 条）。
 ******************************************************************************
 */

#ifndef AUDIO_READER_RESULT_H
#define AUDIO_READER_RESULT_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/tasks/audio_reader/audio_reader_stream.h"
#include "Service/service.h"

/** @brief 一次读块结果的分类。 */
typedef enum
{
    AUDIO_READER_READ_CONTINUE = 0U, /**< 读到了字节，块照常入队。 */
    AUDIO_READER_READ_STALL,         /**< 卷变了（卡没了或正在对账）：流停住，等卷事件，不写读错误。 */
    AUDIO_READER_READ_RETRY,         /**< SERVICE_BUSY：不是文件错误，过约 10 ms 在同一个文件位置重读。 */
    AUDIO_READER_READ_ERROR          /**< 文件错误（卡还在，含卷没变时的卷不可用）或读到 0 字节：写 READ_ERROR。 */
} AudioReader_ReadVerdictTypeDef;

AudioReader_ReadVerdictTypeDef audio_reader_classify_read(Service_StatusTypeDef status,
                                                          uint32_t got,
                                                          bool volume_mounted,
                                                          uint32_t volume_sequence,
                                                          uint32_t handled_sequence);
AudioReader_OpenResultTypeDef audio_reader_classify_open_status(Service_StatusTypeDef status, bool open_failure);
bool audio_reader_status_is_busy(Service_StatusTypeDef status);
bool audio_reader_stream_is_held(const AudioReader_RecordTypeDef *record);
AudioReader_EndTypeDef audio_reader_end_on_volume_change(const AudioReader_RecordTypeDef *record,
                                                         uint32_t volume_sequence,
                                                         uint32_t handled_sequence);
bool audio_reader_busy_log_due(uint32_t retries);

#endif /* AUDIO_READER_RESULT_H */
