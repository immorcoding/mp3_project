/**
 ******************************************************************************
 * @file    id3_reader.c
 * @brief   ID3v2 帧定位、文本解码与字段清理实现。
 ******************************************************************************
 */

#include "id3_reader.h"

#include <stddef.h>
#include <string.h>

#define ID3_FRAME_HEADER_V22_BYTES 6U  /* v2.2 帧头：3 字符 ID + 3 字节大小。 */
#define ID3_FRAME_HEADER_V23_BYTES 10U /* v2.3/v2.4 帧头：4 字符 ID + 4 字节大小 + 2 字节标志。 */
#define ID3_OPEN_END_UNBOUNDED     0xFFFFFFFFU /* 标签级去同步且标签超出首块：真实末尾未知。 */

#define ID3_SEPARATOR_TEXT         " / " /* 多值连写分隔符。 */
#define ID3_SEPARATOR_BYTES        3U

/** @brief 文本解码过程中产生码点的结果。 */
typedef enum
{
    ID3_CP_OK = 0U, /**< 产生了一个码点。 */
    ID3_CP_END,     /**< 文本已读完。 */
    ID3_CP_ERROR    /**< 非法序列。 */
} id3_cp_status_t;

/** @brief 码点来源：按编码字节把负载逐个转成 Unicode 码点。 */
typedef struct
{
    uint8_t encoding;              /**< 编码字节 0–3。 */
    const uint8_t *text;           /**< 文本负载。 */
    uint32_t length;               /**< 文本字节数。 */
    uint32_t pos;                  /**< 下一个未读字节。 */
    bool gbk;                      /**< 编码 0 且整体判定为 GBK。 */
    bool big_endian;               /**< 编码 1 当前值的字节序。 */
    bool value_start;              /**< 编码 1：下一个单元是值的开头，需识别 BOM。 */
    Id3Reader_GbkLookupFunc lookup; /**< GBK 查表。 */
    void *lookup_context;          /**< 查表上下文。 */
} id3_source_t;

/** @brief 字段组装状态：边接收码点边做 trim、丢空项、连写与容量截断。 */
typedef struct
{
    char *out;           /**< 输出缓冲。 */
    uint32_t capacity;   /**< 可写字节数（不含结尾 '\0'）。 */
    uint32_t length;     /**< 已写字节数。 */
    uint32_t last_end;   /**< 当前值最后一个非空白字符之后的输出位置（或值开始位置）。 */
    bool started;        /**< 当前值已出现非空白字符。 */
    bool full;           /**< 容量已耗尽，之后不再写入（保证截断处无空洞）。 */
    bool control_pending; /**< 当前值内出现了尚未确认是否位于尾部的控制类空白。 */
} id3_field_t;

/**
 * @brief 读 4 字节 syncsafe 整数。
 * @param[in] p 4 字节。
 * @param[out] value 结果。
 * @return false 表示某字节最高位非 0。
 */
static bool id3_read_syncsafe(const uint8_t *p, uint32_t *value)
{
    if (((p[0] | p[1] | p[2] | p[3]) & 0x80U) != 0U)
    {
        return false;
    }
    *value = ((uint32_t)p[0] << 21) | ((uint32_t)p[1] << 14) | ((uint32_t)p[2] << 7) | (uint32_t)p[3];
    return true;
}

/**
 * @brief 读 4 字节大端整数。
 * @param[in] p 4 字节。
 * @return 值。
 */
static uint32_t id3_read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/**
 * @brief 就地还原去同步：把每个 FF 00 还原为 FF。
 * @param[in,out] data 数据。
 * @param[in] length 字节数。
 * @return 还原后的字节数。
 */
static uint32_t id3_unsync(uint8_t *data, uint32_t length)
{
    uint32_t out = 0U;

    for (uint32_t i = 0U; i < length; i++)
    {
        data[out++] = data[i];
        if ((data[i] == 0xFFU) && ((i + 1U) < length) && (data[i + 1U] == 0U))
        {
            i++;
        }
    }
    return out;
}

/**
 * @brief 解析标签头，初始化遍历状态，必要时就地还原标签级去同步。
 * @param[out] reader 遍历状态。
 * @param[in,out] block 文件偏移 0 起的字节块；v2.2/v2.3 标签级去同步时被就地改写。
 * @param[in,out] block_length 输入块字节数；去同步还原后改为还原后的有效字节数。
 * @param[in] file_length 文件长度，用于夹紧标签末尾。
 * @return 标签头解析结果。
 * @note  标签级去同步（v2.2/v2.3 头标志 bit 7）还原后偏移不再对应文件，之后只能在这一块内解析。
 */
Id3Reader_OpenStatusTypeDef Id3Reader_Open(Id3Reader_TypeDef *reader,
                                           uint8_t *block,
                                           uint32_t *block_length,
                                           uint32_t file_length)
{
    uint32_t length = *block_length;
    uint32_t tag_size;
    uint32_t tag_end;
    uint8_t flags;
    uint8_t major;

    if ((length < ID3_READER_HEADER_BYTES) || (file_length < ID3_READER_HEADER_BYTES) ||
        (memcmp(block, "ID3", 3U) != 0))
    {
        return ID3_READER_OPEN_NO_TAG;
    }
    major = block[3];
    flags = block[5];
    if ((major < 2U) || (major > 4U) || !id3_read_syncsafe(block + 6, &tag_size) ||
        ((major == 2U) && ((flags & 0x40U) != 0U)))
    {
        return ID3_READER_OPEN_INVALID;
    }

    tag_end = ID3_READER_HEADER_BYTES + tag_size;
    if ((tag_end > file_length) || (tag_end < tag_size))
    {
        tag_end = file_length;
    }
    reader->Version = major;
    reader->SingleBlock = false;
    reader->TagEnd = tag_end;
    reader->Cursor = ID3_READER_HEADER_BYTES;

    if (((flags & 0x80U) != 0U) && (major != 4U))
    {
        uint32_t avail_end = (tag_end < length) ? tag_end : length;
        uint32_t restored = id3_unsync(block + ID3_READER_HEADER_BYTES, avail_end - ID3_READER_HEADER_BYTES);

        reader->SingleBlock = true;
        reader->TagEnd = (tag_end <= length) ? (ID3_READER_HEADER_BYTES + restored) : ID3_OPEN_END_UNBOUNDED;
        length = ID3_READER_HEADER_BYTES + restored;
        *block_length = length;
    }

    if (((flags & 0x40U) != 0U) && (major >= 3U))
    {
        uint64_t frames_start;

        if (length < (ID3_READER_HEADER_BYTES + 4U))
        {
            reader->TagEnd = ID3_READER_HEADER_BYTES;
            return ID3_READER_OPEN_OK;
        }
        if (major == 3U)
        {
            frames_start = (uint64_t)ID3_READER_HEADER_BYTES + 4U + id3_read_be32(block + ID3_READER_HEADER_BYTES);
        }
        else
        {
            uint32_t ext_size;

            if (!id3_read_syncsafe(block + ID3_READER_HEADER_BYTES, &ext_size) || (ext_size < 6U))
            {
                return ID3_READER_OPEN_INVALID;
            }
            frames_start = (uint64_t)ID3_READER_HEADER_BYTES + ext_size;
        }
        reader->Cursor = (frames_start > reader->TagEnd) ? reader->TagEnd : (uint32_t)frames_start;
    }
    return ID3_READER_OPEN_OK;
}

/**
 * @brief 判断帧 ID 的每个字符是否为 A–Z 或 0–9。
 * @param[in] id 帧 ID。
 * @param[in] length ID 字符数。
 * @return true 合法。
 */
static bool id3_frame_id_valid(const uint8_t *id, uint32_t length)
{
    for (uint32_t i = 0U; i < length; i++)
    {
        bool upper = (id[i] >= 'A') && (id[i] <= 'Z');
        bool digit = (id[i] >= '0') && (id[i] <= '9');

        if (!upper && !digit)
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief 把帧 ID 归类到关心的帧种类。
 * @param[in] version 标签主版本。
 * @param[in] id 帧 ID。
 * @param[out] kind 种类。
 * @return true 是关心的帧。
 */
static bool id3_frame_kind(uint8_t version, const uint8_t *id, Id3Reader_KindTypeDef *kind)
{
    if (version == 2U)
    {
        if (memcmp(id, "TT2", 3U) == 0)
        {
            *kind = ID3_READER_KIND_TITLE;
            return true;
        }
        if (memcmp(id, "TP1", 3U) == 0)
        {
            *kind = ID3_READER_KIND_ARTIST;
            return true;
        }
        return false;
    }
    if (memcmp(id, "TIT2", 4U) == 0)
    {
        *kind = ID3_READER_KIND_TITLE;
        return true;
    }
    if (memcmp(id, "TPE1", 4U) == 0)
    {
        *kind = ID3_READER_KIND_ARTIST;
        return true;
    }
    return false;
}

/**
 * @brief 跳过负载开头的若干字节。
 * @param[in,out] payload 负载起点。
 * @param[in,out] length 负载字节数。
 * @param[in] skip 要跳过的字节数。
 * @return false 表示负载不足。
 */
static bool id3_skip(uint8_t **payload, uint32_t *length, uint32_t skip)
{
    if (*length < skip)
    {
        return false;
    }
    *payload += skip;
    *length -= skip;
    return true;
}

/**
 * @brief 准备一个完整位于块内的目标帧：处理帧标志、还原帧级去同步、取出编码字节与文本。
 * @param[in] version 标签主版本。
 * @param[in,out] header 帧头起点（帧头标志在其中）。
 * @param[in] size 帧大小（负载字节数）。
 * @param[out] frame 输出帧。
 * @note  压缩或加密、负载为空、分组/数据长度字段放不下，都标记为不可读。
 */
static void id3_prepare_frame(uint8_t version, uint8_t *header, uint32_t size, Id3Reader_FrameTypeDef *frame)
{
    uint32_t header_bytes = (version == 2U) ? ID3_FRAME_HEADER_V22_BYTES : ID3_FRAME_HEADER_V23_BYTES;
    uint8_t *payload = header + header_bytes;
    uint32_t length = size;
    bool ok = true;

    if (version == 3U)
    {
        uint8_t flags = header[9];

        if ((flags & 0xC0U) != 0U)
        {
            ok = false;
        }
        else if ((flags & 0x20U) != 0U)
        {
            ok = id3_skip(&payload, &length, 1U);
        }
    }
    else if (version == 4U)
    {
        uint8_t flags = header[9];

        if ((flags & 0x0CU) != 0U)
        {
            ok = false;
        }
        else
        {
            if ((flags & 0x02U) != 0U)
            {
                length = id3_unsync(payload, length);
            }
            if ((flags & 0x40U) != 0U)
            {
                ok = id3_skip(&payload, &length, 1U);
            }
            if (ok && ((flags & 0x01U) != 0U))
            {
                ok = id3_skip(&payload, &length, 4U);
            }
        }
    }

    frame->Readable = ok && (length > 0U);
    if (frame->Readable)
    {
        frame->Encoding = payload[0];
        frame->Text = payload + 1;
        frame->TextLength = length - 1U;
    }
    else
    {
        frame->Encoding = 0U;
        frame->Text = NULL;
        frame->TextLength = 0U;
    }
}

/**
 * @brief 在块内逐帧向前，直到找到下一个目标帧、需要更多数据或帧序列结束。
 * @param[in,out] reader 遍历状态。
 * @param[in,out] block 覆盖 [block_offset, block_offset + block_length) 的字节块；目标帧的帧级去同步会就地还原。
 * @param[in] block_offset 块首字节的文件偏移（标签级去同步的单块模式下为 0）。
 * @param[in] block_length 块有效字节数。
 * @param[out] frame 找到的帧（返回 ID3_READER_FRAME 时有效）。
 * @param[out] next_offset 返回 ID3_READER_NEED_DATA 时，调用者应读取的起始偏移。
 * @return ID3_READER_FRAME、ID3_READER_NEED_DATA 或 ID3_READER_END。
 * @note  非目标帧只按帧头跳过，不要求负载在块内（不读 APIC 等负载）；目标帧负载不完整在块内时要求从帧头重读一块，
 *        重读后仍放不下的帧标记为不可读。
 */
Id3Reader_StatusTypeDef Id3Reader_NextFrame(Id3Reader_TypeDef *reader,
                                            uint8_t *block,
                                            uint32_t block_offset,
                                            uint32_t block_length,
                                            Id3Reader_FrameTypeDef *frame,
                                            uint32_t *next_offset)
{
    uint32_t header_bytes = (reader->Version == 2U) ? ID3_FRAME_HEADER_V22_BYTES : ID3_FRAME_HEADER_V23_BYTES;
    uint32_t id_bytes = (reader->Version == 2U) ? 3U : 4U;
    uint64_t block_end = (uint64_t)block_offset + block_length;

    for (;;)
    {
        uint32_t cursor = reader->Cursor;
        uint8_t *header;
        uint32_t size;
        uint32_t frame_end;
        Id3Reader_KindTypeDef kind;

        if ((cursor > reader->TagEnd) || ((reader->TagEnd - cursor) < header_bytes))
        {
            return ID3_READER_END;
        }
        if ((cursor < block_offset) || (((uint64_t)cursor + header_bytes) > block_end))
        {
            if (cursor == block_offset)
            {
                return ID3_READER_END; /* 块已从帧头开始仍放不下帧头：文件被截断。 */
            }
            *next_offset = cursor;
            return ID3_READER_NEED_DATA;
        }

        header = block + (cursor - block_offset);
        if (!id3_frame_id_valid(header, id_bytes))
        {
            return ID3_READER_END;
        }
        if (reader->Version == 2U)
        {
            size = ((uint32_t)header[3] << 16) | ((uint32_t)header[4] << 8) | (uint32_t)header[5];
        }
        else if (reader->Version == 3U)
        {
            size = id3_read_be32(header + 4);
        }
        else if (!id3_read_syncsafe(header + 4, &size))
        {
            return ID3_READER_END;
        }
        if (size > (reader->TagEnd - cursor - header_bytes))
        {
            return ID3_READER_END; /* 帧越过标签末尾：标签损坏，后面的字节不可信。 */
        }
        frame_end = cursor + header_bytes + size;

        if (!id3_frame_kind(reader->Version, header, &kind))
        {
            reader->Cursor = frame_end;
            continue;
        }

        frame->Kind = kind;
        if ((uint64_t)frame_end > block_end)
        {
            if (cursor != block_offset)
            {
                *next_offset = cursor;
                return ID3_READER_NEED_DATA;
            }
            frame->Readable = false; /* 从帧头重读一块仍放不下：负载大于一块。 */
            frame->Encoding = 0U;
            frame->Text = NULL;
            frame->TextLength = 0U;
        }
        else
        {
            id3_prepare_frame(reader->Version, header, size, frame);
        }
        reader->Cursor = frame_end;
        return ID3_READER_FRAME;
    }
}

/**
 * @brief 判断编码 0 文本是否整体构成合法 GBK。
 * @param[in] text 文本。
 * @param[in] length 字节数。
 * @param[in] lookup 查表回调；NULL 视为无映射。
 * @param[in] context 回调上下文。
 * @return true：含 ≥ 0x80 字节，且全部高位字节组成结构合法并有映射的双字节。
 */
static bool id3_text_is_gbk(const uint8_t *text, uint32_t length, Id3Reader_GbkLookupFunc lookup, void *context)
{
    bool has_high = false;
    uint32_t i = 0U;

    if (lookup == NULL)
    {
        return false;
    }
    while (i < length)
    {
        uint8_t lead = text[i];
        uint8_t trail;

        if (lead < 0x80U)
        {
            i++;
            continue;
        }
        has_high = true;
        if ((lead < 0x81U) || (lead > 0xFEU) || ((i + 1U) >= length))
        {
            return false;
        }
        trail = text[i + 1U];
        if ((trail < 0x40U) || (trail == 0x7FU) || (trail > 0xFEU))
        {
            return false;
        }
        if (lookup((uint16_t)(((uint16_t)lead << 8) | trail), context) == 0U)
        {
            return false;
        }
        i += 2U;
    }
    return has_high;
}

/**
 * @brief 从 UTF-16 流读一个 16 位单元。
 * @param[in] source 码点来源。
 * @return 单元值。
 */
static uint16_t id3_read_unit(const id3_source_t *source)
{
    uint8_t b0 = source->text[source->pos];
    uint8_t b1 = source->text[source->pos + 1U];

    return source->big_endian ? (uint16_t)(((uint16_t)b0 << 8) | b1) : (uint16_t)(((uint16_t)b1 << 8) | b0);
}

/**
 * @brief 从来源读下一个 UTF-16 码点。
 * @param[in,out] source 码点来源。
 * @param[out] cp 码点。
 * @return 读取结果；孤立代理为 ID3_CP_ERROR。
 */
static id3_cp_status_t id3_next_utf16(id3_source_t *source, uint32_t *cp)
{
    uint16_t unit;

    if (source->pos >= source->length)
    {
        return ID3_CP_END;
    }
    if ((source->encoding == 1U) && source->value_start)
    {
        const uint8_t *p = source->text + source->pos;

        source->value_start = false;
        source->big_endian = false;
        if ((p[0] == 0xFFU) && (p[1] == 0xFEU))
        {
            source->pos += 2U;
        }
        else if ((p[0] == 0xFEU) && (p[1] == 0xFFU))
        {
            source->big_endian = true;
            source->pos += 2U;
        }
        if (source->pos >= source->length)
        {
            return ID3_CP_END;
        }
    }

    unit = id3_read_unit(source);
    source->pos += 2U;
    if (unit == 0U)
    {
        source->value_start = true;
        *cp = 0U;
        return ID3_CP_OK;
    }
    if ((unit >= 0xD800U) && (unit <= 0xDBFFU))
    {
        uint16_t low;

        if (source->pos >= source->length)
        {
            return ID3_CP_ERROR;
        }
        low = id3_read_unit(source);
        if ((low < 0xDC00U) || (low > 0xDFFFU))
        {
            return ID3_CP_ERROR;
        }
        source->pos += 2U;
        *cp = 0x10000U + (((uint32_t)unit - 0xD800U) << 10) + ((uint32_t)low - 0xDC00U);
        return ID3_CP_OK;
    }
    if ((unit >= 0xDC00U) && (unit <= 0xDFFFU))
    {
        return ID3_CP_ERROR;
    }
    *cp = unit;
    return ID3_CP_OK;
}

/**
 * @brief 从来源读下一个严格 UTF-8 码点（无过长编码、无代理项、不超 U+10FFFF）。
 * @param[in,out] source 码点来源。
 * @param[out] cp 码点。
 * @return 读取结果。
 */
static id3_cp_status_t id3_next_utf8(id3_source_t *source, uint32_t *cp)
{
    uint8_t lead;
    uint32_t extra;
    uint32_t code;

    if (source->pos >= source->length)
    {
        return ID3_CP_END;
    }
    lead = source->text[source->pos];
    if (lead < 0x80U)
    {
        source->pos++;
        *cp = lead;
        return ID3_CP_OK;
    }
    if ((lead >= 0xC2U) && (lead <= 0xDFU))
    {
        extra = 1U;
        code = lead & 0x1FU;
    }
    else if ((lead >= 0xE0U) && (lead <= 0xEFU))
    {
        extra = 2U;
        code = lead & 0x0FU;
    }
    else if ((lead >= 0xF0U) && (lead <= 0xF4U))
    {
        extra = 3U;
        code = lead & 0x07U;
    }
    else
    {
        return ID3_CP_ERROR;
    }
    if ((source->length - source->pos - 1U) < extra)
    {
        return ID3_CP_ERROR;
    }
    for (uint32_t k = 1U; k <= extra; k++)
    {
        uint8_t cont = source->text[source->pos + k];

        if ((cont & 0xC0U) != 0x80U)
        {
            return ID3_CP_ERROR;
        }
        code = (code << 6) | (uint32_t)(cont & 0x3FU);
    }
    if (((extra == 2U) && (code < 0x800U)) || ((extra == 3U) && ((code < 0x10000U) || (code > 0x10FFFFU))) ||
        ((code >= 0xD800U) && (code <= 0xDFFFU)))
    {
        return ID3_CP_ERROR;
    }
    source->pos += extra + 1U;
    *cp = code;
    return ID3_CP_OK;
}

/**
 * @brief 从来源读下一个码点。
 * @param[in,out] source 码点来源。
 * @param[out] cp 码点（0 表示多值分隔 NUL）。
 * @return 读取结果。
 */
static id3_cp_status_t id3_next_cp(id3_source_t *source, uint32_t *cp)
{
    if (source->encoding == 3U)
    {
        return id3_next_utf8(source, cp);
    }
    if (source->encoding != 0U)
    {
        return id3_next_utf16(source, cp);
    }
    if (source->pos >= source->length)
    {
        return ID3_CP_END;
    }
    if (!source->gbk || (source->text[source->pos] < 0x80U))
    {
        *cp = source->text[source->pos];
        source->pos++;
        return ID3_CP_OK;
    }
    *cp = source->lookup((uint16_t)(((uint16_t)source->text[source->pos] << 8) | source->text[source->pos + 1U]),
                         source->lookup_context);
    source->pos += 2U;
    return ID3_CP_OK;
}

/**
 * @brief 判断码点是否为要裁掉的空白（ASCII 空白与 U+3000）。
 * @param[in] cp 码点。
 * @return true 是空白。
 */
static bool id3_is_space(uint32_t cp)
{
    return (cp == 0x20U) || ((cp >= 0x09U) && (cp <= 0x0DU)) || (cp == 0x3000U);
}

/**
 * @brief 判断码点是否为 C0 或 C1 控制字符。
 * @param[in] cp 码点。
 * @return true 是控制字符。
 */
static bool id3_is_control(uint32_t cp)
{
    return (cp < 0x20U) || ((cp >= 0x80U) && (cp <= 0x9FU));
}

/**
 * @brief 把码点写成 UTF-8。
 * @param[out] out 输出，至少 4 字节。
 * @param[in] cp 码点。
 * @return 写入字节数。
 */
static uint32_t id3_encode_utf8(char *out, uint32_t cp)
{
    if (cp < 0x80U)
    {
        out[0] = (char)cp;
        return 1U;
    }
    if (cp < 0x800U)
    {
        out[0] = (char)(0xC0U | (cp >> 6));
        out[1] = (char)(0x80U | (cp & 0x3FU));
        return 2U;
    }
    if (cp < 0x10000U)
    {
        out[0] = (char)(0xE0U | (cp >> 12));
        out[1] = (char)(0x80U | ((cp >> 6) & 0x3FU));
        out[2] = (char)(0x80U | (cp & 0x3FU));
        return 3U;
    }
    out[0] = (char)(0xF0U | (cp >> 18));
    out[1] = (char)(0x80U | ((cp >> 12) & 0x3FU));
    out[2] = (char)(0x80U | ((cp >> 6) & 0x3FU));
    out[3] = (char)(0x80U | (cp & 0x3FU));
    return 4U;
}

/**
 * @brief 结束当前值：裁掉尾部空白，并为下一个值复位。
 * @param[in,out] field 字段状态。
 */
static void id3_field_end_value(id3_field_t *field)
{
    if (field->started)
    {
        field->length = field->last_end;
    }
    field->started = false;
    field->control_pending = false;
    field->last_end = field->length;
}

/**
 * @brief 向字段接收一个非 NUL 码点。
 * @param[in,out] field 字段状态。
 * @param[in] cp 码点。
 * @return false 表示整条取不到（控制字符）。
 * @note  前导空白丢弃；值内空白先暂存，值结束时裁掉尾部；放不下的完整字符不写，之后也不再写，保证按字符边界截断。
 */
static bool id3_field_feed(id3_field_t *field, uint32_t cp)
{
    char encoded[4];
    uint32_t n;
    uint32_t separator = 0U;

    if (id3_is_space(cp))
    {
        if (!field->started)
        {
            return true;
        }
        if (id3_is_control(cp))
        {
            field->control_pending = true;
        }
    }
    else
    {
        if (id3_is_control(cp) || field->control_pending)
        {
            return false;
        }
        if (!field->started)
        {
            field->started = true;
            separator = (field->length > 0U) ? ID3_SEPARATOR_BYTES : 0U;
        }
    }

    if (field->full)
    {
        return true;
    }
    n = id3_encode_utf8(encoded, cp);
    if ((field->length + separator + n) > field->capacity)
    {
        field->full = true;
        return true;
    }
    if (separator != 0U)
    {
        memcpy(field->out + field->length, ID3_SEPARATOR_TEXT, ID3_SEPARATOR_BYTES);
        field->length += separator;
    }
    memcpy(field->out + field->length, encoded, n);
    field->length += n;
    if (!id3_is_space(cp))
    {
        field->last_end = field->length;
    }
    return true;
}

/**
 * @brief 把一个文本帧的负载解码并清理成可显示的 UTF-8 字段。
 * @param[in] encoding 编码字节 0–3；大于 3 为失败。
 * @param[in] text 编码字节之后的文本负载。
 * @param[in] text_length 文本字节数。
 * @param[in] gbk_lookup GBK → Unicode 查表回调；NULL 表示不做 GBK 判定。
 * @param[in] gbk_context 查表回调上下文。
 * @param[out] out 输出缓冲；非 OK 时为空串。
 * @param[in] out_bytes 输出容量，含结尾 '\0'；结果超出时按字符边界截断，不加省略号。
 * @return ID3_READER_TEXT_OK、ID3_READER_TEXT_EMPTY 或 ID3_READER_TEXT_INVALID。
 * @note  清理顺序：按 NUL 拆多值 → 各值去首尾空白 → 丢空项 → 以 " / " 连写；含控制字符的整条失败。
 *        容量耗尽后仍继续校验剩余文本，非法内容照样整帧失败。
 */
Id3Reader_TextStatusTypeDef Id3Reader_DecodeField(uint8_t encoding,
                                                  const uint8_t *text,
                                                  uint32_t text_length,
                                                  Id3Reader_GbkLookupFunc gbk_lookup,
                                                  void *gbk_context,
                                                  char *out,
                                                  uint32_t out_bytes)
{
    id3_source_t source;
    id3_field_t field;
    uint32_t cp;
    id3_cp_status_t status;

    if ((out == NULL) || (out_bytes == 0U))
    {
        return ID3_READER_TEXT_INVALID;
    }
    out[0] = '\0';
    if ((encoding > 3U) || (text == NULL) ||
        (((encoding == 1U) || (encoding == 2U)) && ((text_length & 1U) != 0U)))
    {
        return ID3_READER_TEXT_INVALID;
    }

    memset(&source, 0, sizeof(source));
    source.encoding = encoding;
    source.text = text;
    source.length = text_length;
    source.big_endian = (encoding == 2U);
    source.value_start = true;
    source.lookup = gbk_lookup;
    source.lookup_context = gbk_context;
    if (encoding == 0U)
    {
        source.gbk = id3_text_is_gbk(text, text_length, gbk_lookup, gbk_context);
    }
    else if ((encoding == 3U) && (text_length >= 3U) && (text[0] == 0xEFU) && (text[1] == 0xBBU) &&
             (text[2] == 0xBFU))
    {
        source.pos = 3U;
    }

    field.out = out;
    field.capacity = out_bytes - 1U;
    field.length = 0U;
    field.last_end = 0U;
    field.started = false;
    field.full = false;
    field.control_pending = false;

    while ((status = id3_next_cp(&source, &cp)) == ID3_CP_OK)
    {
        if (cp == 0U)
        {
            id3_field_end_value(&field);
        }
        else if (!id3_field_feed(&field, cp))
        {
            out[0] = '\0';
            return ID3_READER_TEXT_INVALID;
        }
    }
    if (status == ID3_CP_ERROR)
    {
        out[0] = '\0';
        return ID3_READER_TEXT_INVALID;
    }

    id3_field_end_value(&field);
    out[field.length] = '\0';
    return (field.length == 0U) ? ID3_READER_TEXT_EMPTY : ID3_READER_TEXT_OK;
}
