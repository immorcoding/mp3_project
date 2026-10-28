/**
 * @file storage_catalog_text.h
 * @brief 曲目文本纯函数：由路径与“按偏移读文件”回调得到曲目显示名、歌手、结果分类与读取次数。
 * @note  不访问 FatFs、RTOS 或日志，可在主机上直接测试；计时与日志由调用方做。规则见 STOR-15、STOR-16。
 */

#ifndef STORAGE_CATALOG_TEXT_H
#define STORAGE_CATALOG_TEXT_H

#include <stdbool.h>
#include <stdint.h>

#include "Components/id3_reader/id3_reader.h"
#include "storage_catalog_config.h"
#include "storage_catalog_name.h"

/** @brief 标签读取结果分类，以标题为准。 */
typedef enum
{
    STORAGE_CATALOG_TEXT_TAGGED = 0U,   /**< 取到：TIT2 可用。 */
    STORAGE_CATALOG_TEXT_NO_ID3V2,      /**< 无 ID3v2 标签。 */
    STORAGE_CATALOG_TEXT_FRAME_MISSING, /**< TIT2 缺失或清理后为空。 */
    STORAGE_CATALOG_TEXT_DECODE_FAILED, /**< TIT2 解码失败，或标签头不可读。 */
    STORAGE_CATALOG_TEXT_READ_LIMIT,    /**< 读取次数或首块范围用尽仍未取到 TIT2。 */
    STORAGE_CATALOG_TEXT_READ_ERROR     /**< 读回调报错且未取到 TIT2。 */
} StorageCatalog_TextResultTypeDef;

/**
 * @brief 按偏移读文件的回调。
 * @param[in] context 调用者上下文。
 * @param[in] offset 文件偏移。
 * @param[out] buffer 输出缓冲。
 * @param[in] bytes 请求字节数。
 * @param[out] read_bytes 实际读到的字节数（短读表示文件尾）。
 * @return true 读取成功；false 读取错误。
 */
typedef bool (*StorageCatalog_TextReadFunc)(void *context,
                                           uint32_t offset,
                                           uint8_t *buffer,
                                           uint32_t bytes,
                                           uint32_t *read_bytes);

/** @brief 曲目文本函数的输入。 */
typedef struct
{
    const char *Path;                         /**< 曲库 UTF-8 相对路径（用于文件主名）。 */
    uint32_t PathBytes;                       /**< 路径字节数。 */
    uint32_t FileBytes;                       /**< 文件长度。 */
    StorageCatalog_TextReadFunc Read;         /**< 按偏移读文件。 */
    void *ReadContext;                        /**< 读回调上下文。 */
    Id3Reader_GbkLookupFunc GbkLookup;        /**< GBK 查表回调；NULL 表示不做 GBK 判定。 */
    void *GbkContext;                         /**< 查表回调上下文。 */
    uint8_t *Block;                           /**< 工作缓冲，至少 STORAGE_CATALOG_TEXT_BLOCK_BYTES 字节；内容会被就地改写。 */
} StorageCatalog_TextRequestTypeDef;

/** @brief 曲目文本函数的输出。 */
typedef struct
{
    char Title[STORAGE_CATALOG_NAME_BYTES];   /**< 曲目显示名：可用标题，否则文件主名。 */
    char Artist[STORAGE_CATALOG_NAME_BYTES];  /**< 歌手；取不到为空串。 */
    StorageCatalog_TextResultTypeDef Result;  /**< 结果分类。 */
    uint8_t Reads;                            /**< 本次读回调被调用的次数。 */
} StorageCatalog_TextTypeDef;

bool storage_catalog_track_text(const StorageCatalog_TextRequestTypeDef *request,
                                StorageCatalog_TextTypeDef *text);

#endif /* STORAGE_CATALOG_TEXT_H */
