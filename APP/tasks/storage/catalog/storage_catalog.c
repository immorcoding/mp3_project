/**
 * @file storage_catalog.c
 * @brief 扫描 SD `Music/`，把相对路径写入 SDRAM 字符串池；成功后生成顺序播放列表。
 */

#include "storage_catalog.h"
#include "storage_playback_cursor.h"
#include "storage_sheet.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static StorageCatalog_MusicTypeDef MusicCatalogPool
    __attribute__((section(".storage_catalog"), aligned(32)));

static StorageCatalog_MusicItemsTypeDef MusicCatalogIndex[STORAGE_CATALOG_MUSIC_MAX_NUM]
    __attribute__((section(".storage_catalog"), aligned(32)));

static char storage_catalog_dir_stack[STORAGE_CATALOG_DIR_STACK_MAX]
                                     [SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U]
    __attribute__((section(".storage_catalog"), aligned(32)));

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
 */
static bool storage_catalog_music_file_strcheck(const char *filename)
{
    uint32_t length;

    if (filename == NULL)
    {
        return false;
    }

    length = (uint32_t)strlen(filename);
    if ((length < 4U) || (length > SERVICE_FILESYSTEM_NAME_MAX_BYTES))
    {
        return false;
    }

    if (filename[length - 4U] != '.')
    {
        return false;
    }

    if (((filename[length - 3U] != 'm') && (filename[length - 3U] != 'M')) ||
        ((filename[length - 2U] != 'p') && (filename[length - 2U] != 'P')) ||
        (filename[length - 1U] != '3'))
    {
        return false;
    }

    return true;
}

/**
 * @brief 把一条已拼好的 UTF-8 相对路径写入字符串池。
 * @return true 已收录；false 条数满或池满。
 */
static bool storage_catalog_music_append(const char *path)
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
 * @brief 用路径栈遍历 `Music/`，同时只开一个目录句柄。
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
        Service_StatusTypeDef read_status;

        stack_top--;
        (void)memcpy(current,
                      storage_catalog_dir_stack[stack_top],
                      sizeof(current));

        if (Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD,
                                             current,
                                             &music_directory) != SERVICE_OK)
        {
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

        for (;;)
        {
            Service_Filesystem_DirectoryEntryTypeDef entry;
            char child[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];

            read_status = Service_Filesystem_ReadDirectory(music_directory, &entry);
            if (read_status != SERVICE_OK)
            {
                (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                                       storage_catalog_log_tag,
                                       "Music Directory Read Failed.");
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

            if (!storage_catalog_music_append(child))
            {
                result = STORAGE_OK;
                stack_top = 0U;
                break;
            }
        }

        if (Service_Filesystem_CloseDirectory(music_directory) != SERVICE_OK)
        {
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
 * @brief 扫描已挂载 SD 的 `Music/`，写入 Music Catalog，成功后生成顺序播放列表。
 * @return STORAGE_OK 扫描完成（含空目录或表满截断）；STORAGE_ERROR 读取、关闭或建表失败。
 * @note 仅 Storage Task 在 MountSD 成功之后调用。不 memset 整池。
 */
Storage_StatusTypeDef storage_catalog_music_init(void)
{
    storage_catalog_music_reset();
    if (storage_catalog_music_dfs() != STORAGE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_catalog_log_tag,
                               "Music catalog dfs failed.");
        return STORAGE_ERROR;
    }

    if (storage_sheet_init(MusicCatalogPool.IndexNum, MusicCatalogPool.Generation) != STORAGE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_catalog_log_tag,
                               "Music sheet initialization failed.");
        return STORAGE_ERROR;
    }

    if (storage_playback_cursor_init(MusicCatalogPool.IndexNum,
                                     MusicCatalogPool.Generation) != STORAGE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_catalog_log_tag,
                               "Music playback cursor initialization failed.");
        return STORAGE_ERROR;
    }
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
 * @brief 拔卡或卸载时作废 Catalog，并作废对应的播放列表与游标。
 * @return STORAGE_OK。
 */
Storage_StatusTypeDef storage_catalog_invalidate(void)
{
    storage_catalog_music_reset();
    (void)storage_playback_cursor_invalidate();
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
