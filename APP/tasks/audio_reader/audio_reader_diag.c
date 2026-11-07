/**
 ******************************************************************************
 * @file    audio_reader_diag.c
 * @brief   音频读取任务两行新日志的格式化（见 audio_reader_diag.h）。
 ******************************************************************************
 */

#include "APP/tasks/audio_reader/audio_reader_diag.h"

#include <stdio.h>

/** @brief 时延字段的上限；超过写这个数，没有意义的大数不占行宽。 */
#define AUDIO_READER_DIAG_LATENCY_MAX_MS 99999UL

/**
 * @brief  把 snprintf 的返回值收成“写入的字符数”，并保证缓冲 NUL 结尾。
 * @param[in] written snprintf 的返回值。
 * @param[in] size 缓冲长度（大于 0）。
 * @return 写入的字符数（不含 NUL）；放不下时是被截断后的长度。
 */
static size_t audio_reader_diag_finish(int written, size_t size)
{
    if (written < 0)
    {
        return 0U;
    }
    return ((size_t)written >= size) ? (size - 1U) : (size_t)written;
}

/**
 * @brief  格式化 `CARD_REMOVED` 时延行的正文。
 * @param[out] buffer 输出缓冲；为空或长度为 0 时什么都不写。
 * @param[in] size 缓冲长度。
 * @param[in] latency_known 边沿时刻可用为真；不可用时 `lat` 写 `-`。
 * @param[in] latency_ms 从卡检测第一个边沿到写 CARD_REMOVED 的毫秒数，超过 99999 写 99999。
 * @param[in] sequence_from 读取任务已处理的卷序号。
 * @param[in] sequence_to 现在的卷序号。
 * @param[in] stream_seq 被终止的流序号。
 * @return 写入的字符数（不含 NUL）。
 */
size_t audio_reader_format_removed(char *buffer,
                                   size_t size,
                                   bool latency_known,
                                   uint32_t latency_ms,
                                   uint32_t sequence_from,
                                   uint32_t sequence_to,
                                   uint32_t stream_seq)
{
    int written;

    if ((buffer == NULL) || (size == 0U))
    {
        return 0U;
    }

    if (latency_known)
    {
        written = snprintf(buffer,
                           size,
                           "CARD_REMOVED lat=%lums seq=%lu->%lu stream=%lu",
                           (unsigned long)((latency_ms > AUDIO_READER_DIAG_LATENCY_MAX_MS) ? AUDIO_READER_DIAG_LATENCY_MAX_MS
                                                                                           : latency_ms),
                           (unsigned long)sequence_from,
                           (unsigned long)sequence_to,
                           (unsigned long)stream_seq);
    }
    else
    {
        written = snprintf(buffer,
                           size,
                           "CARD_REMOVED lat=- seq=%lu->%lu stream=%lu",
                           (unsigned long)sequence_from,
                           (unsigned long)sequence_to,
                           (unsigned long)stream_seq);
    }
    return audio_reader_diag_finish(written, size);
}

/**
 * @brief  格式化 `SERVICE_BUSY` 重试行的正文。
 * @param[out] buffer 输出缓冲；为空或长度为 0 时什么都不写。
 * @param[in] size 缓冲长度。
 * @param[in] operation 操作名，`open` 或 `read`（静态字符串）。
 * @param[in] position 播放列表位置。
 * @param[in] retries 同一次开流或读块遇到 SERVICE_BUSY 的次数，含这一次。
 * @return 写入的字符数（不含 NUL）。
 */
size_t audio_reader_format_busy(char *buffer, size_t size, const char *operation, uint32_t position, uint32_t retries)
{
    int written;

    if ((buffer == NULL) || (size == 0U))
    {
        return 0U;
    }

    written = snprintf(buffer,
                       size,
                       "SERVICE_BUSY op=%s pos=%lu n=%lu",
                       operation,
                       (unsigned long)position,
                       (unsigned long)retries);
    return audio_reader_diag_finish(written, size);
}
