/**
 ******************************************************************************
 * @file    storage_audio_result.c
 * @brief   音频流对文件调用结果的分类（纯函数，见 storage_audio_result.h）。
 ******************************************************************************
 */

#include "APP/tasks/storage/stream/storage_audio_result.h"

#include "Service/filesystem/filesystem_types.h"

/**
 * @brief  把一次读块的结果分类。
 * @param[in] status `Service_Filesystem_ReadFile` 的返回值。
 * @param[in] got 实际读到的字节数。
 * @param[in] volume_unchanged SD 卷仍挂着且卷序号与 Storage 已处理的相同。调用者须在读失败之后、先等过本轮对账
 *            （例如对同一个句柄 `CloseFile`，它在 Filesystem 入口等闸）再取这个值，否则卡刚拔、消抖还没结束时会误判。
 * @return 继续（块照常入队）、停住（卷变了，卷不可用，等卷事件）或读错误（文件错误，卷没变时的卷不可用，
 *         或成功但读到 0 字节）。
 */
Storage_AudioReadVerdictTypeDef storage_audio_classify_read(Service_StatusTypeDef status, uint32_t got, bool volume_unchanged)
{
    if (Service_Filesystem_StatusIsVolumeUnavailable(status))
    {
        /* 卷变了：卡没了或正在对账，等卷事件。卷没变：卡还在、设备出错，没有边沿就没有卷事件，不能等。 */
        return volume_unchanged ? STORAGE_AUDIO_READ_ERROR : STORAGE_AUDIO_READ_STALL;
    }

    return ((status == SERVICE_OK) && (got != 0U)) ? STORAGE_AUDIO_READ_CONTINUE : STORAGE_AUDIO_READ_ERROR;
}

/**
 * @brief  把开流或探测的 Service 状态收成开启结果。
 * @param[in] status 打开、取长度、定位或读取的 Service 状态。
 * @param[in] open_failure true 表示打开文件本身失败（无法区分缺失与损坏，按坏文件）。
 * @return 开启结果码：卷不可用与无文件系统为 `NOT_READY`，超时为 `IO_ERROR`，参数非法为 `BAD_FILE`，
 *         其余打开失败为 `BAD_FILE`、探测失败为 `IO_ERROR`。
 */
Storage_AudioOpenResultTypeDef storage_audio_classify_open_status(Service_StatusTypeDef status, bool open_failure)
{
    if (Service_Filesystem_StatusIsVolumeUnavailable(status))
    {
        return STORAGE_AUDIO_OPEN_NOT_READY; /* NO_MEDIA / NOT_READY / INVALID_HANDLE 不是文件错误（#195 第 9 条）。 */
    }

    switch (status)
    {
        case SERVICE_NO_FILESYSTEM:
            return STORAGE_AUDIO_OPEN_NOT_READY;
        case SERVICE_TIMEOUT:
            return STORAGE_AUDIO_OPEN_IO_ERROR;
        case SERVICE_INVALID_PARAM:
            return STORAGE_AUDIO_OPEN_BAD_FILE;
        default:
            return open_failure ? STORAGE_AUDIO_OPEN_BAD_FILE : STORAGE_AUDIO_OPEN_IO_ERROR;
    }
}
