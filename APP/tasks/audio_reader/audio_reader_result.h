/**
 ******************************************************************************
 * @file    audio_reader_result.h
 * @brief   “失败结果 + 卷序号 → 写什么停止原因”（纯函数，主机可测）。
 *
 * @details
 *          #195 第 9 条的失败约定：`NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 不是文件错误，说明卡没了或卷任务正在对账。
 *          读块得到它们时流“停住”：不写 `READ_ERROR`、不对同一个句柄重试，等卷事件，序号变了由
 *          `audio_reader_end_on_volume_change()` 决定写 `CARD_REMOVED`；开流和探测得到它们按 `NOT_READY` 应答。
 *          **前提是卷确实变了**（SD 卷序号与读取任务已处理的不同，或卷已不挂，且已等过本轮对账）。卷没变而得到它们有两种
 *          可能，分不出来：① 卡刚拔、边沿已到而卷任务还没轮到——读取任务与卷任务同为优先级 3，卡检测中断不会让同级的
 *          卷任务立刻抢占，闸还没关、序号还没变，`CloseFile` 直接通过；② 卡还在、设备出了错（例如标签读取传输出错使设备进
 *          ERROR，FatFs 的下一次读得到 `NOT_READY`；`FR_NOT_READY` 也映射成 `NO_MEDIA`），没有边沿就不会有卷事件。
 *          所以不立刻报读错误（①会把拔卡误报成读错误，之后也不再写 `CARD_REMOVED`），也不无限等（②会静音到 T_data）：
 *          进入**卷宽限**，在等待点上等一个有上界的时间（`AUDIO_READER_VOLUME_GRACE_MS`）。期间来了卷事件（序号变了）按
 *          拔卡处理，超时仍没变才报读错误（开流则应答 `IO_ERROR`）；新请求取代旧流，宽限让位。
 *          其他错误表示卡还在，同样立刻按原来的方式报错。
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
    AUDIO_READER_READ_GRACE,         /**< 卷不可用而卷没变：卡刚拔卷任务还没轮到，或卡还在设备出错，分不出来；进卷宽限，有上界地等卷事件。 */
    AUDIO_READER_READ_ERROR          /**< 文件错误（卡还在）或读到 0 字节：写 READ_ERROR。 */
} AudioReader_ReadVerdictTypeDef;

/** @brief 卷宽限里每圈的判断（读块和开流共用）。 */
typedef enum
{
    AUDIO_READER_GRACE_WAIT = 0U,    /**< 继续等：没有新请求、卷没变、还剩时间。 */
    AUDIO_READER_GRACE_VOLUME_CHANGED, /**< 等到卷事件：卷序号变了或卷已不挂。读块按拔卡写 CARD_REMOVED，开流应答 NOT_READY。 */
    AUDIO_READER_GRACE_EXPIRED,      /**< 超时：宽限用完卷仍没变，卡还在。读块写 READ_ERROR，开流应答 IO_ERROR。 */
    AUDIO_READER_GRACE_SUPERSEDED    /**< 等待中来了新请求：新请求取代旧流，宽限让位，不写任何终止原因。 */
} AudioReader_GraceActionTypeDef;

/** @brief 开流或探测失败之后怎么跟进。 */
typedef enum
{
    AUDIO_READER_OPEN_FOLLOW_ANSWER = 0U, /**< 照分类结果立刻应答。 */
    AUDIO_READER_OPEN_FOLLOW_RETRY,       /**< SERVICE_BUSY：不应答，过约 10 ms 重跑整个开流。 */
    AUDIO_READER_OPEN_FOLLOW_GRACE        /**< 卷不可用而卷没变：不应答，进卷宽限等卷事件。 */
} AudioReader_OpenFollowTypeDef;

AudioReader_ReadVerdictTypeDef audio_reader_classify_read(Service_StatusTypeDef status,
                                                          uint32_t got,
                                                          bool volume_mounted,
                                                          uint32_t volume_sequence,
                                                          uint32_t handled_sequence);
AudioReader_OpenResultTypeDef audio_reader_classify_open_status(Service_StatusTypeDef status, bool open_failure);
bool audio_reader_volume_changed(bool volume_mounted, uint32_t volume_sequence, uint32_t reference_sequence);
AudioReader_GraceActionTypeDef audio_reader_grace_step(bool request_pending, bool volume_changed, uint32_t remaining_ticks);
AudioReader_OpenFollowTypeDef audio_reader_open_follow_up(bool busy, bool volume_unavailable, bool volume_unchanged);
AudioReader_OpenResultTypeDef audio_reader_open_grace_result(AudioReader_GraceActionTypeDef action);
bool audio_reader_status_is_busy(Service_StatusTypeDef status);
bool audio_reader_stream_is_held(const AudioReader_RecordTypeDef *record);
AudioReader_EndTypeDef audio_reader_end_on_volume_change(const AudioReader_RecordTypeDef *record,
                                                         uint32_t volume_sequence,
                                                         uint32_t handled_sequence);
bool audio_reader_busy_log_due(uint32_t retries);

#endif /* AUDIO_READER_RESULT_H */
