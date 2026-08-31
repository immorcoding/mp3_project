/**
 * @file filesystem_flash_file.c
 * @brief Storage Task 独占的 Flash 文件访问；FatFs 对象及类型不向 APP 暴露。
 */
#include "Service/filesystem/filesystem_service.h"
#include "Service/filesystem/flash/filesystem_flash_file.h"
#include "Service/filesystem/flash/filesystem_flash_transfer.h"
#include "FATFS/App/fatfs.h"

#include <limits.h>
#include <string.h>

/** @brief 首版仅一个打开文件槽，实际 FIL 及其扇区缓存不占用任务栈。 */
static FIL filesystem_flash_file;
/** @brief 成功挂载后才允许文件访问，不依赖 FatFs 隐式挂载。 */
static bool filesystem_flash_file_mounted;
/** @brief 文件槽是否仍被占用；关闭失败时保留，等待显式重试或卷注销。 */
static bool filesystem_flash_file_open;
/** @brief 每次成功打开递增；卷注销不复位，避免旧句柄命中新文件。 */
static uint32_t filesystem_flash_file_generation;

/**
 * @brief 将 FatFs 文件结果转换为 Service 状态，不泄漏 FRESULT。
 * @param[in] result 文件操作结果。
 * @return OK 为成功；已存在或被锁定为 BUSY；未就绪/超时/无文件系统分别保留，
 *         无效对象或参数为 INVALID_PARAM，其余失败为 ERROR。
 */
static Service_StatusTypeDef filesystem_flash_file_result(FRESULT result)
{
    switch (result)
    {
        case FR_OK:
            return SERVICE_OK;
        case FR_EXIST:
        case FR_LOCKED:
            return SERVICE_BUSY;
        case FR_NOT_READY:
            return SERVICE_NOT_READY;
        case FR_TIMEOUT:
            return SERVICE_TIMEOUT;
        case FR_NO_FILESYSTEM:
            return SERVICE_NO_FILESYSTEM;
        case FR_INVALID_OBJECT:
        case FR_INVALID_NAME:
        case FR_INVALID_PARAMETER:
            return SERVICE_INVALID_PARAM;
        default:
            return SERVICE_ERROR;
    }
}

/**
 * @brief 接收卷挂载状态；注销时丢弃旧文件对象，但不执行隐式同步。
 * @param[in] mounted 已成功挂载为 true，注销或重新挂载前为 false。
 * @note 仅卷生命周期入口在 Storage Task 调用；普通关闭失败不能调用它伪造关闭成功。
 */
void filesystem_flash_file_set_mounted(bool mounted)
{
    filesystem_flash_file_mounted = mounted;
    if (!mounted)
    {
        filesystem_flash_file_open = false;
        memset(&filesystem_flash_file, 0, sizeof(filesystem_flash_file));
    }
}

/**
 * @brief 查询文件槽是否仍占用，避免挂载操作使在用句柄失效。
 * @return true 为存在未成功关闭的文件，false 为空槽。
 * @note 只供 Filesystem Module 的同一 Storage Task 上下文查询。
 */
bool filesystem_flash_file_is_open(void)
{
    return filesystem_flash_file_open;
}

/**
 * @brief 检查文件访问的任务所有权及显式挂载状态。
 * @return true 表示允许访问；false 时不得调用 FatFs 文件 API。
 */
static bool filesystem_flash_file_ready(void)
{
    return filesystem_flash_transfer_is_owner() && filesystem_flash_file_mounted;
}

/**
 * @brief 校验不透明句柄，拒绝关闭、注销或重新打开前的旧代次。
 * @param[in] file 调用者持有的 Service 文件句柄。
 * @return OK 为当前文件；NOT_READY 为上下文或卷无效；INVALID_PARAM 为句柄失效。
 */
static Service_StatusTypeDef filesystem_flash_file_validate(Service_Filesystem_FileTypeDef file)
{
    if (!filesystem_flash_file_ready())
    {
        return SERVICE_NOT_READY;
    }
    return filesystem_flash_file_open && file.Token != 0U &&
                   file.Token == filesystem_flash_file_generation
               ? SERVICE_OK
               : SERVICE_INVALID_PARAM;
}

/**
 * @brief 将 Flash 根目录 ASCII 文件名拼接到实际 USERPath，并转换为 TCHAR。
 * @param[in] name 非空根目录文件名，不接受卷号、目录、通配符或非 ASCII 字符。
 * @param[out] path 至少 FILE_NAME_MAX_BYTES + 4 个 TCHAR 的调用者缓冲。
 * @return true 为转换成功，false 为驱动路径或文件名无效。
 * @note 不硬编码全局卷号；拒绝路径穿越和尾部空格/点，避免名字规范化造成误删。
 */
static bool filesystem_flash_file_path(const char *name, TCHAR *path)
{
    uint32_t length = 0U;
    if (!name || retUSER != 0U || USERPath[0] < '0' || USERPath[0] > '9' || USERPath[1] != ':' ||
        USERPath[2] != '/' || USERPath[3] != '\0')
    {
        return false;
    }
    while (length <= SERVICE_FILESYSTEM_FILE_NAME_MAX_BYTES && name[length])
    {
        unsigned char value = (unsigned char)name[length];
        if (length == SERVICE_FILESYSTEM_FILE_NAME_MAX_BYTES || value <= 0x20U || value >= 0x7FU ||
            strchr("\"*/:<>?\\|", value))
        {
            return false;
        }
        path[length + 3U] = (TCHAR)value;
        length++;
    }
    if (!length || name[0] == '.' || name[length - 1U] == '.')
    {
        return false;
    }
    path[0] = (TCHAR)USERPath[0];
    path[1] = (TCHAR)':';
    path[2] = (TCHAR)'/';
    path[length + 3U] = 0;
    return true;
}

/**
 * @brief 在已挂载 Flash 卷打开一个文件，由 Service 持有实际 FIL。
 * @param[in] name 根目录 ASCII 文件名，最长 FILE_NAME_MAX_BYTES 字节。
 * @param[in] mode READ 只读已有文件；CREATE_NEW 新建可写文件，绝不覆盖同名文件。
 * @param[out] file 仅成功时写入有效句柄，失败时不修改。
 * @return OK 为打开成功；BUSY 为文件槽占用或同名文件已存在；其他状态为打开失败。
 * @note 仅 Storage Task 调用，首版同时一个文件。同步调用可能等待介质；不自动挂载或格式化。
 */
Service_StatusTypeDef Service_Filesystem_OpenFlashFile(const char *name,
                                                       Service_Filesystem_FileModeTypeDef mode,
                                                       Service_Filesystem_FileTypeDef *file)
{
    TCHAR path[SERVICE_FILESYSTEM_FILE_NAME_MAX_BYTES + 4U];
    if (!filesystem_flash_file_ready())
    {
        return SERVICE_NOT_READY;
    }
    if (!file ||
        (mode != SERVICE_FILESYSTEM_FILE_READ && mode != SERVICE_FILESYSTEM_FILE_CREATE_NEW) ||
        !filesystem_flash_file_path(name, path))
    {
        return SERVICE_INVALID_PARAM;
    }
    if (filesystem_flash_file_open)
    {
        return SERVICE_BUSY;
    }
    if (filesystem_flash_file_generation == UINT32_MAX)
    {
        return SERVICE_ERROR;
    }
    BYTE flags = mode == SERVICE_FILESYSTEM_FILE_READ ? FA_READ : FA_CREATE_NEW | FA_WRITE;
    FRESULT result = f_open(&filesystem_flash_file, path, flags);
    if (result == FR_OK)
    {
        filesystem_flash_file_open = true;
        file->Token = ++filesystem_flash_file_generation;
    }
    return filesystem_flash_file_result(result);
}

/**
 * @brief 同步读取当前文件并报告实际字节数。
 * @param[in] file 当前有效的 Service 文件句柄。
 * @param[out] data 至少 length 字节的有效缓冲，返回前保持独占。
 * @param[in] length 非零字节数，不超过 FatFs UINT 可表示范围。
 * @param[out] transferred 实际读取字节数；校验失败时为零，错误时也可能已有部分数据。
 * @return OK 只表示本次读取无错误，短读或零字节可能为 EOF；其他状态表示失败。
 * @note 仅 Storage Task 调用；DMA 使用内部缓冲，APP 缓冲无 DMA 对齐要求。
 */
Service_StatusTypeDef Service_Filesystem_ReadFile(Service_Filesystem_FileTypeDef file,
                                                  void *data,
                                                  uint32_t length,
                                                  uint32_t *transferred)
{
    if (!transferred)
    {
        return SERVICE_INVALID_PARAM;
    }
    *transferred = 0U;
    if (!data || !length || length > UINT_MAX)
    {
        return SERVICE_INVALID_PARAM;
    }
    Service_StatusTypeDef status = filesystem_flash_file_validate(file);
    if (status != SERVICE_OK)
    {
        return status;
    }
    UINT count = 0U;
    FRESULT result = f_read(&filesystem_flash_file, data, (UINT)length, &count);
    *transferred = count;
    return filesystem_flash_file_result(result);
}

/**
 * @brief 同步写入当前文件，保留 FatFs 的实际写入长度语义。
 * @param[in] file 以 CREATE_NEW 打开的有效句柄。
 * @param[in] data 至少 length 字节的输入，返回前保持有效且不改写。
 * @param[in] length 非零字节数，不超过 FatFs UINT 可表示范围。
 * @param[out] transferred 实际写入字节数；即使返回 OK，空间不足也可能产生短写。
 * @return OK 表示 FatFs 未报告错误；其他状态表示失败，失败不代表完全未写入。
 * @note 仅 Storage Task 调用；调用者必须检查字节数，并在需要持久化边界时调用 SyncFile。
 */
Service_StatusTypeDef Service_Filesystem_WriteFile(Service_Filesystem_FileTypeDef file,
                                                   const void *data,
                                                   uint32_t length,
                                                   uint32_t *transferred)
{
    if (!transferred)
    {
        return SERVICE_INVALID_PARAM;
    }
    *transferred = 0U;
    if (!data || !length || length > UINT_MAX)
    {
        return SERVICE_INVALID_PARAM;
    }
    Service_StatusTypeDef status = filesystem_flash_file_validate(file);
    if (status != SERVICE_OK)
    {
        return status;
    }
    UINT count = 0U;
    FRESULT result = f_write(&filesystem_flash_file, data, (UINT)length, &count);
    *transferred = count;
    return filesystem_flash_file_result(result);
}

/**
 * @brief 同步文件缓存、目录元数据及底层逻辑盘。
 * @param[in] file 当前有效句柄。
 * @return OK 表示 f_sync 完成；其他状态表示校验、介质或同步失败。
 * @note 仅 Storage Task 同步等待；不能将跨组 FatFs 更新视为掉电原子事务。
 */
Service_StatusTypeDef Service_Filesystem_SyncFile(Service_Filesystem_FileTypeDef file)
{
    Service_StatusTypeDef status = filesystem_flash_file_validate(file);
    return status == SERVICE_OK ? filesystem_flash_file_result(f_sync(&filesystem_flash_file))
                                : status;
}

/**
 * @brief 同步并关闭文件，仅在成功后释放槽及清零调用者句柄。
 * @param[in,out] file 当前有效句柄；关闭失败时保留以供重试或显式卷恢复。
 * @return OK 表示已关闭；其他状态表示失败，不得据此假设文件锁已释放。
 * @note 仅 Storage Task 调用；介质错误导致不能关闭时，记录错误并走既有显式恢复流程。
 */
Service_StatusTypeDef Service_Filesystem_CloseFile(Service_Filesystem_FileTypeDef *file)
{
    if (!file)
    {
        return SERVICE_INVALID_PARAM;
    }
    Service_StatusTypeDef status = filesystem_flash_file_validate(*file);
    if (status != SERVICE_OK)
    {
        return status;
    }
    status = filesystem_flash_file_result(f_close(&filesystem_flash_file));
    if (status == SERVICE_OK)
    {
        filesystem_flash_file_open = false;
        file->Token = 0U;
    }
    return status;
}

/**
 * @brief 删除 Flash 根目录文件并同步文件系统元数据，不执行物理安全擦除。
 * @param[in] name 需删除的根目录 ASCII 文件名；调用者负责确认该文件属于自身。
 * @return OK 为已删除或文件已不存在；BUSY 为存在未关闭文件；其他状态为删除失败。
 * @note 仅 Storage Task 调用；先关闭文件。无 TRIM 时只释放 FAT 簇，后续重写 LBA
 *       才使旧 FTL 版本失效；不按文件地址直接擦 NOR，也不自动格式化或恢复卷。
 */
Service_StatusTypeDef Service_Filesystem_RemoveFlashFile(const char *name)
{
    TCHAR path[SERVICE_FILESYSTEM_FILE_NAME_MAX_BYTES + 4U];
    if (!filesystem_flash_file_ready())
    {
        return SERVICE_NOT_READY;
    }
    if (!filesystem_flash_file_path(name, path))
    {
        return SERVICE_INVALID_PARAM;
    }
    if (filesystem_flash_file_open)
    {
        return SERVICE_BUSY;
    }
    static FILINFO info; /* 长文件名输出不占用 Storage Task 栈；调用已被单任务串行化。 */
    FRESULT result = f_stat(path, &info);
    if (result == FR_NO_FILE)
    {
        return SERVICE_OK;
    }
    if (result != FR_OK)
    {
        return filesystem_flash_file_result(result);
    }
    if (info.fattrib & AM_DIR)
    {
        return SERVICE_INVALID_PARAM;
    }
    return filesystem_flash_file_result(f_unlink(path));
}
