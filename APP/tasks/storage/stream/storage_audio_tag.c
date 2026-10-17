/**
 * @file storage_audio_tag.c
 * @brief 越过标签纯函数的实现：ID3v2 头、尾部 ID3v1 与 APEv2。
 */

#include "storage_audio_tag.h"

#include <string.h>

#define AUDIO_TAG_ID3V1_BYTES        128U
#define AUDIO_TAG_ID3V2_HEADER_BYTES 10U
#define AUDIO_TAG_ID3V2_FOOTER_BYTES 10U
#define AUDIO_TAG_ID3V2_FLAG_FOOTER  0x10U
#define AUDIO_TAG_APE_FOOTER_BYTES   32U
#define AUDIO_TAG_APE_FLAG_HAS_HEADER 0x80000000UL
#define AUDIO_TAG_APE_FLAG_IS_HEADER  0x20000000UL
#define AUDIO_TAG_APE_VERSION_2       2000UL

/**
 * @brief 按小端读 32 位。
 * @param[in] p 至少 4 字节。
 * @return 数值。
 */
static uint32_t audio_tag_read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
 * @brief 解析头部 ID3v2，得到音频起点。
 * @param[in] head 文件前 head_bytes 字节。
 * @param[in] head_bytes 实际提供的头字节数。
 * @param[out] start 无标签为 0，否则为标签总长（头 + 体 + 可选 footer）。
 * @return false 表示 "ID3" 魔数存在但头非法。
 * @note  扩展头在声明的体长度内，不额外加长。
 */
static bool audio_tag_parse_id3v2(const uint8_t *head, uint32_t head_bytes, uint64_t *start)
{
    uint32_t i;
    uint64_t body = 0U;

    *start = 0U;
    if ((head_bytes < 3U) || (memcmp(head, "ID3", 3U) != 0))
    {
        return true;
    }
    if (head_bytes < AUDIO_TAG_ID3V2_HEADER_BYTES)
    {
        return false;
    }
    for (i = 6U; i < 10U; i++)
    {
        if ((head[i] & 0x80U) != 0U)
        {
            return false;
        }
        body = (body << 7) | head[i];
    }
    *start = AUDIO_TAG_ID3V2_HEADER_BYTES + body;
    if ((head[5] & AUDIO_TAG_ID3V2_FLAG_FOOTER) != 0U)
    {
        *start += AUDIO_TAG_ID3V2_FOOTER_BYTES;
    }
    return true;
}

/**
 * @brief 解析尾部 ID3v1 与 APEv2，得到音频终点。
 * @param[in] tail 文件末 tail_bytes 字节。
 * @param[in] tail_bytes 实际提供的尾字节数。
 * @param[in] file_size 文件总长。
 * @param[out] end 音频终点。
 * @return false 表示 APE 声明长度非法或越界。
 */
static bool audio_tag_parse_trailer(const uint8_t *tail,
                                    uint32_t tail_bytes,
                                    uint32_t file_size,
                                    uint32_t *end)
{
    uint32_t cut = 0U; /* 已从文件末尾切掉的字节数 */
    const uint8_t *footer;
    uint32_t flags;
    uint32_t tag_size;
    uint64_t total;

    if ((tail_bytes >= AUDIO_TAG_ID3V1_BYTES) &&
        (memcmp(&tail[tail_bytes - AUDIO_TAG_ID3V1_BYTES], "TAG", 3U) == 0))
    {
        cut = AUDIO_TAG_ID3V1_BYTES;
    }

    if (tail_bytes >= (cut + AUDIO_TAG_APE_FOOTER_BYTES))
    {
        footer = &tail[tail_bytes - cut - AUDIO_TAG_APE_FOOTER_BYTES];
        if (memcmp(footer, "APETAGEX", 8U) == 0)
        {
            flags = audio_tag_read_le32(&footer[20]);
            tag_size = audio_tag_read_le32(&footer[12]);
            if ((flags & AUDIO_TAG_APE_FLAG_IS_HEADER) != 0U)
            {
                /* 末尾只可能是脚；声明自己是头的不是合法尾标签，按普通音频字节处理。 */
            }
            else
            {
                total = tag_size;
                if ((audio_tag_read_le32(&footer[8]) >= AUDIO_TAG_APE_VERSION_2) &&
                    ((flags & AUDIO_TAG_APE_FLAG_HAS_HEADER) != 0U))
                {
                    total += AUDIO_TAG_APE_FOOTER_BYTES;
                }
                if ((tag_size < AUDIO_TAG_APE_FOOTER_BYTES) || ((total + cut) > file_size))
                {
                    return false;
                }
                cut += (uint32_t)total;
            }
        }
    }

    *end = file_size - cut;
    return true;
}

/**
 * @brief 由文件头尾字节算出音频区间。
 * @param[in] head 文件前 head_bytes 字节。
 * @param[in] head_bytes min(HEAD_BYTES, file_size)。
 * @param[in] tail 文件末 tail_bytes 字节。
 * @param[in] tail_bytes min(TAIL_BYTES, file_size)。
 * @param[in] file_size 文件总长。
 * @param[out] range 成功时的 [Start, End)。
 * @retval true 区间非空且在文件内。
 * @retval false 指针/长度不符约定、标签长度越界或音频区间为空（Storage 应报 BAD_FILE）。
 */
bool storage_audio_tag_locate(const uint8_t *head,
                              uint32_t head_bytes,
                              const uint8_t *tail,
                              uint32_t tail_bytes,
                              uint32_t file_size,
                              Storage_AudioRangeTypeDef *range)
{
    uint64_t start;
    uint32_t end;
    uint32_t expect_head;
    uint32_t expect_tail;

    if ((head == NULL) || (tail == NULL) || (range == NULL) || (file_size == 0U))
    {
        return false;
    }
    expect_head = (file_size < STORAGE_AUDIO_TAG_HEAD_BYTES) ? file_size : STORAGE_AUDIO_TAG_HEAD_BYTES;
    expect_tail = (file_size < STORAGE_AUDIO_TAG_TAIL_BYTES) ? file_size : STORAGE_AUDIO_TAG_TAIL_BYTES;
    if ((head_bytes != expect_head) || (tail_bytes != expect_tail))
    {
        return false;
    }

    if (!audio_tag_parse_id3v2(head, head_bytes, &start) ||
        !audio_tag_parse_trailer(tail, tail_bytes, file_size, &end))
    {
        return false;
    }
    if (start >= end)
    {
        return false;
    }

    range->Start = (uint32_t)start;
    range->End = end;
    return true;
}
