/**
 ******************************************************************************
 * @file    audio_reader_diag.h
 * @brief   音频读取任务两行新日志的格式化（纯函数，主机可测）：`CARD_REMOVED` 时延行和 `SERVICE_BUSY` 重试行。
 *
 * @details
 *          串口里每行的前缀是 `<时间> <I|W|E> (<tick>) <标签>: `，标签是 `Audio`，标签后面有冒号；这里只写正文。
 *          文本只含数字与固定词，不含文件名（DIAG-3）。每行不超过 127 字符，放得进 Service Log 的单条正文。
 *          - `CARD_REMOVED lat=<ms>ms seq=<前>-><后> stream=<流序号>`（INFO，常开，R4.10，每次写 CARD_REMOVED 一行）：
 *            `lat` 是从卡检测的第一个边沿（卷任务记下的时刻）到流记录信箱被写成 CARD_REMOVED 并叫醒播放任务的毫秒数，
 *            在固件里算好（日志时间戳是输出时刻，不能相减）；边沿时刻不可用时写 `-`。`seq` 是读取任务已处理的卷序号
 *            与现在的卷序号（换卡跳 2）。
 *          - `SERVICE_BUSY op=<open|read> pos=<播放列表位置> n=<第几次>`（WARN，R4.12、R6.10 搜 SERVICE_BUSY）：
 *            同一次开流或读块遇到文件槽用尽，第 1 次和之后每满 100 次记一行。
 ******************************************************************************
 */

#ifndef AUDIO_READER_DIAG_H
#define AUDIO_READER_DIAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

size_t audio_reader_format_removed(char *buffer,
                                   size_t size,
                                   bool latency_known,
                                   uint32_t latency_ms,
                                   uint32_t sequence_from,
                                   uint32_t sequence_to,
                                   uint32_t stream_seq);
size_t audio_reader_format_busy(char *buffer, size_t size, const char *operation, uint32_t position, uint32_t retries);

#endif /* AUDIO_READER_DIAG_H */
