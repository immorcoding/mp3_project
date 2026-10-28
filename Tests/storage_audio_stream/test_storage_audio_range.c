/**
 * @file test_storage_audio_range.c
 * @brief 越过标签纯函数的主机测试：只经 storage_audio_range_locate 观察音频区间。
 */

#include "APP/tasks/storage/stream/storage_audio_range.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FILE_MAX 4096U

/** @brief 测试用虚拟文件：按布局拼出字节，再按调用方约定截取头尾。 */
static uint8_t file_bytes[FILE_MAX];
static uint32_t file_size;

/**
 * @brief 写 ID3v2 头（含 synchsafe 长度）。
 * @param[in] offset 写入起点。
 * @param[in] body_size 标签体长度（含扩展头），按 synchsafe 编码。
 * @param[in] flags 头标志字节。
 */
static void put_id3v2(uint32_t offset, uint32_t body_size, uint8_t flags)
{
    file_bytes[offset + 0U] = 'I';
    file_bytes[offset + 1U] = 'D';
    file_bytes[offset + 2U] = '3';
    file_bytes[offset + 3U] = 4U;
    file_bytes[offset + 4U] = 0U;
    file_bytes[offset + 5U] = flags;
    file_bytes[offset + 6U] = (uint8_t)((body_size >> 21) & 0x7FU);
    file_bytes[offset + 7U] = (uint8_t)((body_size >> 14) & 0x7FU);
    file_bytes[offset + 8U] = (uint8_t)((body_size >> 7) & 0x7FU);
    file_bytes[offset + 9U] = (uint8_t)(body_size & 0x7FU);
}

/**
 * @brief 写 ID3v1 标记（以 "TAG" 开头，共 128 字节占位）。
 * @param[in] offset 标签起点。
 */
static void put_id3v1(uint32_t offset)
{
    memcpy(&file_bytes[offset], "TAG", 3U);
}

/**
 * @brief 写 APEv2 脚（32 字节）。
 * @param[in] offset 脚的起点。
 * @param[in] tag_size 标签大小字段（含脚、不含头）。
 * @param[in] has_header 是否置 header 存在标志。
 */
static void put_ape_footer(uint32_t offset, uint32_t tag_size, bool has_header)
{
    uint32_t flags = has_header ? 0x80000000UL : 0U;
    uint32_t i;

    memcpy(&file_bytes[offset], "APETAGEX", 8U);
    file_bytes[offset + 8U] = 0xD0U; /* 版本 2000，小端 */
    file_bytes[offset + 9U] = 0x07U;
    for (i = 0U; i < 4U; i++)
    {
        file_bytes[offset + 12U + i] = (uint8_t)(tag_size >> (8U * i));
        file_bytes[offset + 20U + i] = (uint8_t)(flags >> (8U * i));
    }
}

/**
 * @brief 以当前虚拟文件按调用方约定截取头尾后调用被测函数。
 * @param[out] range 接收区间。
 * @return 被测函数结果。
 */
static bool locate(Storage_AudioRangeTypeDef *range)
{
    uint32_t head_bytes = file_size < STORAGE_AUDIO_RANGE_HEAD_BYTES ? file_size : STORAGE_AUDIO_RANGE_HEAD_BYTES;
    uint32_t tail_bytes = file_size < STORAGE_AUDIO_RANGE_TAIL_BYTES ? file_size : STORAGE_AUDIO_RANGE_TAIL_BYTES;

    return storage_audio_range_locate(file_bytes,
                                    head_bytes,
                                    &file_bytes[file_size - tail_bytes],
                                    tail_bytes,
                                    file_size,
                                    range);
}

/**
 * @brief 清空虚拟文件并设定长度；内容用 0xFF 充当“音频”。
 * @param[in] size 文件长度。
 */
static void reset_file(uint32_t size)
{
    memset(file_bytes, 0xFF, sizeof(file_bytes));
    file_size = size;
}

/** @brief 无标签的文件整个都是音频。 */
static void plain_file_is_whole_range(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(1000U);
    assert(locate(&r));
    assert(r.Start == 0U && r.End == 1000U);
}

/** @brief ID3v2 头被越过。 */
static void id3v2_header_is_skipped(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(1000U);
    put_id3v2(0U, 300U, 0U);
    assert(locate(&r));
    assert(r.Start == 310U && r.End == 1000U);
}

/** @brief 超过 127 的 synchsafe 长度按 7 位分组解码。 */
static void id3v2_synchsafe_length_over_127_is_decoded(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(2000U);
    put_id3v2(0U, 1000U, 0U);
    assert(locate(&r));
    assert(r.Start == 1010U && r.End == 2000U);
}

/** @brief 扩展头在声明的体长度内，不额外加长。 */
static void id3v2_extended_header_is_inside_declared_size(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(1000U);
    put_id3v2(0U, 200U, 0x40U);
    assert(locate(&r));
    assert(r.Start == 210U && r.End == 1000U);
}

/** @brief footer 标志使起点多 10 字节。 */
static void id3v2_footer_adds_ten_bytes(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(1000U);
    put_id3v2(0U, 200U, 0x10U);
    assert(locate(&r));
    assert(r.Start == 220U && r.End == 1000U);
}

/** @brief synchsafe 字节最高位非零视为坏文件。 */
static void id3v2_with_invalid_synchsafe_byte_is_bad(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(1000U);
    put_id3v2(0U, 100U, 0U);
    file_bytes[8] = 0x80U;
    assert(!locate(&r));
}

/** @brief ID3v2 声明长度超出文件为坏文件。 */
static void id3v2_length_beyond_file_is_bad(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(500U);
    put_id3v2(0U, 600U, 0U);
    assert(!locate(&r));
}

/** @brief 尾部 ID3v1 被切掉。 */
static void id3v1_trailer_is_cut(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(1000U);
    put_id3v1(1000U - 128U);
    assert(locate(&r));
    assert(r.Start == 0U && r.End == 872U);
}

/** @brief 无 header 的 APEv2 只切 tag_size 字节。 */
static void ape_without_header_is_cut(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(1000U);
    put_ape_footer(1000U - 32U, 100U, false);
    assert(locate(&r));
    assert(r.Start == 0U && r.End == 900U);
}

/** @brief 带 header 的 APEv2 额外切 32 字节 header。 */
static void ape_with_header_cuts_header_too(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(1000U);
    put_ape_footer(1000U - 32U, 100U, true);
    assert(locate(&r));
    assert(r.End == 868U && r.Start == 0U);
}

/** @brief APE 位于 ID3v1 之前时两者都被切掉。 */
static void ape_before_id3v1_both_cut(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(1000U);
    put_id3v1(1000U - 128U);
    put_ape_footer(1000U - 128U - 32U, 100U, true);
    assert(locate(&r));
    assert(r.Start == 0U && r.End == 1000U - 128U - 132U);
}

/** @brief 头尾标签组合。 */
static void all_tags_combined(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(2000U);
    put_id3v2(0U, 90U, 0U);
    put_id3v1(2000U - 128U);
    put_ape_footer(2000U - 128U - 32U, 64U, false);
    assert(locate(&r));
    assert(r.Start == 100U && r.End == 2000U - 128U - 64U);
}

/** @brief APE 长度超出文件为坏文件。 */
static void ape_length_beyond_file_is_bad(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(1000U);
    put_ape_footer(1000U - 32U, 5000U, false);
    assert(!locate(&r));
}

/** @brief APE 长度小于脚本身为坏文件。 */
static void ape_length_smaller_than_footer_is_bad(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(1000U);
    put_ape_footer(1000U - 32U, 10U, false);
    assert(!locate(&r));
}

/** @brief 标签占满全文件，音频区间为空，为坏文件。 */
static void tags_covering_whole_file_leave_empty_range_and_are_bad(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(138U);
    put_id3v2(0U, 0U, 0U);
    put_id3v1(10U);
    assert(!locate(&r));
}

/** @brief 头部标签与尾部标签相交，区间为空，为坏文件。 */
static void id3v2_overlapping_trailer_is_bad(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(400U);
    put_id3v2(0U, 300U, 0U);
    put_id3v1(400U - 128U);
    assert(!locate(&r));
}

/** @brief 空文件为坏文件。 */
static void empty_file_is_bad(void)
{
    Storage_AudioRangeTypeDef r;

    assert(!storage_audio_range_locate(file_bytes, 0U, file_bytes, 0U, 0U, &r));
}

/** @brief 短于标签头的文件按无标签处理。 */
static void short_file_without_tags_is_whole_range(void)
{
    Storage_AudioRangeTypeDef r;

    reset_file(5U);
    assert(locate(&r));
    assert(r.Start == 0U && r.End == 5U);
}

/**
 * @brief 测试入口。
 * @return 0 表示全部通过；断言失败会直接终止进程。
 */
int main(void)
{
    plain_file_is_whole_range();
    id3v2_header_is_skipped();
    id3v2_synchsafe_length_over_127_is_decoded();
    id3v2_extended_header_is_inside_declared_size();
    id3v2_footer_adds_ten_bytes();
    id3v2_with_invalid_synchsafe_byte_is_bad();
    id3v2_length_beyond_file_is_bad();
    id3v1_trailer_is_cut();
    ape_without_header_is_cut();
    ape_with_header_cuts_header_too();
    ape_before_id3v1_both_cut();
    all_tags_combined();
    ape_length_beyond_file_is_bad();
    ape_length_smaller_than_footer_is_bad();
    tags_covering_whole_file_leave_empty_range_and_are_bad();
    id3v2_overlapping_trailer_is_bad();
    empty_file_is_bad();
    short_file_without_tags_is_whole_range();
    puts("storage_audio_range tests passed");
    return 0;
}
