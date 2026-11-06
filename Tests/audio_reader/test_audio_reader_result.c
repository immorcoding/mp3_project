/**
 ******************************************************************************
 * @file    test_audio_reader_result.c
 * @brief   “失败结果 + 卷序号 → 写什么停止原因”（纯函数）：读块结果映射为继续、停住、重试或读错误，
 *          开流结果映射为开启结果码，卷序号变了时哪些流要写 CARD_REMOVED，SERVICE_BUSY 的重试与日志节拍，
 *          两行新日志的格式（#215，#195 第 9 条，#194 第 9、11、13 条）。
 *
 * @details
 *          核心是失败约定：卷变了（卡没了或正在对账）时 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 不是文件错误——
 *          读块得到它们时流“停住”（不写 `READ_ERROR`、不对同一个句柄重试），等卷事件写 `CARD_REMOVED`；
 *          卷没变而读块得到它们（卡还在、设备出错）则没有卷事件可等，立刻按读错误报；开流得到它们按 `NOT_READY` 应答。
 *          其他错误表示卡还在，立刻按原来的方式报错。`SERVICE_BUSY` 不算文件错误，过约 10 ms 重试。
 *          期望值来自 #195、#194 与 #202 D 节，不用代码里的表达式重算。
 ******************************************************************************
 */

#include "host_test_assert.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "APP/tasks/audio_reader/audio_reader_diag.h"
#include "APP/tasks/audio_reader/audio_reader_result.h"

/** @brief 卷挂着且序号等于已处理的序号（卷没变）。 */
#define UNCHANGED true, 7U, 7U

/**
 * @brief 读块成功且读到了字节：继续，卷变没变都一样。读到 0 字节（文件在预期长度前结束）仍是读错误。
 */
static void test_successful_read_continues_and_empty_read_is_an_error(void)
{
    assert(audio_reader_classify_read(SERVICE_OK, 32768U, UNCHANGED) == AUDIO_READER_READ_CONTINUE);
    assert(audio_reader_classify_read(SERVICE_OK, 32768U, true, 8U, 7U) == AUDIO_READER_READ_CONTINUE);
    assert(audio_reader_classify_read(SERVICE_OK, 1U, UNCHANGED) == AUDIO_READER_READ_CONTINUE);
    assert(audio_reader_classify_read(SERVICE_OK, 0U, UNCHANGED) == AUDIO_READER_READ_ERROR);
    assert(audio_reader_classify_read(SERVICE_OK, 0U, false, 8U, 7U) == AUDIO_READER_READ_ERROR);
}

/**
 * @brief 卷不可用的三种返回值不是文件错误，**前提是卷确实变了**：流停住，等卷事件写 CARD_REMOVED，不管读到了几个字节。
 *        “变了”有两种：卷序号和已处理的不同（卸载、换卡、被吞的拔出加重新挂载），或卷已不挂。
 */
static void test_volume_unavailable_statuses_stall_the_stream_while_the_volume_has_changed(void)
{
    static const Service_StatusTypeDef unavailable[] = {SERVICE_NO_MEDIA, SERVICE_NOT_READY, SERVICE_INVALID_HANDLE};

    for (unsigned index = 0U; index < sizeof(unavailable) / sizeof(unavailable[0]); index++)
    {
        assert(audio_reader_classify_read(unavailable[index], 0U, true, 8U, 7U) == AUDIO_READER_READ_STALL);   /* 卸载：序号加 1。 */
        assert(audio_reader_classify_read(unavailable[index], 4096U, true, 9U, 7U) == AUDIO_READER_READ_STALL); /* 换卡：序号跳 2。 */
        assert(audio_reader_classify_read(unavailable[index], 0U, false, 7U, 7U) == AUDIO_READER_READ_STALL);   /* 卷已不挂，序号还没来得及比。 */
        assert(audio_reader_classify_read(unavailable[index], 0U, false, 8U, 7U) == AUDIO_READER_READ_STALL);
    }
}

/**
 * @brief 卷没变（挂着且序号相同）时，同样的三种返回值说明卡还在、设备出了错：没有边沿就不会有卷事件，
 *        一直等会静音到 T_data（5 s），所以立刻按读错误报。
 */
static void test_volume_unavailable_statuses_with_an_unchanged_volume_are_read_errors(void)
{
    static const Service_StatusTypeDef unavailable[] = {SERVICE_NO_MEDIA, SERVICE_NOT_READY, SERVICE_INVALID_HANDLE};

    for (unsigned index = 0U; index < sizeof(unavailable) / sizeof(unavailable[0]); index++)
    {
        assert(audio_reader_classify_read(unavailable[index], 0U, UNCHANGED) == AUDIO_READER_READ_ERROR);
        assert(audio_reader_classify_read(unavailable[index], 4096U, UNCHANGED) == AUDIO_READER_READ_ERROR);
    }
}

/**
 * @brief 卷序号在 31 位回绕处照样按“不相同”比较，不用大小比较：从最大值回到 0 也是变了。
 */
static void test_sequence_comparison_is_equality_not_ordering(void)
{
    assert(audio_reader_classify_read(SERVICE_NO_MEDIA, 0U, true, 0U, 0x7FFFFFFFU) == AUDIO_READER_READ_STALL);
    assert(audio_reader_classify_read(SERVICE_NO_MEDIA, 0U, true, 0x7FFFFFFFU, 0x7FFFFFFFU) == AUDIO_READER_READ_ERROR);
}

/**
 * @brief 其他错误表示卡还在：立刻报读错误，卷变没变都一样；SERVICE_BUSY 例外，它是文件槽暂时用尽，不算文件错误，要重试。
 */
static void test_other_errors_are_read_errors_but_busy_is_retried(void)
{
    static const Service_StatusTypeDef errors[] = {SERVICE_ERROR,         SERVICE_TIMEOUT,    SERVICE_INVALID_PARAM,
                                                   SERVICE_NO_FILESYSTEM, SERVICE_NO_SPACE};

    for (unsigned index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        assert(audio_reader_classify_read(errors[index], 0U, UNCHANGED) == AUDIO_READER_READ_ERROR);
        assert(audio_reader_classify_read(errors[index], 100U, false, 8U, 7U) == AUDIO_READER_READ_ERROR);
    }
    assert(audio_reader_classify_read(SERVICE_BUSY, 0U, UNCHANGED) == AUDIO_READER_READ_RETRY);
    assert(audio_reader_classify_read(SERVICE_BUSY, 0U, false, 8U, 7U) == AUDIO_READER_READ_RETRY);
}

/**
 * @brief 开流、探测得到卷不可用：按 NOT_READY 应答（不是 BAD_FILE，也不是 IO_ERROR），打开本身失败和探测失败都一样。
 */
static void test_volume_unavailable_statuses_answer_not_ready_when_opening(void)
{
    static const Service_StatusTypeDef unavailable[] = {SERVICE_NO_MEDIA, SERVICE_NOT_READY, SERVICE_INVALID_HANDLE};

    for (unsigned index = 0U; index < sizeof(unavailable) / sizeof(unavailable[0]); index++)
    {
        assert(audio_reader_classify_open_status(unavailable[index], true) == AUDIO_READER_OPEN_NOT_READY);
        assert(audio_reader_classify_open_status(unavailable[index], false) == AUDIO_READER_OPEN_NOT_READY);
    }
    assert(audio_reader_classify_open_status(SERVICE_NO_FILESYSTEM, true) == AUDIO_READER_OPEN_NOT_READY);
}

/**
 * @brief 开流的其他结果沿用原来的映射：超时是 IO_ERROR，参数非法是 BAD_FILE，其余的打开失败按坏文件、探测失败按 IO_ERROR。
 *        SERVICE_BUSY 不再走这里（要重试），但分类函数本身保持原映射，万一重试用尽或有人直接调用时不会得到新值。
 */
static void test_other_open_statuses_keep_their_existing_mapping(void)
{
    assert(audio_reader_classify_open_status(SERVICE_TIMEOUT, true) == AUDIO_READER_OPEN_IO_ERROR);
    assert(audio_reader_classify_open_status(SERVICE_TIMEOUT, false) == AUDIO_READER_OPEN_IO_ERROR);
    assert(audio_reader_classify_open_status(SERVICE_INVALID_PARAM, true) == AUDIO_READER_OPEN_BAD_FILE);
    assert(audio_reader_classify_open_status(SERVICE_INVALID_PARAM, false) == AUDIO_READER_OPEN_BAD_FILE);
    assert(audio_reader_classify_open_status(SERVICE_ERROR, true) == AUDIO_READER_OPEN_BAD_FILE);
    assert(audio_reader_classify_open_status(SERVICE_ERROR, false) == AUDIO_READER_OPEN_IO_ERROR);
    assert(audio_reader_classify_open_status(SERVICE_BUSY, true) == AUDIO_READER_OPEN_BAD_FILE);
    assert(audio_reader_classify_open_status(SERVICE_BUSY, false) == AUDIO_READER_OPEN_IO_ERROR);
}

/**
 * @brief 只有 SERVICE_BUSY 过约 10 ms 重试（文件槽暂时用尽，T_data 兜底）；卷不可用和别的错误都不重试。
 *        这是 #194 对第 13 条的修正：BUSY 不映射成 IO_ERROR，更不映射成 BAD_FILE（好歌不会被当坏文件跳过）。
 */
static void test_only_busy_is_retried(void)
{
    assert(audio_reader_status_is_busy(SERVICE_BUSY));
    assert(!audio_reader_status_is_busy(SERVICE_OK));
    assert(!audio_reader_status_is_busy(SERVICE_ERROR));
    assert(!audio_reader_status_is_busy(SERVICE_NO_MEDIA));
    assert(!audio_reader_status_is_busy(SERVICE_NOT_READY));
    assert(!audio_reader_status_is_busy(SERVICE_INVALID_HANDLE));
    assert(!audio_reader_status_is_busy(SERVICE_TIMEOUT));
    assert(!audio_reader_status_is_busy(SERVICE_NO_SPACE));
}

/**
 * @brief 构造一份流记录。
 * @param[in] stream_seq 流序号。
 * @param[in] open 开启结果。
 * @param[in] end 终止状态。
 * @return 其余字段为 0 的记录。
 */
static AudioReader_RecordTypeDef record_of(uint32_t stream_seq, AudioReader_OpenResultTypeDef open, AudioReader_EndTypeDef end)
{
    AudioReader_RecordTypeDef record;

    memset(&record, 0, sizeof(record));
    record.StreamSeq = stream_seq;
    record.OpenResult = open;
    record.End = end;
    return record;
}

/**
 * @brief “当前流仍被播放任务持有”：已成功开启，且没有以读错误或拔卡终止；读尽（ENDED）的流也算——
 *        文件已读完、播放任务还在放内存里的数据时拔卡同样要立即停止。
 */
static void test_a_stream_is_held_until_it_ends_in_error_or_card_removal(void)
{
    assert(audio_reader_stream_is_held(&(AudioReader_RecordTypeDef){7U, AUDIO_READER_OPEN_OK, 1U, 3U, 100U, AUDIO_READER_END_NONE}));
    assert(audio_reader_stream_is_held(&(AudioReader_RecordTypeDef){7U, AUDIO_READER_OPEN_OK, 1U, 3U, 100U, AUDIO_READER_END_ENDED}));

    assert(!audio_reader_stream_is_held(&(AudioReader_RecordTypeDef){7U, AUDIO_READER_OPEN_OK, 1U, 3U, 100U, AUDIO_READER_END_READ_ERROR}));
    assert(!audio_reader_stream_is_held(&(AudioReader_RecordTypeDef){7U, AUDIO_READER_OPEN_OK, 1U, 3U, 100U, AUDIO_READER_END_CARD_REMOVED}));
    assert(!audio_reader_stream_is_held(&(AudioReader_RecordTypeDef){0U, AUDIO_READER_OPEN_OK, 1U, 3U, 100U, AUDIO_READER_END_NONE})); /* 无流。 */
    assert(!audio_reader_stream_is_held(&(AudioReader_RecordTypeDef){7U, AUDIO_READER_OPEN_PENDING, 0U, 0U, 0U, AUDIO_READER_END_NONE}));
    assert(!audio_reader_stream_is_held(&(AudioReader_RecordTypeDef){7U, AUDIO_READER_OPEN_NOT_READY, 0U, 0U, 0U, AUDIO_READER_END_NONE}));
    assert(!audio_reader_stream_is_held(&(AudioReader_RecordTypeDef){7U, AUDIO_READER_OPEN_BAD_FILE, 1U, 3U, 0U, AUDIO_READER_END_NONE}));
}

/**
 * @brief 卷序号变了且流仍被播放任务持有：写 CARD_REMOVED（含读尽的流、换卡序号跳 2）；序号没变、流不被持有、
 *        已经以读错误终止的，都不写，返回 NONE。
 */
static void test_card_removed_is_written_only_when_the_sequence_changed_and_the_stream_is_held(void)
{
    const AudioReader_RecordTypeDef playing = record_of(7U, AUDIO_READER_OPEN_OK, AUDIO_READER_END_NONE);
    const AudioReader_RecordTypeDef ended = record_of(7U, AUDIO_READER_OPEN_OK, AUDIO_READER_END_ENDED);
    const AudioReader_RecordTypeDef errored = record_of(7U, AUDIO_READER_OPEN_OK, AUDIO_READER_END_READ_ERROR);
    const AudioReader_RecordTypeDef answered_not_ready = record_of(7U, AUDIO_READER_OPEN_NOT_READY, AUDIO_READER_END_NONE);
    const AudioReader_RecordTypeDef no_stream = record_of(0U, AUDIO_READER_OPEN_PENDING, AUDIO_READER_END_NONE);

    assert(audio_reader_end_on_volume_change(&playing, 8U, 7U) == AUDIO_READER_END_CARD_REMOVED);    /* 拔卡。 */
    assert(audio_reader_end_on_volume_change(&playing, 9U, 7U) == AUDIO_READER_END_CARD_REMOVED);    /* 换卡，序号跳 2。 */
    assert(audio_reader_end_on_volume_change(&ended, 8U, 7U) == AUDIO_READER_END_CARD_REMOVED);      /* 已读完的流拔卡也立即停止。 */
    assert(audio_reader_end_on_volume_change(&playing, 0U, 0x7FFFFFFFU) == AUDIO_READER_END_CARD_REMOVED); /* 回绕。 */

    assert(audio_reader_end_on_volume_change(&playing, 7U, 7U) == AUDIO_READER_END_NONE);            /* 序号没变（别的卷的事件叫醒）。 */
    assert(audio_reader_end_on_volume_change(&errored, 8U, 7U) == AUDIO_READER_END_NONE);            /* 已经以读错误终止，不覆盖。 */
    assert(audio_reader_end_on_volume_change(&answered_not_ready, 8U, 7U) == AUDIO_READER_END_NONE); /* 没有成功开启的流。 */
    assert(audio_reader_end_on_volume_change(&no_stream, 8U, 7U) == AUDIO_READER_END_NONE);
}

/**
 * @brief 日志节拍：同一次开流或读块遇到 SERVICE_BUSY，第 1 次记一行，之后每满 100 次再记一行（约 1 s），其余不记。
 *        T_data 5 s 内最多 6 行，不刷屏（DIAG-3）。
 */
static void test_busy_log_is_rate_limited(void)
{
    assert(!audio_reader_busy_log_due(0U));
    assert(audio_reader_busy_log_due(1U));
    for (uint32_t retries = 2U; retries < 100U; retries++)
    {
        assert(!audio_reader_busy_log_due(retries));
    }
    assert(audio_reader_busy_log_due(100U));
    assert(!audio_reader_busy_log_due(101U));
    assert(!audio_reader_busy_log_due(199U));
    assert(audio_reader_busy_log_due(200U));

    uint32_t lines = 0U;
    for (uint32_t retries = 1U; retries <= 500U; retries++) /* 500 次约 5 s。 */
    {
        lines += audio_reader_busy_log_due(retries) ? 1U : 0U;
    }
    assert(lines == 6U);
}

/**
 * @brief CARD_REMOVED 时延行（常开，R4.10）：串口里标签后有冒号，正文固定格式，时延已在固件里算好（相对卡检测的第一个边沿）。
 */
static void test_card_removed_line_has_the_documented_format(void)
{
    char line[128];

    audio_reader_format_removed(line, sizeof(line), true, 36U, 5U, 6U, 17U);
    assert(strcmp(line, "CARD_REMOVED lat=36ms seq=5->6 stream=17") == 0);

    audio_reader_format_removed(line, sizeof(line), true, 36U, 5U, 7U, 17U); /* 换卡：序号跳 2。 */
    assert(strcmp(line, "CARD_REMOVED lat=36ms seq=5->7 stream=17") == 0);

    audio_reader_format_removed(line, sizeof(line), false, 0U, 5U, 6U, 17U); /* 边沿时刻不可用：写 -，不编数字。 */
    assert(strcmp(line, "CARD_REMOVED lat=- seq=5->6 stream=17") == 0);

    audio_reader_format_removed(line, sizeof(line), true, 123456U, 0x7FFFFFFFU, 0x7FFFFFFFU, 0xFFFFFFFFU); /* 数字字段截在 99999，整行不超过 127 字符。 */
    assert(strcmp(line, "CARD_REMOVED lat=99999ms seq=2147483647->2147483647 stream=4294967295") == 0);
    assert(strlen(line) <= 127U);
}

/**
 * @brief SERVICE_BUSY 日志行：WARN，固定以 SERVICE_BUSY 开头（R4.12、R6.10 搜这个词），带操作、播放列表位置和第几次。
 *        不含文件名（DIAG-3）。
 */
static void test_busy_line_has_the_documented_format(void)
{
    char line[128];

    audio_reader_format_busy(line, sizeof(line), "open", 12U, 1U);
    assert(strcmp(line, "SERVICE_BUSY op=open pos=12 n=1") == 0);

    audio_reader_format_busy(line, sizeof(line), "read", 11999U, 300U);
    assert(strcmp(line, "SERVICE_BUSY op=read pos=11999 n=300") == 0);

    audio_reader_format_busy(line, sizeof(line), "open", 65535U, 0xFFFFFFFFU);
    assert(strcmp(line, "SERVICE_BUSY op=open pos=65535 n=4294967295") == 0);
    assert(strlen(line) <= 127U);
}

/**
 * @brief 缓冲放不下时安全截断：不越界，总是 NUL 结尾，返回写入的字符数。
 */
static void test_formatters_truncate_safely(void)
{
    char tiny[8];
    size_t written;

    written = audio_reader_format_removed(tiny, sizeof(tiny), true, 36U, 5U, 6U, 17U);
    assert(written < sizeof(tiny));
    assert(tiny[sizeof(tiny) - 1U] == '\0');
    assert(strlen(tiny) == written);

    written = audio_reader_format_busy(tiny, sizeof(tiny), "open", 12U, 1U);
    assert(written < sizeof(tiny));
    assert(strlen(tiny) == written);

    assert(audio_reader_format_busy(NULL, 0U, "open", 12U, 1U) == 0U);
}

/**
 * @brief 依次运行用例。
 * @return 全部通过时为 0，断言失败时进程中止。
 */
int main(void)
{
    test_successful_read_continues_and_empty_read_is_an_error();
    test_volume_unavailable_statuses_stall_the_stream_while_the_volume_has_changed();
    test_volume_unavailable_statuses_with_an_unchanged_volume_are_read_errors();
    test_sequence_comparison_is_equality_not_ordering();
    test_other_errors_are_read_errors_but_busy_is_retried();
    test_volume_unavailable_statuses_answer_not_ready_when_opening();
    test_other_open_statuses_keep_their_existing_mapping();
    test_only_busy_is_retried();
    test_a_stream_is_held_until_it_ends_in_error_or_card_removal();
    test_card_removed_is_written_only_when_the_sequence_changed_and_the_stream_is_held();
    test_busy_log_is_rate_limited();
    test_card_removed_line_has_the_documented_format();
    test_busy_line_has_the_documented_format();
    test_formatters_truncate_safely();
    puts("audio_reader_result: all passed");
    return 0;
}
