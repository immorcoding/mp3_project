/**
 ******************************************************************************
 * @file    id3_reader.h
 * @brief   与介质无关的 ID3v2 帧定位、文本解码与字段清理 Component Interface。
 * @note    不含 FatFs、RTOS 或 HAL：调用者读文件并把字节块连同其文件偏移交给本模块，
 *          本模块只回答“下一步读哪里”或“找到了哪个帧”。
 ******************************************************************************
 */

#ifndef ID3_READER_H
#define ID3_READER_H

#include <stdbool.h>
#include <stdint.h>

#define ID3_READER_HEADER_BYTES 10U /* ID3v2 标签头长度；首块至少要有这么多字节才可能识别标签。 */

/** @brief 标签头解析结果。 */
typedef enum
{
    ID3_READER_OPEN_OK = 0U,   /**< 识别到可读的 ID3v2.2/2.3/2.4 标签头。 */
    ID3_READER_OPEN_NO_TAG,    /**< 文件不以 "ID3" 开头，或不足一个标签头。 */
    ID3_READER_OPEN_INVALID    /**< 是 ID3 标签但不可读：版本不支持、大小字段非法或 v2.2 压缩位置位。 */
} Id3Reader_OpenStatusTypeDef;

/** @brief 帧遍历结果。 */
typedef enum
{
    ID3_READER_FRAME = 0U,     /**< 找到一个关心的帧，见 Id3Reader_FrameTypeDef。 */
    ID3_READER_NEED_DATA,      /**< 当前块覆盖不到下一处，调用者应从 next_offset 起重新读一块。 */
    ID3_READER_END             /**< 帧序列结束（标签尾、填充区、非法帧 ID 或标签被截断）。 */
} Id3Reader_StatusTypeDef;

/** @brief 本模块关心的帧种类。 */
typedef enum
{
    ID3_READER_KIND_TITLE = 0U, /**< TIT2（v2.2 为 TT2）。 */
    ID3_READER_KIND_ARTIST      /**< TPE1（v2.2 为 TP1）。 */
} Id3Reader_KindTypeDef;

/** @brief 文本字段清理结果。 */
typedef enum
{
    ID3_READER_TEXT_OK = 0U,   /**< 得到非空的可显示 UTF-8。 */
    ID3_READER_TEXT_EMPTY,     /**< 解码合法，但清理后为空。 */
    ID3_READER_TEXT_INVALID    /**< 整帧解码失败（编码字节、非法序列、孤立代理、奇数长度或控制字符）。 */
} Id3Reader_TextStatusTypeDef;

/**
 * @brief GBK 双字节码转 Unicode 的查表回调。
 * @param[in] gbk_code 首字节在高 8 位的 GBK 双字节码。
 * @param[in] context 调用者上下文。
 * @return Unicode 码点；0 表示无映射。
 */
typedef uint16_t (*Id3Reader_GbkLookupFunc)(uint16_t gbk_code, void *context);

/** @brief 一次标签遍历的状态；Id3Reader_Open 初始化，之后只由本模块修改。 */
typedef struct
{
    uint8_t Version;     /**< 主版本号：2、3 或 4。 */
    bool SingleBlock;    /**< 标签级去同步已就地还原：偏移不再对应文件，只能在首块内解析。 */
    uint32_t TagEnd;     /**< 标签末尾偏移（不含 footer），已夹到文件长度内。 */
    uint32_t Cursor;     /**< 下一个帧头的偏移。 */
} Id3Reader_TypeDef;

/** @brief 找到的帧；指针指向调用者的块，随块失效。 */
typedef struct
{
    Id3Reader_KindTypeDef Kind; /**< 帧种类。 */
    bool Readable;              /**< false：帧为压缩、加密、负载为空或大于一块，视为解码失败。 */
    uint8_t Encoding;           /**< 文本编码字节（Readable 时有效）。 */
    const uint8_t *Text;        /**< 编码字节之后的文本负载（已还原帧级去同步，已跳过分组与数据长度字段）。 */
    uint32_t TextLength;        /**< 文本负载字节数。 */
} Id3Reader_FrameTypeDef;

Id3Reader_OpenStatusTypeDef Id3Reader_Open(Id3Reader_TypeDef *reader,
                                           uint8_t *block,
                                           uint32_t *block_length,
                                           uint32_t file_length);

Id3Reader_StatusTypeDef Id3Reader_NextFrame(Id3Reader_TypeDef *reader,
                                            uint8_t *block,
                                            uint32_t block_offset,
                                            uint32_t block_length,
                                            Id3Reader_FrameTypeDef *frame,
                                            uint32_t *next_offset);

Id3Reader_TextStatusTypeDef Id3Reader_DecodeField(uint8_t encoding,
                                                  const uint8_t *text,
                                                  uint32_t text_length,
                                                  Id3Reader_GbkLookupFunc gbk_lookup,
                                                  void *gbk_context,
                                                  char *out,
                                                  uint32_t out_bytes);

#endif /* ID3_READER_H */
