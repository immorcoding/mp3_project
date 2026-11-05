/**
 * @file catalog_track_text.c
 * @brief 曲目文本的文件读取封装：打开、取长度、按偏移读、调 catalog_track_text()、关闭。
 */

#include "catalog_track_text.h"

#include "catalog_track_open.h"

#include <stdint.h>
#include <string.h>

#include "Service/filesystem/filesystem_file.h"
#include "Service/filesystem/filesystem_text.h"

/* 曲目标签的每次读取不超过文件接口单次读写的上限。 */
_Static_assert(CATALOG_TEXT_BLOCK_BYTES <= SERVICE_FILESYSTEM_IO_MAX_BYTES,
               "tag read block must fit one Filesystem transfer");

/** @brief 标签读取工作缓冲；只由 Storage Task 使用，放静态区以免占任务栈。 */
static uint8_t catalog_track_text_block[CATALOG_TEXT_BLOCK_BYTES];

/** @brief 最近一次读取的结果；下一次读取前有效。 */
static CatalogTrackText_ResultTypeDef catalog_track_text_result;

/** @brief 最近一次读取是因为打开时得到卷不可用（`NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE`）而返回空结果。 */
static bool catalog_track_text_unavailable;

/**
 * @brief 最近一次 `catalog_track_text_read()` 返回空结果，是不是因为打开时卷不可用（拔卡的正常竞态）。
 * @return true 是：不是文件错误，也不是防御分支，调用者不必记 WARN，随后的卷事件会收掉窗口或名字槽。
 * @note  每次 `catalog_track_text_read()` 开头清零；只由 Storage Task 调用。
 */
bool catalog_track_text_last_read_unavailable(void)
{
    return catalog_track_text_unavailable;
}

/**
 * @brief 曲目文本纯函数的读回调：seek 到偏移后读至多 bytes 字节。
 * @param[in] context 指向已打开的文件句柄。
 * @param[in] offset 文件偏移。
 * @param[out] buffer 输出缓冲。
 * @param[in] bytes 请求字节数。
 * @param[out] read_bytes 实际读到的字节数；短读表示文件尾。
 * @return true 读取成功；false seek 或读取报错。
 */
static bool catalog_track_text_read_at(void *context,
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
static uint16_t catalog_track_text_gbk(uint16_t gbk_code, void *context)
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
static const CatalogTrackText_ResultTypeDef *catalog_track_text_fallback(CatalogTrackText_ResultTypeDef *result,
                                                                          const char *path,
                                                                          uint32_t path_bytes,
                                                                          Catalog_TextResultTypeDef reason)
{
    result->Text.Artist[0] = '\0';
    result->Text.Result = reason;
    result->Text.Reads = 0U;
    return catalog_display_name(path, path_bytes, result->Text.Title, sizeof(result->Text.Title)) ? result : NULL;
}

/**
 * @brief 读一首曲目的显示名与歌手。
 * @param[in] position 播放列表位置，只用于核对不符时的日志。
 * @param[in] track `catalog_read_track()` 一次读全的结果，须是有效位置（路径、定位符属于同一曲库代次）。
 * @return 本 Module 持有的结果，下一次调用前有效；NULL 表示文件主名提取失败（STOR-15 的防御分支），
 *         或打开时得到 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE`（卷不可用，
 *         不是文件错误，不回退、不计数）。
 * @note  只由 Storage Task 调用。先按曲库里的文件定位符打开，核对不符退回按路径打开（STOR-17、ARC-19）；
 *        打开失败按“打开失败”回退文件主名；取文件长度失败按“读取错误”回退；
 *        其余标签侧结果由 catalog_track_text() 判定。标签读取 FIL 用完即关，与音频流 FIL 合计
 *        不超过 FatFs `_FS_LOCK`（2）。一次调用最坏为 f_open + 取长度 + CATALOG_TEXT_MAX_READS 次读取 + f_close。
 */
const CatalogTrackText_ResultTypeDef *catalog_track_text_read(uint16_t position, const Catalog_TrackTypeDef *track)
{
    CatalogTrackText_ResultTypeDef *result = &catalog_track_text_result;
    Catalog_TextRequestTypeDef request;
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_StatusTypeDef open_status;
    uint64_t size64 = 0U;
    const char *path;
    uint32_t path_bytes;
    bool extracted;

    catalog_track_text_unavailable = false;
    if ((track == NULL) || (track->Path[0] == '\0'))
    {
        return NULL;
    }
    path = track->Path;
    path_bytes = (uint32_t)strlen(path);
    catalog_track_text_unavailable = false;
    result->OpenFailed = false;
    result->OpenUs = 0U;

    open_status = catalog_track_open(position, track, &file, &result->OpenUs);
    if (Service_Filesystem_StatusIsVolumeUnavailable(open_status))
    {
        /* 卷没了或正在对账：不是文件错误，不回退文件主名、不算“打开失败”；调用者的窗口或名字槽随后被卷事件收掉。 */
        catalog_track_text_unavailable = true;
        return NULL;
    }
    if (open_status != SERVICE_OK)
    {
        result->OpenFailed = true;
        return catalog_track_text_fallback(result, path, path_bytes, CATALOG_TEXT_NO_ID3V2);
    }

    if (Service_Filesystem_GetFileSize(file, &size64) != SERVICE_OK)
    {
        (void)Service_Filesystem_CloseFile(file);
        return catalog_track_text_fallback(result, path, path_bytes, CATALOG_TEXT_READ_ERROR);
    }

    request.Path = path;
    request.PathBytes = path_bytes;
    request.FileBytes = (size64 > UINT32_MAX) ? UINT32_MAX : (uint32_t)size64;
    request.Read = catalog_track_text_read_at;
    request.ReadContext = &file;
    request.GbkLookup = catalog_track_text_gbk;
    request.GbkContext = NULL;
    request.Block = catalog_track_text_block;

    extracted = catalog_track_text(&request, &result->Text);
    (void)Service_Filesystem_CloseFile(file);
    return extracted ? result : NULL;
}
