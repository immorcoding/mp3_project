/**
 ******************************************************************************
 * @file    test_storage_audio_result.c
 * @brief   音频流对文件调用结果的分类（纯函数）：读块结果映射为继续、停住或读错误，开流结果映射为开启结果码（#211）。
 *
 * @details
 *          核心是 #195 第 9 条的失败约定：`NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 不是文件错误——读块得到它们时
 *          流“停住”（不写 `READ_ERROR`、不对同一个句柄重试），等卷事件写 `CARD_REMOVED`；开流得到它们按 `NOT_READY`
 *          应答。其他错误表示卡还在，立刻按原来的方式报错。期望值来自 #195 与 #202 D 节，不用代码里的表达式重算。
 ******************************************************************************
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "APP/tasks/storage/stream/storage_audio_result.h"

/**
 * @brief 读块成功且读到了字节：继续。读到 0 字节（文件在预期长度前结束）仍是读错误，与原行为一致。
 */
static void test_successful_read_continues_and_empty_read_is_an_error(void)
{
    assert(storage_audio_classify_read(SERVICE_OK, 32768U) == STORAGE_AUDIO_READ_CONTINUE);
    assert(storage_audio_classify_read(SERVICE_OK, 1U) == STORAGE_AUDIO_READ_CONTINUE);
    assert(storage_audio_classify_read(SERVICE_OK, 0U) == STORAGE_AUDIO_READ_ERROR);
}

/**
 * @brief 卷不可用的三种返回值不是文件错误：流停住，不管读到了几个字节。
 */
static void test_volume_unavailable_statuses_stall_the_stream_instead_of_failing_it(void)
{
    static const Service_StatusTypeDef unavailable[] = {SERVICE_NO_MEDIA, SERVICE_NOT_READY, SERVICE_INVALID_HANDLE};

    for (unsigned index = 0U; index < sizeof(unavailable) / sizeof(unavailable[0]); index++)
    {
        assert(storage_audio_classify_read(unavailable[index], 0U) == STORAGE_AUDIO_READ_STALL);
        assert(storage_audio_classify_read(unavailable[index], 4096U) == STORAGE_AUDIO_READ_STALL);
    }
}

/**
 * @brief 其他错误表示卡还在：立刻报读错误，不等卷事件（卡还在且没有边沿时不会有卷事件，一直等会悬空）。
 */
static void test_other_errors_are_read_errors_because_the_card_is_still_there(void)
{
    static const Service_StatusTypeDef errors[] = {SERVICE_ERROR,         SERVICE_TIMEOUT,       SERVICE_BUSY,
                                                   SERVICE_INVALID_PARAM, SERVICE_NO_FILESYSTEM, SERVICE_NO_SPACE};

    for (unsigned index = 0U; index < sizeof(errors) / sizeof(errors[0]); index++)
    {
        assert(storage_audio_classify_read(errors[index], 0U) == STORAGE_AUDIO_READ_ERROR);
        assert(storage_audio_classify_read(errors[index], 100U) == STORAGE_AUDIO_READ_ERROR);
    }
}

/**
 * @brief 开流、探测得到卷不可用：按 NOT_READY 应答（不是 BAD_FILE，也不是 IO_ERROR），打开本身失败和探测失败都一样。
 */
static void test_volume_unavailable_statuses_answer_not_ready_when_opening(void)
{
    static const Service_StatusTypeDef unavailable[] = {SERVICE_NO_MEDIA, SERVICE_NOT_READY, SERVICE_INVALID_HANDLE};

    for (unsigned index = 0U; index < sizeof(unavailable) / sizeof(unavailable[0]); index++)
    {
        assert(storage_audio_classify_open_status(unavailable[index], true) == STORAGE_AUDIO_OPEN_NOT_READY);
        assert(storage_audio_classify_open_status(unavailable[index], false) == STORAGE_AUDIO_OPEN_NOT_READY);
    }
    assert(storage_audio_classify_open_status(SERVICE_NO_FILESYSTEM, true) == STORAGE_AUDIO_OPEN_NOT_READY);
}

/**
 * @brief 开流的其他结果沿用原来的映射：超时是 IO_ERROR，参数非法是 BAD_FILE，其余的打开失败按坏文件、探测失败按 IO_ERROR。
 */
static void test_other_open_statuses_keep_their_existing_mapping(void)
{
    assert(storage_audio_classify_open_status(SERVICE_TIMEOUT, true) == STORAGE_AUDIO_OPEN_IO_ERROR);
    assert(storage_audio_classify_open_status(SERVICE_TIMEOUT, false) == STORAGE_AUDIO_OPEN_IO_ERROR);
    assert(storage_audio_classify_open_status(SERVICE_INVALID_PARAM, true) == STORAGE_AUDIO_OPEN_BAD_FILE);
    assert(storage_audio_classify_open_status(SERVICE_INVALID_PARAM, false) == STORAGE_AUDIO_OPEN_BAD_FILE);
    assert(storage_audio_classify_open_status(SERVICE_ERROR, true) == STORAGE_AUDIO_OPEN_BAD_FILE);
    assert(storage_audio_classify_open_status(SERVICE_ERROR, false) == STORAGE_AUDIO_OPEN_IO_ERROR);
    assert(storage_audio_classify_open_status(SERVICE_BUSY, true) == STORAGE_AUDIO_OPEN_BAD_FILE);
    assert(storage_audio_classify_open_status(SERVICE_BUSY, false) == STORAGE_AUDIO_OPEN_IO_ERROR);
}

/**
 * @brief 依次运行用例。
 * @return 全部通过时为 0，断言失败时进程中止。
 */
int main(void)
{
    test_successful_read_continues_and_empty_read_is_an_error();
    test_volume_unavailable_statuses_stall_the_stream_instead_of_failing_it();
    test_other_errors_are_read_errors_because_the_card_is_still_there();
    test_volume_unavailable_statuses_answer_not_ready_when_opening();
    test_other_open_statuses_keep_their_existing_mapping();
    puts("storage_audio_result: all passed");
    return 0;
}
