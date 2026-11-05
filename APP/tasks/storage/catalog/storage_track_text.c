/**
 * @file storage_track_text.c
 * @brief 曲目文本的文件读取封装：打开、取长度、按偏移读、调 storage_catalog_track_text()、关闭。
 */

#include "storage_track_text.h"

#include "storage_catalog.h"
#include "storage_track_open.h"

#include <stdint.h>
#include <string.h>

#include "Service/filesystem/filesystem_file.h"
#include "Service/filesystem/filesystem_text.h"

/* 曲目标签的每次读取不超过文件接口单次读写的上限。 */
_Static_assert(STORAGE_CATALOG_TEXT_BLOCK_BYTES <= SERVICE_FILESYSTEM_IO_MAX_BYTES,
               "tag read block must fit one Filesystem transfer");

/** @brief 标签读取工作缓冲；只由 Storage Task 使用，放静态区以免占任务栈。 */
static uint8_t storage_track_text_block[STORAGE_CATALOG_TEXT_BLOCK_BYTES];

/** @brief 当前曲目的曲库相对路径；仅用于文件主名回退与退回路径打开，不进结果。 */
static char storage_track_text_path[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];

/** @brief 最近一次读取的结果；下一次读取前有效。 */
static StorageTrackText_ResultTypeDef storage_track_text_result;

/**
 * @brief 曲目文本纯函数的读回调：seek 到偏移后读至多 bytes 字节。
 * @param[in] context 指向已打开的文件句柄。
 * @param[in] offset 文件偏移。
 * @param[out] buffer 输出缓冲。
 * @param[in] bytes 请求字节数。
 * @param[out] read_bytes 实际读到的字节数；短读表示文件尾。
 * @return true 读取成功；false seek 或读取报错。
 */
static bool storage_track_text_read_at(void *context,
                                       uint32_t offset,
                                       uint8_t *buffer,
                                       uint32_t bytes,
                                       uint32_t *read_bytes)
{
    const Service_Filesystem_FileHandleTypeDef *file = (const Service_Filesystem_FileHandleTypeDef *)context;

    *read_bytes = 0U;
    if (Service_Filesystem_SeekFile(*file, (uint64_t)offset) != SERVICE_OK)
    {
        return false;
    }
    return Service_Filesystem_ReadFile(*file, buffer, bytes, read_bytes) == SERVICE_OK;
}

/**
 * @brief GBK 查表回调：转发 Filesystem Service 的薄函数，FatFs 不出 Service。
 * @param[in] gbk_code 首字节在高 8 位的 GBK 双字节码。
 * @param[in] context 未用。
 * @return Unicode 码点；0 表示无映射。
 */
static uint16_t storage_track_text_gbk(uint16_t gbk_code, void *context)
{
    (void)context;
    return Service_Filesystem_GbkToUnicode(gbk_code);
}

/**
 * @brief 标签读不出时的回退：标题取文件主名、歌手为空，结果分类由调用者指定，读取次数为 0。
 * @param[in,out] result 本 Module 的结果；写入标题、歌手、分类与读取次数。
 * @param[in] path 曲库 UTF-8 相对路径。
 * @param[in] path_bytes 路径字节数。
 * @param[in] reason 回退的结果分类。
 * @return result；NULL 表示文件主名提取失败（STOR-15 的防御分支）。
 */
static const StorageTrackText_ResultTypeDef *storage_track_text_fallback(StorageTrackText_ResultTypeDef *result,
                                                                          const char *path,
                                                                          uint32_t path_bytes,
                                                                          StorageCatalog_TextResultTypeDef reason)
{
    result->Text.Artist[0] = '\0';
    result->Text.Result = reason;
    result->Text.Reads = 0U;
    return storage_catalog_display_name(path, path_bytes, result->Text.Title, sizeof(result->Text.Title)) ? result : NULL;
}

/**
 * @brief 读一首曲目的显示名与歌手。
 * @param[in] position 播放列表位置，只用于核对不符时的日志。
 * @param[in] catalog_index 曲目的 Catalog 下标；调用者须已核对 Sheet 与 Catalog 代次一致。
 * @return 本 Module 持有的结果，下一次调用前有效；NULL 表示文件主名提取失败（STOR-15 的防御分支），
 *         或下标已不在曲库内（拷不出路径），或打开时得到 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE`（卷不可用，
 *         不是文件错误，不回退、不计数）。
 * @note  只由 Storage Task 调用。先按曲库里的文件定位符打开，核对不符退回按路径打开（STOR-17、ARC-19）；
 *        打开失败按“打开失败”回退文件主名；取文件长度失败按“读取错误”回退；
 *        其余标签侧结果由 storage_catalog_track_text() 判定。标签读取 FIL 用完即关，与音频流 FIL 合计
 *        不超过 FatFs `_FS_LOCK`（2）。一次调用最坏为 f_open + 取长度 + STORAGE_CATALOG_TEXT_MAX_READS 次读取 + f_close。
 */
const StorageTrackText_ResultTypeDef *storage_track_text_read(uint16_t position, uint16_t catalog_index)
{
    StorageTrackText_ResultTypeDef *result = &storage_track_text_result;
    StorageCatalog_TextRequestTypeDef request;
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_StatusTypeDef open_status;
    uint64_t size64 = 0U;
    const char *path = storage_track_text_path;
    uint32_t path_bytes;
    bool extracted;

    if (storage_catalog_copy_path(catalog_index, storage_track_text_path, sizeof(storage_track_text_path)) != STORAGE_OK)
    {
        return NULL;
    }
    path_bytes = (uint32_t)strlen(path);
    result->OpenFailed = false;
    result->OpenUs = 0U;

    open_status = storage_track_open(position, catalog_index, path, &file, &result->OpenUs);
    if (Service_Filesystem_StatusIsVolumeUnavailable(open_status))
    {
        /* 卷没了或正在对账：不是文件错误，不回退文件主名、不算“打开失败”；调用者的窗口或名字槽随后被卷事件收掉。 */
        return NULL;
    }
    if (open_status != SERVICE_OK)
    {
        result->OpenFailed = true;
        return storage_track_text_fallback(result, path, path_bytes, STORAGE_CATALOG_TEXT_NO_ID3V2);
    }

    if (Service_Filesystem_GetFileSize(file, &size64) != SERVICE_OK)
    {
        (void)Service_Filesystem_CloseFile(file);
        return storage_track_text_fallback(result, path, path_bytes, STORAGE_CATALOG_TEXT_READ_ERROR);
    }

    request.Path = path;
    request.PathBytes = path_bytes;
    request.FileBytes = (size64 > UINT32_MAX) ? UINT32_MAX : (uint32_t)size64;
    request.Read = storage_track_text_read_at;
    request.ReadContext = &file;
    request.GbkLookup = storage_track_text_gbk;
    request.GbkContext = NULL;
    request.Block = storage_track_text_block;

    extracted = storage_catalog_track_text(&request, &result->Text);
    (void)Service_Filesystem_CloseFile(file);
    return extracted ? result : NULL;
}
