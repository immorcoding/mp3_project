/**
 * @file filesystem_handle.c
 * @brief 静态文件/目录槽、代次 Token 以及按卷立即失效。
 *
 * @details
 *          槽表和代次计数是两个卷共用的静态状态。全局一把锁时由那把锁覆盖；每卷一把锁时，
 *          所有对槽表的读写都在“打开表锁”内完成（卷锁 → 表锁的顺序），`FIL` 的 `memset`
 *          在互斥量内而不是关中断内进行。按 Token 操作的公开函数先从 Token 读出所属卷，
 *          拿到卷锁后再次校验 Token（`filesystem_handle_lock_file/directory`）。
 */

#include "Service/filesystem/filesystem_handle.h"

#include "Service/filesystem/filesystem_config.h"
#include "Service/filesystem/filesystem_lock.h"
#include "Service/filesystem/flash/filesystem_flash_transfer.h"

#include <string.h>

/** @brief Token 低 8 位保存 1 起始槽号，高 24 位保存代次。 */
#define FILESYSTEM_HANDLE_INDEX_MASK 0xFFU
#define FILESYSTEM_HANDLE_GENERATION_SHIFT 8U
#define FILESYSTEM_HANDLE_GENERATION_MAX 0x00FFFFFFU
#define FILESYSTEM_HANDLE_VOLUME_COUNT 2U

typedef struct
{
    FIL object;
    uint32_t generation;
    Service_Filesystem_VolumeTypeDef volume;
    bool in_use;
} filesystem_file_slot_t;

typedef struct
{
    DIR object;
    uint32_t generation;
    Service_Filesystem_VolumeTypeDef volume;
    bool in_use;
} filesystem_directory_slot_t;

/* 槽表跨卷共享：每卷一把锁时由打开表锁保护。 */
static filesystem_file_slot_t filesystem_file_slots[FILESYSTEM_FILE_SLOT_COUNT];
static filesystem_directory_slot_t filesystem_directory_slots[FILESYSTEM_DIRECTORY_SLOT_COUNT];
/* 按卷分元素：各自的卷锁保护。 */
static bool filesystem_volume_initialized[FILESYSTEM_HANDLE_VOLUME_COUNT];
static bool filesystem_volume_mounted[FILESYSTEM_HANDLE_VOLUME_COUNT];

/**
 * @brief 检查卷枚举是否可作为内部数组下标。
 * @param[in] volume 调用者传入的卷。
 * @return true 表示 SD 或 Flash。
 */
bool filesystem_handle_volume_is_valid(Service_Filesystem_VolumeTypeDef volume)
{
    return (volume == SERVICE_FILESYSTEM_VOLUME_SD) ||
           (volume == SERVICE_FILESYSTEM_VOLUME_FLASH);
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
 * @note  调用者持有该卷的卷锁；槽表操作在打开表锁内。只处理该卷正在使用的槽：
 *        空闲槽上残留的旧 volume 字段可能已被另一卷的 acquire 复用，不能碰。
 */
static void filesystem_handle_invalidate_volume(Service_Filesystem_VolumeTypeDef volume)
{
    uint32_t index;

    filesystem_lock_table_acquire("SlotInvalidate");
    for (index = 0U; index < FILESYSTEM_FILE_SLOT_COUNT; index++)
    {
        if (filesystem_file_slots[index].in_use && (filesystem_file_slots[index].volume == volume))
        {
            filesystem_file_slots[index].in_use = false;
            memset(&filesystem_file_slots[index].object, 0, sizeof(FIL));
        }
    }

    for (index = 0U; index < FILESYSTEM_DIRECTORY_SLOT_COUNT; index++)
    {
        if (filesystem_directory_slots[index].in_use &&
            (filesystem_directory_slots[index].volume == volume))
        {
            filesystem_directory_slots[index].in_use = false;
            memset(&filesystem_directory_slots[index].object, 0, sizeof(DIR));
        }
    }
    filesystem_lock_table_release();
}

/**
 * @brief 记录卷是否已完成 InitSD。
 * @param[in] volume 目标卷。
 * @param[in] ready 是否就绪。
 * @note  调用者持有该卷的卷锁。
 */
void filesystem_handle_set_volume_initialized(Service_Filesystem_VolumeTypeDef volume, bool ready)
{
    if (filesystem_handle_volume_is_valid(volume))
    {
        filesystem_volume_initialized[volume] = ready;
    }
}

/**
 * @brief 记录卷是否已挂载；置假时同步使该卷全部句柄失效。
 * @param[in] volume 目标卷。
 * @param[in] mounted 是否已挂载。
 * @note  调用者持有该卷的卷锁。
 */
void filesystem_handle_set_mounted(Service_Filesystem_VolumeTypeDef volume, bool mounted)
{
    if (!filesystem_handle_volume_is_valid(volume))
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
 * @brief 查询卷是否已挂载。
 * @param[in] volume 目标卷。
 * @return true 表示已挂载；调用者持有该卷的卷锁。
 */
bool filesystem_handle_is_mounted(Service_Filesystem_VolumeTypeDef volume)
{
    return filesystem_handle_volume_is_valid(volume) && filesystem_volume_mounted[volume];
}

/**
 * @brief 查询该卷是否仍有打开的文件或目录。
 * @param[in] volume 目标卷。
 * @return true 表示至少一个槽正被该卷使用。
 * @note  槽表扫描在打开表锁内。
 */
bool filesystem_handle_volume_has_open(Service_Filesystem_VolumeTypeDef volume)
{
    uint32_t index;
    bool found = false;

    if (!filesystem_handle_volume_is_valid(volume))
    {
        return false;
    }

    filesystem_lock_table_acquire("SlotHasOpen");
    for (index = 0U; !found && (index < FILESYSTEM_FILE_SLOT_COUNT); index++)
    {
        found = filesystem_file_slots[index].in_use &&
                (filesystem_file_slots[index].volume == volume);
    }

    for (index = 0U; !found && (index < FILESYSTEM_DIRECTORY_SLOT_COUNT); index++)
    {
        found = filesystem_directory_slots[index].in_use &&
                (filesystem_directory_slots[index].volume == volume);
    }
    filesystem_lock_table_release();

    return found;
}

/**
 * @brief 检查卷已初始化（SD）或执行器已注册（Flash），且已挂载。
 * @param[in] volume 目标卷。
 * @return SERVICE_OK 为就绪；INVALID_PARAM 为卷枚举无效；NOT_READY 为尚未就绪。
 * @note  调用者持有该卷的卷锁。不再比较调用任务身份（#188 第 3 条）。
 */
Service_StatusTypeDef filesystem_handle_volume_ready(Service_Filesystem_VolumeTypeDef volume)
{
    if (!filesystem_handle_volume_is_valid(volume))
    {
        return SERVICE_INVALID_PARAM;
    }

    if (volume == SERVICE_FILESYSTEM_VOLUME_FLASH)
    {
        if (!filesystem_flash_transfer_is_registered())
        {
            return SERVICE_NOT_READY;
        }
    }
    else if (!filesystem_volume_initialized[volume])
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
 * @brief 在目标卷上占用一个文件槽。
 * @param[in] volume 目标卷；调用者持有它的卷锁。
 * @param[out] object 成功时给出槽内 FIL。
 * @param[out] file 成功时给出 Token。
 * @return OK 为已占用；BUSY 为槽用尽；ERROR 为代次耗尽。
 * @note  找槽、置占用和 `memset(FIL)` 都在打开表锁内。
 */
Service_StatusTypeDef filesystem_handle_acquire_file(Service_Filesystem_VolumeTypeDef volume,
                                                     FIL **object,
                                                     Service_Filesystem_FileHandleTypeDef *file)
{
    uint32_t index;
    Service_StatusTypeDef status = filesystem_handle_volume_ready(volume);

    if (status != SERVICE_OK)
    {
        return status;
    }
    if ((object == NULL) || (file == NULL))
    {
        return SERVICE_INVALID_PARAM;
    }

    status = SERVICE_BUSY;
    filesystem_lock_table_acquire("SlotAcquire");
    for (index = 0U; index < FILESYSTEM_FILE_SLOT_COUNT; index++)
    {
        filesystem_file_slot_t *slot = &filesystem_file_slots[index];

        if (!slot->in_use)
        {
            if (!filesystem_handle_bump_generation(&slot->generation))
            {
                status = SERVICE_ERROR;
                break;
            }
            memset(&slot->object, 0, sizeof(slot->object));
            slot->volume = volume;
            slot->in_use = true;
            *object = &slot->object;
            file->Token = filesystem_handle_make_token(index, slot->generation);
            status = SERVICE_OK;
            break;
        }
    }
    filesystem_lock_table_release();

    return status;
}

/**
 * @brief 从文件 Token 读出它所属的卷，不要求卷已就绪。
 * @param[in] file 调用者持有的文件句柄。
 * @param[out] volume 成功时写入所属卷。
 * @return OK 为 Token 指向一个正在使用的槽；INVALID_HANDLE 为 Token 无效。
 * @note  槽表读取在打开表锁内。读到之后槽可能被另一任务释放，所以调用者拿到卷锁后必须再次校验。
 */
static Service_StatusTypeDef filesystem_handle_file_volume(
    Service_Filesystem_FileHandleTypeDef file,
    Service_Filesystem_VolumeTypeDef *volume)
{
    uint32_t index;
    Service_StatusTypeDef status = SERVICE_INVALID_HANDLE;

    if (!filesystem_handle_parse_index(file.Token, FILESYSTEM_FILE_SLOT_COUNT, &index))
    {
        return SERVICE_INVALID_HANDLE;
    }

    filesystem_lock_table_acquire("SlotVolume");
    if (filesystem_file_slots[index].in_use &&
        (filesystem_file_slots[index].generation == (file.Token >> FILESYSTEM_HANDLE_GENERATION_SHIFT)))
    {
        *volume = filesystem_file_slots[index].volume;
        status = SERVICE_OK;
    }
    filesystem_lock_table_release();

    return status;
}

/**
 * @brief 校验文件 Token 并取出槽内 FIL。
 * @param[in] file 调用者持有的文件句柄。
 * @param[out] object 成功时给出槽内 FIL，失败时为 NULL。
 * @return OK 为有效；INVALID_HANDLE 为 Token 失效；其余为所属卷未就绪。
 * @note  调用者持有该 Token 所属卷的卷锁。槽表读取在打开表锁内。
 */
Service_StatusTypeDef filesystem_handle_lookup_file(Service_Filesystem_FileHandleTypeDef file,
                                                    FIL **object)
{
    Service_Filesystem_VolumeTypeDef volume = SERVICE_FILESYSTEM_VOLUME_SD;
    uint32_t index;
    Service_StatusTypeDef status;

    if (object == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    *object = NULL;

    status = filesystem_handle_file_volume(file, &volume);
    if (status != SERVICE_OK)
    {
        return status;
    }

    status = filesystem_handle_volume_ready(volume);
    if (status != SERVICE_OK)
    {
        return status;
    }

    (void)filesystem_handle_parse_index(file.Token, FILESYSTEM_FILE_SLOT_COUNT, &index);
    *object = &filesystem_file_slots[index].object;
    return SERVICE_OK;
}

/**
 * @brief 按文件 Token 拿所属卷的卷锁，并在锁内再次校验 Token。
 * @param[in] file 调用者持有的文件句柄。
 * @param[in] operation 公开操作名（静态字符串），进入锁统计。
 * @param[out] object 成功时给出槽内 FIL。
 * @param[out] volume 成功时给出所持有的卷锁对应的卷，调用者用它放锁。
 * @return OK 时调用者持有卷锁，必须 `filesystem_lock_release(*volume)`；其余状态不持锁。
 */
Service_StatusTypeDef filesystem_handle_lock_file(Service_Filesystem_FileHandleTypeDef file,
                                                  const char *operation,
                                                  FIL **object,
                                                  Service_Filesystem_VolumeTypeDef *volume)
{
    Service_StatusTypeDef status;

    if ((object == NULL) || (volume == NULL))
    {
        return SERVICE_INVALID_PARAM;
    }
    *object = NULL;

    status = filesystem_handle_file_volume(file, volume);
    if (status != SERVICE_OK)
    {
        return status;
    }

    filesystem_lock_acquire(*volume, operation);
    status = filesystem_handle_lookup_file(file, object);
    if (status != SERVICE_OK)
    {
        filesystem_lock_release(*volume);
    }

    return status;
}

/**
 * @brief 释放文件槽，使 Token 失效。
 * @param[in] file 调用者持有的文件句柄。
 * @note  槽表操作在打开表锁内；调用者持有所属卷的卷锁。
 */
void filesystem_handle_release_file(Service_Filesystem_FileHandleTypeDef file)
{
    uint32_t index;
    filesystem_file_slot_t *slot;

    if (!filesystem_handle_parse_index(file.Token, FILESYSTEM_FILE_SLOT_COUNT, &index))
    {
        return;
    }

    filesystem_lock_table_acquire("SlotRelease");
    slot = &filesystem_file_slots[index];
    if (slot->in_use && (slot->generation == (file.Token >> FILESYSTEM_HANDLE_GENERATION_SHIFT)))
    {
        slot->in_use = false;
        memset(&slot->object, 0, sizeof(slot->object));
    }
    filesystem_lock_table_release();
}

/**
 * @brief 在目标卷上占用一个目录槽。
 * @param[in] volume 目标卷；调用者持有它的卷锁。
 * @param[out] object 成功时给出槽内 DIR。
 * @param[out] directory 成功时给出 Token。
 * @return OK 为已占用；BUSY 为槽用尽；ERROR 为代次耗尽。
 */
Service_StatusTypeDef filesystem_handle_acquire_directory(
    Service_Filesystem_VolumeTypeDef volume,
    DIR **object,
    Service_Filesystem_DirectoryHandleTypeDef *directory)
{
    uint32_t index;
    Service_StatusTypeDef status = filesystem_handle_volume_ready(volume);

    if (status != SERVICE_OK)
    {
        return status;
    }
    if ((object == NULL) || (directory == NULL))
    {
        return SERVICE_INVALID_PARAM;
    }

    status = SERVICE_BUSY;
    filesystem_lock_table_acquire("SlotAcquire");
    for (index = 0U; index < FILESYSTEM_DIRECTORY_SLOT_COUNT; index++)
    {
        filesystem_directory_slot_t *slot = &filesystem_directory_slots[index];

        if (!slot->in_use)
        {
            if (!filesystem_handle_bump_generation(&slot->generation))
            {
                status = SERVICE_ERROR;
                break;
            }
            memset(&slot->object, 0, sizeof(slot->object));
            slot->volume = volume;
            slot->in_use = true;
            *object = &slot->object;
            directory->Token = filesystem_handle_make_token(index, slot->generation);
            status = SERVICE_OK;
            break;
        }
    }
    filesystem_lock_table_release();

    return status;
}

/**
 * @brief 从目录 Token 读出它所属的卷，不要求卷已就绪。
 * @param[in] directory 调用者持有的目录句柄。
 * @param[out] volume 成功时写入所属卷。
 * @return OK 为 Token 指向一个正在使用的槽；INVALID_HANDLE 为 Token 无效。
 */
static Service_StatusTypeDef filesystem_handle_directory_volume(
    Service_Filesystem_DirectoryHandleTypeDef directory,
    Service_Filesystem_VolumeTypeDef *volume)
{
    uint32_t index;
    Service_StatusTypeDef status = SERVICE_INVALID_HANDLE;

    if (!filesystem_handle_parse_index(
            directory.Token, FILESYSTEM_DIRECTORY_SLOT_COUNT, &index))
    {
        return SERVICE_INVALID_HANDLE;
    }

    filesystem_lock_table_acquire("SlotVolume");
    if (filesystem_directory_slots[index].in_use &&
        (filesystem_directory_slots[index].generation ==
         (directory.Token >> FILESYSTEM_HANDLE_GENERATION_SHIFT)))
    {
        *volume = filesystem_directory_slots[index].volume;
        status = SERVICE_OK;
    }
    filesystem_lock_table_release();

    return status;
}

/**
 * @brief 校验目录 Token 并取出槽内 DIR。
 * @param[in] directory 调用者持有的目录句柄。
 * @param[out] object 成功时给出槽内 DIR，失败时为 NULL。
 * @return OK 为有效；INVALID_HANDLE 为 Token 失效；其余为所属卷未就绪。
 * @note  调用者持有该 Token 所属卷的卷锁。
 */
Service_StatusTypeDef filesystem_handle_lookup_directory(
    Service_Filesystem_DirectoryHandleTypeDef directory,
    DIR **object)
{
    Service_Filesystem_VolumeTypeDef volume = SERVICE_FILESYSTEM_VOLUME_SD;
    uint32_t index;
    Service_StatusTypeDef status;

    if (object == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    *object = NULL;

    status = filesystem_handle_directory_volume(directory, &volume);
    if (status != SERVICE_OK)
    {
        return status;
    }

    status = filesystem_handle_volume_ready(volume);
    if (status != SERVICE_OK)
    {
        return status;
    }

    (void)filesystem_handle_parse_index(directory.Token, FILESYSTEM_DIRECTORY_SLOT_COUNT, &index);
    *object = &filesystem_directory_slots[index].object;
    return SERVICE_OK;
}

/**
 * @brief 按目录 Token 拿所属卷的卷锁，并在锁内再次校验 Token。
 * @param[in] directory 调用者持有的目录句柄。
 * @param[in] operation 公开操作名（静态字符串），进入锁统计。
 * @param[out] object 成功时给出槽内 DIR。
 * @param[out] volume 成功时给出所持有的卷锁对应的卷，调用者用它放锁。
 * @return OK 时调用者持有卷锁，必须 `filesystem_lock_release(*volume)`；其余状态不持锁。
 */
Service_StatusTypeDef filesystem_handle_lock_directory(
    Service_Filesystem_DirectoryHandleTypeDef directory,
    const char *operation,
    DIR **object,
    Service_Filesystem_VolumeTypeDef *volume)
{
    Service_StatusTypeDef status;

    if ((object == NULL) || (volume == NULL))
    {
        return SERVICE_INVALID_PARAM;
    }
    *object = NULL;

    status = filesystem_handle_directory_volume(directory, volume);
    if (status != SERVICE_OK)
    {
        return status;
    }

    filesystem_lock_acquire(*volume, operation);
    status = filesystem_handle_lookup_directory(directory, object);
    if (status != SERVICE_OK)
    {
        filesystem_lock_release(*volume);
    }

    return status;
}

/**
 * @brief 释放目录槽，使 Token 失效。
 * @param[in] directory 调用者持有的目录句柄。
 * @note  槽表操作在打开表锁内；调用者持有所属卷的卷锁。
 */
void filesystem_handle_release_directory(Service_Filesystem_DirectoryHandleTypeDef directory)
{
    uint32_t index;
    filesystem_directory_slot_t *slot;

    if (!filesystem_handle_parse_index(
            directory.Token, FILESYSTEM_DIRECTORY_SLOT_COUNT, &index))
    {
        return;
    }

    filesystem_lock_table_acquire("SlotRelease");
    slot = &filesystem_directory_slots[index];
    if (slot->in_use &&
        (slot->generation == (directory.Token >> FILESYSTEM_HANDLE_GENERATION_SHIFT)))
    {
        slot->in_use = false;
        memset(&slot->object, 0, sizeof(slot->object));
    }
    filesystem_lock_table_release();
}
