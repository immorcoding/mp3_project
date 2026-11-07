/**
 * @file filesystem_handle.c
 * @brief 静态文件/目录槽（按卷固定分配）、代次 Token 以及按卷立即失效。
 *
 * @details
 *          本文件不拿锁：调用者（Service 公开函数）已持有所属卷的卷锁。槽按卷固定分配，
 *          所以一个槽只会被一把卷锁保护，两卷的槽互不相干，也不需要保护槽表的第三把锁。
 */

#include "Service/filesystem/filesystem_handle.h"

#include "Service/filesystem/filesystem_config.h"

#include <string.h>

/** @brief Token 低 8 位保存 1 起始槽号，高 24 位保存代次。 */
#define FILESYSTEM_HANDLE_INDEX_MASK 0xFFU
#define FILESYSTEM_HANDLE_GENERATION_SHIFT 8U
#define FILESYSTEM_HANDLE_GENERATION_MAX 0x00FFFFFFU
#define FILESYSTEM_HANDLE_VOLUME_COUNT 2U

/* 每个打开的对象在 FatFs 锁表 Files[] 里占一项，锁表不能先于槽满：不然 f_open 会在槽用尽之前返回
 * FR_TOO_MANY_OPEN_FILES，而且“找空位并占用”这一步会在跨卷时被窄表锁以外的情形挤到。 */
_Static_assert(_FS_LOCK >= (FILESYSTEM_FILE_SLOT_COUNT + FILESYSTEM_DIRECTORY_SLOT_COUNT),
               "FatFs _FS_LOCK must not be smaller than the total number of file and directory slots");
/* 加锁靠 Service 自己的卷锁，不靠 FatFs 的 _FS_REENTRANT（它只保护同一卷，也保护不了 Files[]）。 */
_Static_assert(_FS_REENTRANT == 0, "FatFs _FS_REENTRANT must stay 0; the Filesystem volume locks replace it");
/* Token 的槽号只有 8 位。 */
_Static_assert(FILESYSTEM_FILE_SLOT_COUNT <= FILESYSTEM_HANDLE_INDEX_MASK, "file slot index must fit the Token");
_Static_assert(FILESYSTEM_DIRECTORY_SLOT_COUNT <= FILESYSTEM_HANDLE_INDEX_MASK, "directory slot index must fit the Token");

typedef struct
{
    FIL object;
    uint32_t generation;
    bool in_use;
} filesystem_file_slot_t;

typedef struct
{
    DIR object;
    uint32_t generation;
    bool in_use;
} filesystem_directory_slot_t;

static filesystem_file_slot_t filesystem_file_slots[FILESYSTEM_FILE_SLOT_COUNT];
static filesystem_directory_slot_t filesystem_directory_slots[FILESYSTEM_DIRECTORY_SLOT_COUNT];
static bool filesystem_volume_initialized[FILESYSTEM_HANDLE_VOLUME_COUNT];
/* 挂载标志由所属卷的卷锁保护写入；入口在拿锁之前也读它（一个字节，只作提示），所以声明为 volatile。 */
static volatile bool filesystem_volume_mounted[FILESYSTEM_HANDLE_VOLUME_COUNT];

/**
 * @brief 检查卷枚举是否可作为内部数组下标。
 * @param[in] volume 调用者传入的卷。
 * @return true 表示 SD 或 Flash。
 */
static bool filesystem_handle_volume_valid(Service_Filesystem_VolumeTypeDef volume)
{
    return (volume == SERVICE_FILESYSTEM_VOLUME_SD) ||
           (volume == SERVICE_FILESYSTEM_VOLUME_FLASH);
}

/**
 * @brief 算出某一卷的文件槽区间。
 * @param[in] volume 合法的卷。
 * @param[out] first 区间第一个槽号。
 * @param[out] count 区间槽数。
 */
static void filesystem_handle_file_range(Service_Filesystem_VolumeTypeDef volume,
                                         uint32_t *first,
                                         uint32_t *count)
{
    if (volume == SERVICE_FILESYSTEM_VOLUME_SD)
    {
        *first = 0U;
        *count = FILESYSTEM_SD_FILE_SLOT_COUNT;
    }
    else
    {
        *first = FILESYSTEM_SD_FILE_SLOT_COUNT;
        *count = FILESYSTEM_FLASH_FILE_SLOT_COUNT;
    }
}

/**
 * @brief 算出某一卷的目录槽区间。
 * @param[in] volume 合法的卷。
 * @param[out] first 区间第一个槽号。
 * @param[out] count 区间槽数。
 */
static void filesystem_handle_directory_range(Service_Filesystem_VolumeTypeDef volume,
                                              uint32_t *first,
                                              uint32_t *count)
{
    if (volume == SERVICE_FILESYSTEM_VOLUME_SD)
    {
        *first = 0U;
        *count = FILESYSTEM_SD_DIRECTORY_SLOT_COUNT;
    }
    else
    {
        *first = FILESYSTEM_SD_DIRECTORY_SLOT_COUNT;
        *count = FILESYSTEM_FLASH_DIRECTORY_SLOT_COUNT;
    }
}

/**
 * @brief 把槽号与代次打包为不透明 Token。
 * @param[in] index 0 起始槽号。
 * @param[in] generation 非零代次。
 * @return 可交给调用者的 Token。
 */
static uint32_t filesystem_handle_make_token(uint32_t index, uint32_t generation)
{
    return (generation << FILESYSTEM_HANDLE_GENERATION_SHIFT) | (index + 1U);
}

/**
 * @brief 从 Token 取出槽号。
 * @param[in] token 调用者句柄中的 Token。
 * @param[in] slot_count 对应槽数组长度。
 * @param[out] index 成功时写入 0 起始槽号。
 * @return true Token 低 8 位是合法槽号。
 */
static bool filesystem_handle_parse_index(uint32_t token, uint32_t slot_count, uint32_t *index)
{
    uint32_t packed = token & FILESYSTEM_HANDLE_INDEX_MASK;

    if ((packed == 0U) || (packed > slot_count))
    {
        return false;
    }

    *index = packed - 1U;
    return true;
}

/**
 * @brief 槽号属于哪一卷：区间按卷固定，所以只看槽号。
 * @param[in] index 合法的文件槽号。
 * @return 所属卷。
 */
static Service_Filesystem_VolumeTypeDef filesystem_handle_file_slot_volume(uint32_t index)
{
    return (index < FILESYSTEM_SD_FILE_SLOT_COUNT) ? SERVICE_FILESYSTEM_VOLUME_SD
                                                    : SERVICE_FILESYSTEM_VOLUME_FLASH;
}

/**
 * @brief 槽号属于哪一卷。
 * @param[in] index 合法的目录槽号。
 * @return 所属卷。
 */
static Service_Filesystem_VolumeTypeDef filesystem_handle_directory_slot_volume(uint32_t index)
{
    return (index < FILESYSTEM_SD_DIRECTORY_SLOT_COUNT) ? SERVICE_FILESYSTEM_VOLUME_SD
                                                         : SERVICE_FILESYSTEM_VOLUME_FLASH;
}

/**
 * @brief 从文件 Token 的槽号算出所属卷，用来决定拿哪把卷锁。
 * @param[in] file 调用者传入的句柄。
 * @param[out] volume 成功时写入所属卷。
 * @return true 槽号在范围内；false 表示 Token 不可能有效（含全零 Token）。
 * @note  纯计算，不读槽内容，不要求持锁；槽号合法不代表句柄有效，有效性要拿锁后用 lookup 核对。
 */
bool filesystem_handle_file_volume(Service_Filesystem_FileHandleTypeDef file,
                                   Service_Filesystem_VolumeTypeDef *volume)
{
    uint32_t index;

    if (!filesystem_handle_parse_index(file.Token, FILESYSTEM_FILE_SLOT_COUNT, &index))
    {
        return false;
    }
    *volume = filesystem_handle_file_slot_volume(index);
    return true;
}

/**
 * @brief 从目录 Token 的槽号算出所属卷，用来决定拿哪把卷锁。
 * @param[in] directory 调用者传入的句柄。
 * @param[out] volume 成功时写入所属卷。
 * @return true 槽号在范围内；false 表示 Token 不可能有效。
 */
bool filesystem_handle_directory_volume(Service_Filesystem_DirectoryHandleTypeDef directory,
                                        Service_Filesystem_VolumeTypeDef *volume)
{
    uint32_t index;

    if (!filesystem_handle_parse_index(directory.Token, FILESYSTEM_DIRECTORY_SLOT_COUNT, &index))
    {
        return false;
    }
    *volume = filesystem_handle_directory_slot_volume(index);
    return true;
}

/**
 * @brief 递增槽代次；耗尽后不再复用该槽。
 * @param[in,out] generation 槽内代次。
 * @return true 仍可分配新 Token。
 */
static bool filesystem_handle_bump_generation(uint32_t *generation)
{
    if (*generation >= FILESYSTEM_HANDLE_GENERATION_MAX)
    {
        return false;
    }

    *generation += 1U;
    return true;
}

/**
 * @brief 卸载或恢复时丢弃该卷全部打开对象，但保留代次以免旧句柄命中新对象。
 * @param[in] volume 要失效的卷。
 */
static void filesystem_handle_invalidate_volume(Service_Filesystem_VolumeTypeDef volume)
{
    uint32_t first;
    uint32_t count;

    filesystem_handle_file_range(volume, &first, &count);
    for (uint32_t index = first; index < (first + count); index++)
    {
        filesystem_file_slots[index].in_use = false;
        memset(&filesystem_file_slots[index].object, 0, sizeof(FIL));
    }

    filesystem_handle_directory_range(volume, &first, &count);
    for (uint32_t index = first; index < (first + count); index++)
    {
        filesystem_directory_slots[index].in_use = false;
        memset(&filesystem_directory_slots[index].object, 0, sizeof(DIR));
    }
}

void filesystem_handle_set_volume_initialized(Service_Filesystem_VolumeTypeDef volume, bool ready)
{
    if (filesystem_handle_volume_valid(volume))
    {
        filesystem_volume_initialized[volume] = ready;
    }
}

void filesystem_handle_set_mounted(Service_Filesystem_VolumeTypeDef volume, bool mounted)
{
    if (!filesystem_handle_volume_valid(volume))
    {
        return;
    }

    filesystem_volume_mounted[volume] = mounted;
    if (!mounted)
    {
        filesystem_handle_invalidate_volume(volume);
    }
}

/**
 * @brief 卷是否标为已挂载。
 * @param[in] volume 目标卷。
 * @return true 已挂载；卷非法为 false。
 * @note  不要求持锁：只写入发生在持卷锁时，读是一个字节。拿锁之前读到的值只是提示（入口随后在锁内再确认一次），
 *        持卷锁时读到的值才是依据。
 */
bool filesystem_handle_is_mounted(Service_Filesystem_VolumeTypeDef volume)
{
    return filesystem_handle_volume_valid(volume) && filesystem_volume_mounted[volume];
}

/**
 * @brief 该卷是否还有打开的文件或目录槽。
 * @param[in] volume 目标卷。
 * @return true 该卷至少有一个槽在用；卷非法为 false。
 * @note  调用者已持该卷的卷锁。
 */
bool filesystem_handle_volume_has_open(Service_Filesystem_VolumeTypeDef volume)
{
    uint32_t first;
    uint32_t count;

    if (!filesystem_handle_volume_valid(volume))
    {
        return false;
    }

    filesystem_handle_file_range(volume, &first, &count);
    for (uint32_t index = first; index < (first + count); index++)
    {
        if (filesystem_file_slots[index].in_use)
        {
            return true;
        }
    }

    filesystem_handle_directory_range(volume, &first, &count);
    for (uint32_t index = first; index < (first + count); index++)
    {
        if (filesystem_directory_slots[index].in_use)
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief 检查卷是否可接受文件或目录操作：已初始化（InitSD/InitFlash 成功）且已挂载。
 * @param[in] volume 目标卷。
 * @retval SERVICE_OK 卷已初始化并已挂载。
 * @retval SERVICE_INVALID_PARAM 卷枚举非法。
 * @retval SERVICE_NOT_READY 卷未初始化或未挂载。
 * @note 两个卷用同一条规则，不看调用任务是谁。
 */
Service_StatusTypeDef filesystem_handle_volume_ready(Service_Filesystem_VolumeTypeDef volume)
{
    if (!filesystem_handle_volume_valid(volume))
    {
        return SERVICE_INVALID_PARAM;
    }

    if (!filesystem_volume_initialized[volume])
    {
        return SERVICE_NOT_READY;
    }

    if (!filesystem_volume_mounted[volume])
    {
        return SERVICE_NOT_READY;
    }

    return SERVICE_OK;
}

/**
 * @brief 在该卷的文件槽区间里占一个空槽。
 * @param[in] volume 目标卷。
 * @param[out] object 成功时写入槽内的 FIL（已清零）。
 * @param[out] file 成功时写入新 Token。
 * @retval SERVICE_OK 已占槽。
 * @retval SERVICE_BUSY 该卷的文件槽用尽。
 * @retval SERVICE_NOT_READY 卷未初始化或未挂载。
 * @retval SERVICE_INVALID_PARAM 卷非法或输出参数为空。
 * @retval SERVICE_ERROR 槽的代次耗尽。
 * @note  调用者已持该卷的卷锁。
 */
Service_StatusTypeDef filesystem_handle_acquire_file(Service_Filesystem_VolumeTypeDef volume,
                                                     FIL **object,
                                                     Service_Filesystem_FileHandleTypeDef *file)
{
    uint32_t first;
    uint32_t count;
    Service_StatusTypeDef status = filesystem_handle_volume_ready(volume);

    if (status != SERVICE_OK)
    {
        return status;
    }
    if ((object == NULL) || (file == NULL))
    {
        return SERVICE_INVALID_PARAM;
    }

    filesystem_handle_file_range(volume, &first, &count);
    for (uint32_t index = first; index < (first + count); index++)
    {
        filesystem_file_slot_t *slot = &filesystem_file_slots[index];

        if (!slot->in_use)
        {
            if (!filesystem_handle_bump_generation(&slot->generation))
            {
                return SERVICE_ERROR;
            }
            memset(&slot->object, 0, sizeof(slot->object));
            slot->in_use = true;
            *object = &slot->object;
            file->Token = filesystem_handle_make_token(index, slot->generation);
            return SERVICE_OK;
        }
    }

    return SERVICE_BUSY;
}

/**
 * @brief 核对文件 Token 并取出槽内的 FIL。
 * @param[in] file 调用者传入的句柄。
 * @param[out] object 成功时写入 FIL，否则为 NULL。
 * @retval SERVICE_OK 句柄有效。
 * @retval SERVICE_INVALID_HANDLE 槽号、占用或代次不符。
 * @retval SERVICE_NOT_READY 所属卷未就绪。
 * @note  调用者已持所属卷的卷锁，所以这里的核对不会被别的任务打断。
 */
Service_StatusTypeDef filesystem_handle_lookup_file(Service_Filesystem_FileHandleTypeDef file,
                                                    FIL **object)
{
    uint32_t index;
    filesystem_file_slot_t *slot;
    Service_StatusTypeDef status;

    if (object == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    *object = NULL;
    if (!filesystem_handle_parse_index(file.Token, FILESYSTEM_FILE_SLOT_COUNT, &index))
    {
        return SERVICE_INVALID_HANDLE;
    }

    slot = &filesystem_file_slots[index];
    if (!slot->in_use ||
        (slot->generation != (file.Token >> FILESYSTEM_HANDLE_GENERATION_SHIFT)))
    {
        return SERVICE_INVALID_HANDLE;
    }

    status = filesystem_handle_volume_ready(filesystem_handle_file_slot_volume(index));
    if (status != SERVICE_OK)
    {
        return status;
    }

    *object = &slot->object;
    return SERVICE_OK;
}

void filesystem_handle_release_file(Service_Filesystem_FileHandleTypeDef file)
{
    uint32_t index;
    filesystem_file_slot_t *slot;

    if (!filesystem_handle_parse_index(file.Token, FILESYSTEM_FILE_SLOT_COUNT, &index))
    {
        return;
    }

    slot = &filesystem_file_slots[index];
    if (slot->in_use && (slot->generation == (file.Token >> FILESYSTEM_HANDLE_GENERATION_SHIFT)))
    {
        slot->in_use = false;
        memset(&slot->object, 0, sizeof(slot->object));
    }
}

/**
 * @brief 在该卷的目录槽区间里占一个空槽。
 * @param[in] volume 目标卷。
 * @param[out] object 成功时写入槽内的 DIR（已清零）。
 * @param[out] directory 成功时写入新 Token。
 * @retval SERVICE_OK 已占槽。
 * @retval SERVICE_BUSY 该卷的目录槽用尽。
 * @retval SERVICE_NOT_READY 卷未初始化或未挂载。
 * @retval SERVICE_INVALID_PARAM 卷非法或输出参数为空。
 * @retval SERVICE_ERROR 槽的代次耗尽。
 * @note  调用者已持该卷的卷锁。
 */
Service_StatusTypeDef filesystem_handle_acquire_directory(
    Service_Filesystem_VolumeTypeDef volume,
    DIR **object,
    Service_Filesystem_DirectoryHandleTypeDef *directory)
{
    uint32_t first;
    uint32_t count;
    Service_StatusTypeDef status = filesystem_handle_volume_ready(volume);

    if (status != SERVICE_OK)
    {
        return status;
    }
    if ((object == NULL) || (directory == NULL))
    {
        return SERVICE_INVALID_PARAM;
    }

    filesystem_handle_directory_range(volume, &first, &count);
    for (uint32_t index = first; index < (first + count); index++)
    {
        filesystem_directory_slot_t *slot = &filesystem_directory_slots[index];

        if (!slot->in_use)
        {
            if (!filesystem_handle_bump_generation(&slot->generation))
            {
                return SERVICE_ERROR;
            }
            memset(&slot->object, 0, sizeof(slot->object));
            slot->in_use = true;
            *object = &slot->object;
            directory->Token = filesystem_handle_make_token(index, slot->generation);
            return SERVICE_OK;
        }
    }

    return SERVICE_BUSY;
}

/**
 * @brief 核对目录 Token 并取出槽内的 DIR。
 * @param[in] directory 调用者传入的句柄。
 * @param[out] object 成功时写入 DIR，否则为 NULL。
 * @retval SERVICE_OK 句柄有效。
 * @retval SERVICE_INVALID_HANDLE 槽号、占用或代次不符。
 * @retval SERVICE_NOT_READY 所属卷未就绪。
 * @note  调用者已持所属卷的卷锁。
 */
Service_StatusTypeDef filesystem_handle_lookup_directory(
    Service_Filesystem_DirectoryHandleTypeDef directory,
    DIR **object)
{
    uint32_t index;
    filesystem_directory_slot_t *slot;
    Service_StatusTypeDef status;

    if (object == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    *object = NULL;
    if (!filesystem_handle_parse_index(
            directory.Token, FILESYSTEM_DIRECTORY_SLOT_COUNT, &index))
    {
        return SERVICE_INVALID_HANDLE;
    }

    slot = &filesystem_directory_slots[index];
    if (!slot->in_use ||
        (slot->generation != (directory.Token >> FILESYSTEM_HANDLE_GENERATION_SHIFT)))
    {
        return SERVICE_INVALID_HANDLE;
    }

    status = filesystem_handle_volume_ready(filesystem_handle_directory_slot_volume(index));
    if (status != SERVICE_OK)
    {
        return status;
    }

    *object = &slot->object;
    return SERVICE_OK;
}

void filesystem_handle_release_directory(Service_Filesystem_DirectoryHandleTypeDef directory)
{
    uint32_t index;
    filesystem_directory_slot_t *slot;

    if (!filesystem_handle_parse_index(
            directory.Token, FILESYSTEM_DIRECTORY_SLOT_COUNT, &index))
    {
        return;
    }

    slot = &filesystem_directory_slots[index];
    if (slot->in_use &&
        (slot->generation == (directory.Token >> FILESYSTEM_HANDLE_GENERATION_SHIFT)))
    {
        slot->in_use = false;
        memset(&slot->object, 0, sizeof(slot->object));
    }
}
