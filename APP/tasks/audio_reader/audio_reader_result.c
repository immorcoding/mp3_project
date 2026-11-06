/**
 ******************************************************************************
 * @file    audio_reader_result.c
 * @brief   “失败结果 + 卷序号 → 写什么停止原因”（纯函数，见 audio_reader_result.h）。
 ******************************************************************************
 */

#include "APP/tasks/audio_reader/audio_reader_result.h"

#include "Service/filesystem/filesystem_types.h"

/**
 * @brief  把一次读块的结果分类。
 * @param[in] status `Service_Filesystem_ReadFile` 的返回值。
 * @param[in] got 实际读到的字节数。
 * @param[in] volume_mounted SD 卷现在是否挂着（`volume_status()` 的挂载位）。
 * @param[in] volume_sequence SD 卷现在的卷序号。
 * @param[in] handled_sequence 读取任务已经处理过的卷序号。
 * @return 继续（块照常入队）、停住（卷变了，卷不可用，等卷事件）、重试（SERVICE_BUSY）、宽限（卷不可用而卷没变，
 *         有上界地等卷事件）或读错误（其他文件错误，或成功但读到 0 字节）。
 * @note   调用者须在读失败之后、先等过本轮对账（对同一个句柄 `CloseFile`，它在 Filesystem 入口等闸）再取卷状态。
 *         卷没变并不说明卡还在：卷任务与读取任务同级，卡检测中断不会让它立刻抢占，闸还没关时 `CloseFile` 直接通过，
 *         序号还没变，所以这里不能立刻报读错误，要进宽限。“卷没变”指卷挂着且序号等于已处理的；序号只比相等，
 *         不比大小，所以 31 位回绕不影响。
 */
AudioReader_ReadVerdictTypeDef audio_reader_classify_read(Service_StatusTypeDef status,
                                                          uint32_t got,
                                                          bool volume_mounted,
                                                          uint32_t volume_sequence,
                                                          uint32_t handled_sequence)
{
    if (Service_Filesystem_StatusIsVolumeUnavailable(status))
    {
        /* 卷变了：卡没了或正在对账，等卷事件。卷没变：卡刚拔而卷任务还没轮到，或卡还在设备出错，分不出来，
         * 进宽限——有上界地等卷事件，不立刻报读错误，也不静音到 T_data。 */
        return audio_reader_volume_changed(volume_mounted, volume_sequence, handled_sequence) ? AUDIO_READER_READ_STALL
                                                                                              : AUDIO_READER_READ_GRACE;
    }
    if (audio_reader_status_is_busy(status))
    {
        return AUDIO_READER_READ_RETRY;
    }

    return ((status == SERVICE_OK) && (got != 0U)) ? AUDIO_READER_READ_CONTINUE : AUDIO_READER_READ_ERROR;
}

/**
 * @brief  把开流或探测的 Service 状态收成开启结果。
 * @param[in] status 打开、取长度、定位或读取的 Service 状态。
 * @param[in] open_failure true 表示打开文件本身失败（无法区分缺失与损坏，按坏文件）。
 * @return 开启结果码：卷不可用与无文件系统为 `NOT_READY`，超时为 `IO_ERROR`，参数非法为 `BAD_FILE`，
 *         其余打开失败为 `BAD_FILE`、探测失败为 `IO_ERROR`。
 * @note   `SERVICE_BUSY` 由调用者在进到这里之前拦下重试（`audio_reader_status_is_busy()`），不应走到这里。
 */
AudioReader_OpenResultTypeDef audio_reader_classify_open_status(Service_StatusTypeDef status, bool open_failure)
{
    if (Service_Filesystem_StatusIsVolumeUnavailable(status))
    {
        return AUDIO_READER_OPEN_NOT_READY; /* NO_MEDIA / NOT_READY / INVALID_HANDLE 不是文件错误（#195 第 9 条）。 */
    }

    switch (status)
    {
        case SERVICE_NO_FILESYSTEM:
            return AUDIO_READER_OPEN_NOT_READY;
        case SERVICE_TIMEOUT:
            return AUDIO_READER_OPEN_IO_ERROR;
        case SERVICE_INVALID_PARAM:
            return AUDIO_READER_OPEN_BAD_FILE;
        default:
            return open_failure ? AUDIO_READER_OPEN_BAD_FILE : AUDIO_READER_OPEN_IO_ERROR;
    }
}

/**
 * @brief  这个状态是不是“文件槽暂时用尽”，要过约 10 ms 重试（不算文件错误）。
 * @param[in] status Service 状态。
 * @return `SERVICE_BUSY` 为真。
 */
bool audio_reader_status_is_busy(Service_StatusTypeDef status)
{
    return status == SERVICE_BUSY;
}

/**
 * @brief  判断当前流记录是否仍被播放任务持有（已成功开启、尚未终止或仅读尽）。
 * @param[in] record 当前流记录，不得为空。
 * @retval true  StreamSeq 非 0、OpenResult 为 OK，且 End 为 NONE（FIL 仍开）或 ENDED（读尽）。
 * @retval false 从未开启、开启失败，或已以 READ_ERROR / CARD_REMOVED 终止。
 */
bool audio_reader_stream_is_held(const AudioReader_RecordTypeDef *record)
{
    return (record->StreamSeq != 0U) &&
           (record->OpenResult == AUDIO_READER_OPEN_OK) &&
           ((record->End == AUDIO_READER_END_NONE) || (record->End == AUDIO_READER_END_ENDED));
}

/**
 * @brief  卷序号变了时，当前流要不要写终止原因。
 * @param[in] record 当前流记录，不得为空。
 * @param[in] volume_sequence SD 卷现在的卷序号。
 * @param[in] handled_sequence 读取任务已经处理过的卷序号。
 * @return `AUDIO_READER_END_CARD_REMOVED`：序号变了且流仍被播放任务持有（含已读完的流、换卡序号跳 2）；
 *         `AUDIO_READER_END_NONE`：什么都不写（序号没变，或没有被持有的流，或已经以读错误终止）。
 * @note   卷任务不等本任务：卸载已使旧令牌失效，晚一点收尾也碰不到旧卡；之后 `CloseFile` 只返回 `INVALID_HANDLE`，无害。
 */
AudioReader_EndTypeDef audio_reader_end_on_volume_change(const AudioReader_RecordTypeDef *record,
                                                         uint32_t volume_sequence,
                                                         uint32_t handled_sequence)
{
    if ((volume_sequence != handled_sequence) && audio_reader_stream_is_held(record))
    {
        return AUDIO_READER_END_CARD_REMOVED;
    }
    return AUDIO_READER_END_NONE;
}

/**
 * @brief  SERVICE_BUSY 的日志节拍：这一次是不是该记一行。
 * @param[in] retries 同一次开流或读块遇到 SERVICE_BUSY 的次数，含这一次，从 1 起数。
 * @return 第 1 次和之后每满 `AUDIO_READER_BUSY_LOG_EVERY` 次为真，其余为假；0 为假。
 */
bool audio_reader_busy_log_due(uint32_t retries)
{
    return (retries == 1U) || ((retries != 0U) && ((retries % AUDIO_READER_BUSY_LOG_EVERY) == 0U));
}

/**
 * @brief  卷是不是相对参考序号变了。
 * @param[in] volume_mounted SD 卷现在是否挂着。
 * @param[in] volume_sequence SD 卷现在的卷序号。
 * @param[in] reference_sequence 参考序号（读取任务已处理的序号，或宽限开始时的序号）。
 * @return 卷已不挂，或挂着而序号与参考不同（卸载、换卡跳 2、31 位回绕都算）为真；挂着且序号相同为假。
 */
bool audio_reader_volume_changed(bool volume_mounted, uint32_t volume_sequence, uint32_t reference_sequence)
{
    return !volume_mounted || (volume_sequence != reference_sequence);
}

/**
 * @brief  卷宽限里每圈的判断（读块和开流共用）。
 * @param[in] request_pending 请求信箱里已有新请求。
 * @param[in] volume_changed 卷相对宽限开始时变了（`audio_reader_volume_changed()`）。
 * @param[in] remaining_ticks 距宽限结束还有多少 tick，0 表示已到时间。
 * @return 优先级 新请求 > 卷事件 > 超时 > 继续等：新请求取代旧流所以宽限让位；卷事件到了就不再按超时处理；
 *         时间到了卷仍没变才是超时。
 * @note   让位时不写任何终止原因：处理新请求的那一步会清掉宽限；等待点因为“有新请求就不等”而不会死等。
 */
AudioReader_GraceActionTypeDef audio_reader_grace_step(bool request_pending, bool volume_changed, uint32_t remaining_ticks)
{
    if (request_pending)
    {
        return AUDIO_READER_GRACE_SUPERSEDED;
    }
    if (volume_changed)
    {
        return AUDIO_READER_GRACE_VOLUME_CHANGED;
    }
    return (remaining_ticks == 0U) ? AUDIO_READER_GRACE_EXPIRED : AUDIO_READER_GRACE_WAIT;
}

/**
 * @brief  开流或探测失败之后怎么跟进。
 * @param[in] busy 这次尝试里有一步得到 `SERVICE_BUSY`（文件槽用尽）。
 * @param[in] volume_unavailable 这次尝试里有一步得到 `NO_MEDIA`、`NOT_READY` 或 `INVALID_HANDLE`。
 * @param[in] volume_unchanged 尝试结束时卷挂着且序号没变。
 * @return `SERVICE_BUSY` 过约 10 ms 重试，优先于宽限；卷不可用而卷没变进宽限；其余照分类结果立刻应答
 *         （卷变了应答 `NOT_READY`，别的失败照旧）。曲库没好（代次 0）在到这里之前就应答了，不经过这里。
 */
AudioReader_OpenFollowTypeDef audio_reader_open_follow_up(bool busy, bool volume_unavailable, bool volume_unchanged)
{
    if (busy)
    {
        return AUDIO_READER_OPEN_FOLLOW_RETRY;
    }
    if (volume_unavailable && volume_unchanged)
    {
        return AUDIO_READER_OPEN_FOLLOW_GRACE;
    }
    return AUDIO_READER_OPEN_FOLLOW_ANSWER;
}

/**
 * @brief  开流宽限的结果。
 * @param[in] action 宽限里这一圈的判断。
 * @return 等到卷事件应答 `NOT_READY`（卡没了，播放侧显示 “SD card not ready”，游标重置）；超时仍没变应答 `IO_ERROR`
 *         （卡还在、设备出错，播放侧停止 READ_ERROR、游标保留）；继续等或被新请求取代返回 `PENDING`（不应答）。
 */
AudioReader_OpenResultTypeDef audio_reader_open_grace_result(AudioReader_GraceActionTypeDef action)
{
    switch (action)
    {
        case AUDIO_READER_GRACE_VOLUME_CHANGED:
            return AUDIO_READER_OPEN_NOT_READY;
        case AUDIO_READER_GRACE_EXPIRED:
            return AUDIO_READER_OPEN_IO_ERROR;
        case AUDIO_READER_GRACE_WAIT:
        case AUDIO_READER_GRACE_SUPERSEDED:
        default:
            return AUDIO_READER_OPEN_PENDING;
    }
}
