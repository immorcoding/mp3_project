/**
 * @file storage_catalog.c
 * @brief 扫描 SD `Music/`，把相对路径写入 SDRAM 字符串池；成功后生成顺序播放列表。
 */

#include "storage_catalog.h"
#include "storage_catalog_name.h"
#include "storage_sheet.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Service/filesystem/filesystem_directory.h"
#include "Service/filesystem/filesystem_types.h"
#include "Service/log/log_service.h"

typedef struct
{
    uint32_t Generation; /**< Catalog 代次，内部 .bss 计数的副本。 */
    uint16_t IndexNum;   /**< 已收录曲目数。 */
    uint32_t Tail;       /**< 字符串池已用字节，含每条结尾 '\0'。 */
    char Path[STORAGE_CATALOG_MUSIC_POOL_SIZE];
} StorageCatalog_MusicTypeDef;

typedef struct
{
    uint32_t Offset; /**< 相对 Path 池起点。 */
    uint32_t Length; /**< 路径字节数，不含 '\0'。 */
} StorageCatalog_MusicItemsTypeDef;

static const char storage_catalog_log_tag[] = "Catalog";

static Service_Filesystem_DirectoryHandleTypeDef music_directory;

/** @brief 位于内部 .bss，上电为零；SDRAM 里的 Generation 只是它的副本。 */
static uint32_t music_catalog_generation;

/** @brief 本轮扫描的工作下标，上电为零，每次开扫前清零。 */
static uint32_t music_catalog_index;

/** @brief 本轮扫描因为卷不可用（NO_MEDIA、NOT_READY、INVALID_HANDLE）被丢弃；每次开扫前清零，只在 Storage Task 访问。 */
static bool music_catalog_scan_aborted;

static StorageCatalog_MusicTypeDef MusicCatalogPool
    __attribute__((section(".storage_catalog"), aligned(32)));

static StorageCatalog_MusicItemsTypeDef MusicCatalogIndex[STORAGE_CATALOG_MUSIC_MAX_NUM]
    __attribute__((section(".storage_catalog"), aligned(32)));

/** @brief 与 MusicCatalogIndex 同下标的文件定位符（16 B/条）；扫描时随条目写入，只在同一曲库代次内有效。 */
static Service_Filesystem_FileLocatorTypeDef MusicCatalogLocator[STORAGE_CATALOG_MUSIC_MAX_NUM]
    __attribute__((section(".storage_catalog"), aligned(32)));

static char storage_catalog_dir_stack[STORAGE_CATALOG_DIR_STACK_MAX]
                                     [SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U]
    __attribute__((section(".storage_catalog"), aligned(32)));

/** @brief 本轮扫描开始时的周期计数器读数；与 music_catalog_scan_cycles 一起只在 Storage Task 访问。 */
static uint32_t music_catalog_scan_last_cycles;

/** @brief 本轮扫描累计的周期数（64 位）：DWT 为 32 位，480 MHz 下约 9 s 回绕，扫描可能更长，所以边走边累加。 */
static uint64_t music_catalog_scan_cycles;

/**
 * @brief 开始计本轮扫描耗时。
 */
static void storage_catalog_scan_clock_start(void)
{
    music_catalog_scan_last_cycles = CortexM7CycleCounter_Read();
    music_catalog_scan_cycles = 0U;
}

/**
 * @brief 把距上次读数过去的周期数累加进本轮扫描耗时。
 * @note  扫描里每次目录打开与读取之后调用；相邻两次调用相隔一次目录操作（毫秒级），远小于 32 位计数器的回绕周期，
 *        所以无符号减法取差值总是对的。
 */
static void storage_catalog_scan_clock_poll(void)
{
    const uint32_t now = CortexM7CycleCounter_Read();

    music_catalog_scan_cycles += (uint32_t)(now - music_catalog_scan_last_cycles);
    music_catalog_scan_last_cycles = now;
}

/**
 * @brief 本轮扫描耗时，微秒。
 * @return 微秒数（饱和在 32 位）；核心频率不可用时为 0。
 */
static uint32_t storage_catalog_scan_clock_us(void)
{
    const uint32_t frequency_hz = CortexM7CycleCounter_GetFrequencyHz();
    uint64_t us;

    if (frequency_hz == 0U)
    {
        return 0U;
    }

    us = (music_catalog_scan_cycles * 1000000ULL) / frequency_hz;
    return (us > UINT32_MAX) ? UINT32_MAX : (uint32_t)us;
}

/**
 * @brief 记扫描结束一行 INFO：结果、条数、耗时（DIAG-6）。
 * @param[in] result 结果名，静态字符串：ok、dfs_error、sheet_error、aborted。
 * @note  常开，每次扫描一行；文本只含结果名与数值。耗时单位微秒，含目录读取、打开与扫描中的任务切换，不含建播放列表。
 */
static void storage_catalog_scan_log_end(const char *result)
{
    char text[64];

    (void)snprintf(text, sizeof(text), "Scan done res=%s n=%lu us=%lu",
                   result,
                   (unsigned long)MusicCatalogPool.IndexNum,
                   (unsigned long)storage_catalog_scan_clock_us());
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, storage_catalog_log_tag, text);
}

/**
 * @brief 作废当前 Music Catalog，不把整池清零。
 */
static void storage_catalog_music_reset(void)
{
    music_catalog_generation++;
    music_catalog_index = 0U;
    MusicCatalogPool.Generation = music_catalog_generation;
    MusicCatalogPool.IndexNum = 0U;
    MusicCatalogPool.Tail = 0U;
}

/**
 * @brief 判断文件名是否以 `.mp3` / `.MP3` 结尾。
 * @param[in] filename 以 '\0' 结尾的文件名；NULL 视为不收录。
 * @return true 长度在 FatFs 名字上限内且扩展名符合；否则 false。
 */
static bool storage_catalog_music_file_strcheck(const char *filename)
{
    uint32_t length;

    if (filename == NULL)
    {
        return false;
    }

    length = (uint32_t)strlen(filename);
    if (length > SERVICE_FILESYSTEM_NAME_MAX_BYTES)
    {
        return false;
    }

    /* 与显示名去扩展名共用同一判断，收录规则不会与显示规则漂移。 */
    return storage_catalog_has_mp3_extension(filename, length);
}

/**
 * @brief 把一条已拼好的 UTF-8 相对路径及其文件定位符写入曲库。
 * @param[in] path UTF-8 相对路径。
 * @param[in] locator 目录扫描时随条目给出的文件定位符，全零表示没有。
 * @return true 已收录；false 条数满或池满。
 */
static bool storage_catalog_music_append(const char *path,
                                         const Service_Filesystem_FileLocatorTypeDef *locator)
{
    uint32_t length;
    uint32_t needed;

    if (music_catalog_index >= STORAGE_CATALOG_MUSIC_MAX_NUM)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                               storage_catalog_log_tag,
                               "Music Catalog Full.");
        return false;
    }

    length = (uint32_t)strlen(path);
    needed = length + 1U;
    if ((MusicCatalogPool.Tail + needed) > STORAGE_CATALOG_MUSIC_POOL_SIZE)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                               storage_catalog_log_tag,
                               "Music Catalog Pool Full.");
        return false;
    }

    (void)memcpy(&MusicCatalogPool.Path[MusicCatalogPool.Tail], path, needed);
    MusicCatalogIndex[music_catalog_index].Offset = MusicCatalogPool.Tail;
    MusicCatalogIndex[music_catalog_index].Length = length;
    MusicCatalogLocator[music_catalog_index] = *locator;
    MusicCatalogPool.Tail += needed;
    music_catalog_index++;
    MusicCatalogPool.IndexNum = music_catalog_index;
    return true;
}

/**
 * @brief 拼接父目录与目录项名，禁止末尾 `/`。
 */
static bool storage_catalog_music_join_path(char *destination,
                                            uint32_t destination_size,
                                            const char *directory,
                                            const char *name)
{
    int written;

    written = snprintf(destination, destination_size, "%s/%s", directory, name);
    return (written >= 0) && ((uint32_t)written < destination_size);
}

/**
 * @brief 用路径栈遍历 `Music/`，同时只开一个目录句柄；每个 `.mp3` 条目连同它的文件定位符写入曲库。
 * @return STORAGE_OK 扫描完成（含没有 `Music/`、子目录打不开、表满截断）；STORAGE_ERROR 目录读取或关闭失败，
 *         或卷不可用（`music_catalog_scan_aborted` 置位，不记 ERROR）。
 */
static Storage_StatusTypeDef storage_catalog_music_dfs(void)
{
    uint32_t stack_top = 1U;
    Storage_StatusTypeDef result = STORAGE_OK;

    (void)memset(storage_catalog_dir_stack[0], 0, sizeof(storage_catalog_dir_stack[0]));
    (void)memcpy(storage_catalog_dir_stack[0], "Music", sizeof("Music"));

    while (stack_top > 0U)
    {
        char current[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];
        Service_StatusTypeDef open_status;
        Service_StatusTypeDef read_status;
        Service_StatusTypeDef close_status;

        stack_top--;
        (void)memcpy(current,
                      storage_catalog_dir_stack[stack_top],
                      sizeof(current));

        open_status = Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD,
                                                       current,
                                                       &music_directory);
        if (Service_Filesystem_StatusIsVolumeUnavailable(open_status))
        {
            /* 在途调用被拔卡打断时，这次失败发生在卷任务对账之前，卷序号还没变。路径打开没有句柄可借来等闸，
             * 所以再开一次：它在 Filesystem 入口等闸，等到本轮对账结束，才有资格下结论——卡没了（卷已卸载，
             * 再次得到卷不可用）或卡还在（照常打开）。 */
            open_status = Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD,
                                                           current,
                                                           &music_directory);
        }
        if (open_status != SERVICE_OK)
        {
            storage_catalog_scan_clock_poll();
            if (Service_Filesystem_StatusIsVolumeUnavailable(open_status))
            {
                /* 卷没了或正在对账：不是“没有 Music 目录”，也不是子目录坏了；丢弃本次扫描，等卷事件。 */
                music_catalog_scan_aborted = true;
                return STORAGE_ERROR;
            }
            if ((stack_top == 0U) && (strcmp(current, "Music") == 0))
            {
                (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                                       storage_catalog_log_tag,
                                       "Music Directory Open Failed.");
                return STORAGE_OK;
            }

            (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                                   storage_catalog_log_tag,
                                   "Sub Directory Open Failed.");
            continue;
        }

        storage_catalog_scan_clock_poll();

        for (;;)
        {
            Service_Filesystem_DirectoryEntryTypeDef entry;
            char child[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];

            read_status = Service_Filesystem_ReadDirectory(music_directory, &entry);
            storage_catalog_scan_clock_poll();
            if (read_status != SERVICE_OK)
            {
                if (Service_Filesystem_StatusIsVolumeUnavailable(read_status))
                {
                    music_catalog_scan_aborted = true; /* 同上：不是文件错误，丢弃本次扫描。 */
                }
                else
                {
                    (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                                           storage_catalog_log_tag,
                                           "Music Directory Read Failed.");
                }
                result = STORAGE_ERROR;
                break;
            }

            if (entry.Name[0] == '\0')
            {
                break;
            }

            if (entry.Name[0] == '.')
            {
                continue;
            }

            if (!storage_catalog_music_join_path(child, sizeof(child), current, entry.Name))
            {
                continue;
            }

            if (entry.IsDirectory)
            {
                if (stack_top >= STORAGE_CATALOG_DIR_STACK_MAX)
                {
                    (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                                           storage_catalog_log_tag,
                                           "Music Directory Stack Full.");
                    continue;
                }

                (void)memcpy(storage_catalog_dir_stack[stack_top], child, sizeof(child));
                stack_top++;
                continue;
            }

            if (!storage_catalog_music_file_strcheck(entry.Name))
            {
                continue;
            }

            if (!storage_catalog_music_append(child, &entry.Locator))
            {
                result = STORAGE_OK;
                stack_top = 0U;
                break;
            }
        }

        close_status = Service_Filesystem_CloseDirectory(music_directory);
        if (close_status != SERVICE_OK)
        {
            if (Service_Filesystem_StatusIsVolumeUnavailable(close_status))
            {
                music_catalog_scan_aborted = true;
            }
            result = STORAGE_ERROR;
            break;
        }

        if (result != STORAGE_OK)
        {
            break;
        }
    }

    return result;
}

/**
 * @brief 最近一次扫描结束后，Storage Task 该怎么对待。
 * @param[in] volume_unchanged 扫描结束时 SD 卷仍挂着且卷序号与 Storage 已处理的相同。
 * @return 扫描没有因卷不可用而丢弃：`DONE`。丢弃了且卷变了（卡没了或换了）：`EVENT_PENDING`，等卷事件重扫，
 *         不要就绪。丢弃了但卷没变：`UNREADABLE`——卡还在、设备出错，没有边沿就没有卷事件，等也等不来，
 *         调用者要报错（卷任务对账之前的调用已在 Filesystem 入口等过闸，所以卷序号此时是结论）。
 */
StorageCatalog_ScanVerdictTypeDef storage_catalog_scan_verdict(bool volume_unchanged)
{
    if (!music_catalog_scan_aborted)
    {
        return STORAGE_CATALOG_SCAN_DONE;
    }

    return volume_unchanged ? STORAGE_CATALOG_SCAN_UNREADABLE : STORAGE_CATALOG_SCAN_EVENT_PENDING;
}

/**
 * @brief 扫描已挂载 SD 的 `Music/`，写入 Music Catalog，成功后生成顺序播放列表。
 * @return STORAGE_OK 扫描完成（含空目录或表满截断），或卷在扫描期间不可用而丢弃了本次扫描（曲库保持无效）；
 *         STORAGE_ERROR 读取、关闭或建表失败。
 * @note 仅 Storage Task 在卷就绪（卷事件）之后调用。不 memset 整池。
 */
Storage_StatusTypeDef storage_catalog_music_init(void)
{
    storage_catalog_music_reset();
    storage_catalog_scan_clock_start();
    music_catalog_scan_aborted = false;
    if (storage_catalog_music_dfs() != STORAGE_OK)
    {
        if (music_catalog_scan_aborted)
        {
            /* 扫描期间卷消失或正在对账：丢弃已收录的条目，不发布播放列表，曲库保持无效，等卷事件重新扫描。
             * 这不是文件错误（#195 第 9 条、#196），所以返回成功、不记 ERROR。 */
            storage_catalog_music_reset();
            storage_catalog_scan_log_end("aborted");
            return STORAGE_OK;
        }

        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_catalog_log_tag,
                               "Music catalog dfs failed.");
        storage_catalog_scan_log_end("dfs_error");
        return STORAGE_ERROR;
    }

    if (storage_sheet_init(MusicCatalogPool.IndexNum, MusicCatalogPool.Generation) != STORAGE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_catalog_log_tag,
                               "Music sheet initialization failed.");
        storage_catalog_scan_log_end("sheet_error");
        return STORAGE_ERROR;
    }

    storage_catalog_scan_log_end("ok");

    return STORAGE_OK;
}

/**
 * @brief 预留的 Books Catalog 初始化。
 * @return 当前尚未扫描，返回 STORAGE_OK。
 */
Storage_StatusTypeDef storage_catalog_books_init(void)
{
    return STORAGE_OK;
}

/**
 * @brief 按产品卷顺序重建 Catalog。
 * @return 任一子库失败则 STORAGE_ERROR。
 */
Storage_StatusTypeDef storage_catalog_init(void)
{
    if (storage_catalog_music_init() != STORAGE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_catalog_log_tag,
                               "Music catalog initialization failed.");
        return STORAGE_ERROR;
    }

    // if (storage_catalog_books_init() != STORAGE_OK)
    // {
    //     (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
    //                            storage_catalog_log_tag,
    //                            "Books catalog initialization failed.");
    //     return STORAGE_ERROR;
    // }

    return STORAGE_OK;
}

/**
 * @brief 拔卡或卸载时作废 Catalog，并作废对应的播放列表。
 * @return STORAGE_OK。
 */
Storage_StatusTypeDef storage_catalog_invalidate(void)
{
    storage_catalog_music_reset();
    return storage_sheet_invalidate();
}

/**
 * @brief 返回当前 Catalog 代次（含空表递增后的值）。
 */
uint32_t storage_catalog_generation(void)
{
    return MusicCatalogPool.Generation;
}

/**
 * @brief 返回已收录曲目数。
 */
uint16_t storage_catalog_index_num(void)
{
    return MusicCatalogPool.IndexNum;
}

/**
 * @brief 把指定 Catalog 下标的 UTF-8 相对路径拷进调用方缓冲。
 */
Storage_StatusTypeDef storage_catalog_copy_path(uint16_t catalog_index,
                                                char *out,
                                                uint32_t out_bytes)
{
    uint32_t length;

    if ((out == NULL) || (out_bytes == 0U) ||
        (catalog_index >= MusicCatalogPool.IndexNum))
    {
        return STORAGE_ERROR;
    }

    length = MusicCatalogIndex[catalog_index].Length;
    if ((length + 1U) > out_bytes)
    {
        return STORAGE_ERROR;
    }

    (void)memcpy(out,
                 &MusicCatalogPool.Path[MusicCatalogIndex[catalog_index].Offset],
                 length + 1U);
    return STORAGE_OK;
}

/**
 * @brief 把指定 Catalog 下标的文件定位符拷给调用方。
 * @param[in] catalog_index Catalog 下标。
 * @param[out] out 接收定位符，不得为空。
 * @return true 下标在当前曲库内（定位符可能全零，按它打开会核对不符并退回路径）；
 *         false 下标越界或曲库已作废。
 * @note  定位符只在同一曲库代次内有效；调用者须像取路径一样，先核对 Sheet 与 Catalog 代次一致。
 */
bool storage_catalog_copy_locator(uint16_t catalog_index,
                                  Service_Filesystem_FileLocatorTypeDef *out)
{
    if ((out == NULL) || (catalog_index >= MusicCatalogPool.IndexNum))
    {
        return false;
    }

    *out = MusicCatalogLocator[catalog_index];
    return true;
}
