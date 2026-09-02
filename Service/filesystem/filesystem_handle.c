/**
 * @file filesystem_handle.c
 * @brief 静态文件/目录槽、代次 Token 以及按卷立即失效。
 */

#include "Service/filesystem/filesystem_handle.h"

#include "Service/filesystem/filesystem_config.h"
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

static filesystem_file_slot_t filesystem_file_slots[FILESYSTEM_FILE_SLOT_COUNT];
static filesystem_directory_slot_t filesystem_directory_slots[FILESYSTEM_DIRECTORY_SLOT_COUNT];
static bool filesystem_volume_initialized[FILESYSTEM_HANDLE_VOLUME_COUNT];
static bool filesystem_volume_mounted[FILESYSTEM_HANDLE_VOLUME_COUNT];

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
 */
static void filesystem_handle_invalidate_volume(Service_Filesystem_VolumeTypeDef volume)
{
    uint32_t index;

    for (index = 0U; index < FILESYSTEM_FILE_SLOT_COUNT; index++)
    {
        if (filesystem_file_slots[index].volume == volume)
        {
            filesystem_file_slots[index].in_use = false;
            memset(&filesystem_file_slots[index].object, 0, sizeof(FIL));
        }
    }

    for (index = 0U; index < FILESYSTEM_DIRECTORY_SLOT_COUNT; index++)
    {
        if (filesystem_directory_slots[index].volume == volume)
        {
            filesystem_directory_slots[index].in_use = false;
            memset(&filesystem_directory_slots[index].object, 0, sizeof(DIR));
        }
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

bool filesystem_handle_is_mounted(Service_Filesystem_VolumeTypeDef volume)
{
    return filesystem_handle_volume_valid(volume) && filesystem_volume_mounted[volume];
}

bool filesystem_handle_volume_has_open(Service_Filesystem_VolumeTypeDef volume)
{
    uint32_t index;

    if (!filesystem_handle_volume_valid(volume))
    {
        return false;
    }

    for (index = 0U; index < FILESYSTEM_FILE_SLOT_COUNT; index++)
    {
        if (filesystem_file_slots[index].in_use &&
            (filesystem_file_slots[index].volume == volume))
        {
            return true;
        }
    }

    for (index = 0U; index < FILESYSTEM_DIRECTORY_SLOT_COUNT; index++)
    {
        if (filesystem_directory_slots[index].in_use &&
            (filesystem_directory_slots[index].volume == volume))
        {
            return true;
        }
    }

    return false;
}

Service_StatusTypeDef filesystem_handle_volume_ready(Service_Filesystem_VolumeTypeDef volume)
{
    if (!filesystem_handle_volume_valid(volume))
    {
        return SERVICE_INVALID_PARAM;
    }

    if (volume == SERVICE_FILESYSTEM_VOLUME_FLASH)
    {
        if (!filesystem_flash_transfer_is_owner())
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

    for (index = 0U; index < FILESYSTEM_FILE_SLOT_COUNT; index++)
    {
        filesystem_file_slot_t *slot = &filesystem_file_slots[index];

        if (!slot->in_use)
        {
            if (!filesystem_handle_bump_generation(&slot->generation))
            {
                return SERVICE_ERROR;
            }
            memset(&slot->object, 0, sizeof(slot->object));
            slot->volume = volume;
            slot->in_use = true;
            *object = &slot->object;
            file->Token = filesystem_handle_make_token(index, slot->generation);
            return SERVICE_OK;
        }
    }

    return SERVICE_BUSY;
}

Service_StatusTypeDef filesystem_handle_lookup_file(Service_Filesystem_FileHandleTypeDef file,
                                                    FIL **object)
{
    uint32_t index;
    filesystem_file_slot_t *slot;

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

    Service_StatusTypeDef status = filesystem_handle_volume_ready(slot->volume);
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

    for (index = 0U; index < FILESYSTEM_DIRECTORY_SLOT_COUNT; index++)
    {
        filesystem_directory_slot_t *slot = &filesystem_directory_slots[index];

        if (!slot->in_use)
        {
            if (!filesystem_handle_bump_generation(&slot->generation))
            {
                return SERVICE_ERROR;
            }
            memset(&slot->object, 0, sizeof(slot->object));
            slot->volume = volume;
            slot->in_use = true;
            *object = &slot->object;
            directory->Token = filesystem_handle_make_token(index, slot->generation);
            return SERVICE_OK;
        }
    }

    return SERVICE_BUSY;
}

Service_StatusTypeDef filesystem_handle_lookup_directory(
    Service_Filesystem_DirectoryHandleTypeDef directory,
    DIR **object)
{
    uint32_t index;
    filesystem_directory_slot_t *slot;

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

    Service_StatusTypeDef status = filesystem_handle_volume_ready(slot->volume);
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
