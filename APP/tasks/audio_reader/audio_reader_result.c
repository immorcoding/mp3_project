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
 * @return 继续（块照常入队）、停住（卷变了，卷不可用，等卷事件）、重试（SERVICE_BUSY）或读错误（文件错误，卷没变时的
 *         卷不可用，或成功但读到 0 字节）。
 * @note   调用者须在读失败之后、先等过本轮对账（对同一个句柄 `CloseFile`，它在 Filesystem 入口等闸）再取卷状态，
 *         否则卡刚拔、消抖还没结束时会误判成“卷没变”。“卷没变”指卷挂着且序号等于已处理的；序号只比相等，不比大小，
 *         所以 31 位回绕不影响。
 */
AudioReader_ReadVerdictTypeDef audio_reader_classify_read(Service_StatusTypeDef status,
                                                          uint32_t got,
                                                          bool volume_mounted,
                                                          uint32_t volume_sequence,
                                                          uint32_t handled_sequence)
{
    if (Service_Filesystem_StatusIsVolumeUnavailable(status))
    {
        const bool volume_unchanged = volume_mounted && (volume_sequence == handled_sequence);

        /* 卷变了：卡没了或正在对账，等卷事件。卷没变：卡还在、设备出错，没有边沿就没有卷事件，不能等。 */
        return volume_unchanged ? AUDIO_READER_READ_ERROR : AUDIO_READER_READ_STALL;
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
