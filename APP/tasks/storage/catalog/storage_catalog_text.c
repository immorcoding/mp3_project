/**
 * @file storage_catalog_text.c
 * @brief 曲目文本纯函数的实现：读首块、定位 TIT2/TPE1、解码清理，取不到时回退文件主名。
 */

#include "storage_catalog_text.h"

#include <string.h>

/** @brief 单个目标帧的取值状态。 */
typedef enum
{
    TEXT_UNSEEN = 0U, /**< 尚未遇到该帧。 */
    TEXT_OK,          /**< 取到可显示文本。 */
    TEXT_EMPTY,       /**< 帧存在但清理后为空。 */
    TEXT_INVALID      /**< 帧存在但解码失败。 */
} text_state_t;

/** @brief 遍历在未取到标题时的终止原因。 */
typedef enum
{
    STOP_DONE = 0U, /**< 标题与歌手都已有结论。 */
    STOP_END,       /**< 帧序列结束。 */
    STOP_LIMIT,     /**< 读取次数或单块范围用尽。 */
    STOP_ERROR      /**< 读回调报错。 */
} stop_reason_t;

/**
 * @brief 调用读回调并计数。
 * @param[in] request 输入。
 * @param[in,out] text 输出，累加 Reads。
 * @param[in] offset 文件偏移。
 * @param[in] bytes 请求字节数。
 * @param[out] got 实际字节数。
 * @return false 表示读取错误。
 */
static bool track_read(const StorageCatalog_TextRequestTypeDef *request,
                       StorageCatalog_TextTypeDef *text,
                       uint32_t offset,
                       uint32_t bytes,
                       uint32_t *got)
{
    text->Reads++;
    *got = 0U;
    return request->Read(request->ReadContext, offset, request->Block, bytes, got) && (*got <= bytes);
}

/**
 * @brief 解码一个目标帧并记录其状态；同种帧只认第一个。
 * @param[in] request 输入（提供 GBK 回调）。
 * @param[in] frame 找到的帧。
 * @param[in,out] state 该种帧的状态。
 * @param[out] out 输出缓冲。
 */
static void track_take_frame(const StorageCatalog_TextRequestTypeDef *request,
                             const Id3Reader_FrameTypeDef *frame,
                             text_state_t *state,
                             char *out)
{
    Id3Reader_TextStatusTypeDef status;

    if (*state != TEXT_UNSEEN)
    {
        return;
    }
    if (!frame->Readable)
    {
        out[0] = '\0';
        *state = TEXT_INVALID;
        return;
    }
    status = Id3Reader_DecodeField(frame->Encoding, frame->Text, frame->TextLength, request->GbkLookup,
                                   request->GbkContext, out, STORAGE_CATALOG_NAME_BYTES);
    *state = (status == ID3_READER_TEXT_OK) ? TEXT_OK : ((status == ID3_READER_TEXT_EMPTY) ? TEXT_EMPTY : TEXT_INVALID);
}

/**
 * @brief 由标题状态与终止原因得出结果分类。
 * @param[in] title 标题状态。
 * @param[in] stop 未取到标题时的终止原因。
 * @return 结果分类。
 */
static StorageCatalog_TextResultTypeDef track_classify(text_state_t title, stop_reason_t stop)
{
    switch (title)
    {
    case TEXT_OK:
        return STORAGE_CATALOG_TEXT_TAGGED;
    case TEXT_INVALID:
        return STORAGE_CATALOG_TEXT_DECODE_FAILED;
    case TEXT_EMPTY:
        return STORAGE_CATALOG_TEXT_FRAME_MISSING;
    default:
        break;
    }
    if (stop == STOP_LIMIT)
    {
        return STORAGE_CATALOG_TEXT_READ_LIMIT;
    }
    return (stop == STOP_ERROR) ? STORAGE_CATALOG_TEXT_READ_ERROR : STORAGE_CATALOG_TEXT_FRAME_MISSING;
}

/**
 * @brief 得到一首曲目的显示名与歌手。
 * @param[in] request 路径、文件长度、读回调、GBK 回调与工作缓冲。
 * @param[out] text 曲目显示名、歌手、结果分类与读取次数。
 * @return false 表示参数无效或文件主名提取失败（STOR-15 的防御分支，输出标题与歌手为空串）；
 *         标签读不出时仍返回 true，标题回退文件主名、歌手为空。
 * @note  读取计划：先读首块（文件更短则读到文件尾），逐帧定位 TIT2/TPE1；所需帧头或负载在块外时 seek 到该帧头再读一块，
 *        不读其他帧的负载；含首块最多 STORAGE_CATALOG_TEXT_MAX_READS 次，后续读取不越过标签总长。
 *        带标签级去同步的标签只解析首块。结果分类以标题为准：标题可用即为“取到”，此后歌手缺失、解码失败或读不到
 *        只令歌手为空。不访问 FatFs、RTOS 或日志。
 */
bool storage_catalog_track_text(const StorageCatalog_TextRequestTypeDef *request,
                                StorageCatalog_TextTypeDef *text)
{
    char title[STORAGE_CATALOG_NAME_BYTES];
    text_state_t title_state = TEXT_UNSEEN;
    text_state_t artist_state = TEXT_UNSEEN;
    stop_reason_t stop = STOP_END;
    Id3Reader_TypeDef reader;
    Id3Reader_FrameTypeDef frame;
    uint32_t block_offset = 0U;
    uint32_t block_length;
    uint32_t want;
    uint32_t next_offset;

    if ((request == NULL) || (text == NULL))
    {
        return false;
    }
    text->Title[0] = '\0';
    text->Artist[0] = '\0';
    text->Result = STORAGE_CATALOG_TEXT_NO_ID3V2;
    text->Reads = 0U;
    if ((request->Read == NULL) || (request->Block == NULL) ||
        !storage_catalog_display_name(request->Path, request->PathBytes, text->Title, sizeof(text->Title)))
    {
        text->Title[0] = '\0';
        return false;
    }
    if (request->FileBytes < ID3_READER_HEADER_BYTES)
    {
        return true;
    }

    want = (request->FileBytes < STORAGE_CATALOG_TEXT_BLOCK_BYTES) ? request->FileBytes : STORAGE_CATALOG_TEXT_BLOCK_BYTES;
    if (!track_read(request, text, 0U, want, &block_length))
    {
        text->Result = STORAGE_CATALOG_TEXT_READ_ERROR;
        return true;
    }
    switch (Id3Reader_Open(&reader, request->Block, &block_length, request->FileBytes))
    {
    case ID3_READER_OPEN_OK:
        break;
    case ID3_READER_OPEN_INVALID:
        text->Result = STORAGE_CATALOG_TEXT_DECODE_FAILED;
        return true;
    default:
        return true;
    }

    for (;;)
    {
        Id3Reader_StatusTypeDef status =
            Id3Reader_NextFrame(&reader, request->Block, block_offset, block_length, &frame, &next_offset);

        if (status == ID3_READER_FRAME)
        {
            if (frame.Kind == ID3_READER_KIND_TITLE)
            {
                track_take_frame(request, &frame, &title_state, title);
            }
            else
            {
                track_take_frame(request, &frame, &artist_state, text->Artist);
            }
            if ((title_state != TEXT_UNSEEN) && (artist_state != TEXT_UNSEEN))
            {
                stop = STOP_DONE;
                break;
            }
            continue;
        }
        if (status == ID3_READER_END)
        {
            stop = STOP_END;
            break;
        }
        if (reader.SingleBlock || (text->Reads >= STORAGE_CATALOG_TEXT_MAX_READS))
        {
            stop = STOP_LIMIT;
            break;
        }
        want = reader.TagEnd - next_offset;
        if (want > STORAGE_CATALOG_TEXT_BLOCK_BYTES)
        {
            want = STORAGE_CATALOG_TEXT_BLOCK_BYTES;
        }
        if (!track_read(request, text, next_offset, want, &block_length))
        {
            stop = STOP_ERROR;
            break;
        }
        block_offset = next_offset;
    }

    if (artist_state != TEXT_OK)
    {
        text->Artist[0] = '\0';
    }
    text->Result = track_classify(title_state, stop);
    if (title_state == TEXT_OK)
    {
        memcpy(text->Title, title, sizeof(text->Title));
    }
    return true;
}
