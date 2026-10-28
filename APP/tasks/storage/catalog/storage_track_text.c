/**
 * @file storage_track_text.c
 * @brief 曲目文本的文件读取封装：打开、取长度、按偏移读、调 storage_catalog_track_text()、关闭。
 */

#include "storage_track_text.h"

#include <stdint.h>
#include <string.h>

#include "Service/filesystem/filesystem_file.h"
#include "Service/filesystem/filesystem_text.h"

/** @brief 标签读取工作缓冲；只由 Storage Task 使用，放静态区以免占任务栈。 */
static uint8_t storage_track_text_block[STORAGE_CATALOG_TEXT_BLOCK_BYTES];

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
 * @brief 读一首曲目的显示名与歌手。
 * @param[in] path 曲库 UTF-8 相对路径（SD 卷）。
 * @return 本 Module 持有的结果，下一次调用前有效；NULL 表示文件主名提取失败（STOR-15 的防御分支）。
 * @note  只由 Storage Task 调用。打开失败按“打开失败”回退文件主名；取文件长度失败按“读取错误”回退；
 *        其余标签侧结果由 storage_catalog_track_text() 判定。标签读取 FIL 用完即关，与音频流 FIL 合计
 *        不超过 FatFs `_FS_LOCK`（2）。一次调用最坏为 f_open + 取长度 + STORAGE_CATALOG_TEXT_MAX_READS 次读取 + f_close。
 */
const StorageTrackText_ResultTypeDef *storage_track_text_read(const char *path)
{
    StorageTrackText_ResultTypeDef *result = &storage_track_text_result;
    StorageCatalog_TextRequestTypeDef request;
    Service_Filesystem_FileHandleTypeDef file = {0};
    uint64_t size64 = 0U;
    uint32_t path_bytes;
    bool extracted;

    if (path == NULL)
    {
        return NULL;
    }
    path_bytes = (uint32_t)strlen(path);
    result->OpenFailed = false;

    if (Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD,
                                    path,
                                    SERVICE_FILESYSTEM_FILE_MODE_READ,
                                    &file) != SERVICE_OK)
    {
        result->OpenFailed = true;
        result->Text.Artist[0] = '\0';
        result->Text.Result = STORAGE_CATALOG_TEXT_NO_ID3V2;
        result->Text.Reads = 0U;
        return storage_catalog_display_name(path, path_bytes, result->Text.Title, sizeof(result->Text.Title))
                   ? result
                   : NULL;
    }

    if (Service_Filesystem_GetFileSize(file, &size64) != SERVICE_OK)
    {
        (void)Service_Filesystem_CloseFile(file);
        result->Text.Artist[0] = '\0';
        result->Text.Result = STORAGE_CATALOG_TEXT_READ_ERROR;
        result->Text.Reads = 0U;
        return storage_catalog_display_name(path, path_bytes, result->Text.Title, sizeof(result->Text.Title))
                   ? result
                   : NULL;
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
