/**
 * @file test_filesystem_service.c
 * @brief 经公开 Service Interface 验证卷感知文件/目录行为；RAM 盘替代硬件。
 */

#include "Service/filesystem/filesystem_directory.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/filesystem/filesystem_service.h"

#include "FATFS/App/fatfs.h"
#include "Middlewares/Third_Party/FatFs/src/ff.h"
#include "Middlewares/Third_Party/FatFs/src/ff_gen_drv.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

char SDPath[4];
char USERPath[4];
uint8_t retSD;
uint8_t retUSER;
FATFS SDFatFS;
FATFS USERFatFS;

static bool owner = true;
static bool flash_format_called;
static bool flash_recover_called;

#define RAM_SECTOR_COUNT 2048U
#define RAM_SECTOR_SIZE 512U
static uint8_t ram_media[2][RAM_SECTOR_COUNT * RAM_SECTOR_SIZE];

/**
 * @brief 主机 RAM 盘初始化。
 * @param[in] pdrv 逻辑盘号，仅允许 0 或 1。
 * @return 固定 0 表示就绪。
 */
static DSTATUS ram_initialize(BYTE pdrv)
{
    assert(pdrv < 2U);
    return 0;
}

/**
 * @brief 主机 RAM 盘状态。
 * @param[in] pdrv 逻辑盘号。
 * @return 固定 0 表示就绪。
 */
static DSTATUS ram_status(BYTE pdrv)
{
    return ram_initialize(pdrv);
}

/**
 * @brief 从对应 RAM 盘复制扇区。
 * @param[in] pdrv 逻辑盘号。
 * @param[out] buff 至少 count 个扇区。
 * @param[in] sector 起始扇区。
 * @param[in] count 扇区数。
 * @return RES_OK 或 RES_PARERR。
 */
static DRESULT ram_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if ((pdrv >= 2U) || (buff == NULL) ||
        ((sector + count) > RAM_SECTOR_COUNT))
    {
        return RES_PARERR;
    }
    memcpy(buff, &ram_media[pdrv][sector * RAM_SECTOR_SIZE], count * RAM_SECTOR_SIZE);
    return RES_OK;
}

/**
 * @brief 把扇区写入对应 RAM 盘。
 * @param[in] pdrv 逻辑盘号。
 * @param[in] buff 至少 count 个扇区。
 * @param[in] sector 起始扇区。
 * @param[in] count 扇区数。
 * @return RES_OK 或 RES_PARERR。
 */
static DRESULT ram_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if ((pdrv >= 2U) || (buff == NULL) ||
        ((sector + count) > RAM_SECTOR_COUNT))
    {
        return RES_PARERR;
    }
    memcpy(&ram_media[pdrv][sector * RAM_SECTOR_SIZE], buff, count * RAM_SECTOR_SIZE);
    return RES_OK;
}

/**
 * @brief 提供 mkfs 所需的几何与同步命令。
 * @param[in] pdrv 逻辑盘号。
 * @param[in] cmd ioctl 命令。
 * @param[out] buff 几何输出。
 * @return RES_OK 或 RES_PARERR。
 */
static DRESULT ram_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    assert(pdrv < 2U);
    switch (cmd)
    {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            *(DWORD *)buff = RAM_SECTOR_COUNT;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1U;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = RAM_SECTOR_SIZE;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

static const Diskio_drvTypeDef ram_driver = {
    ram_initialize, ram_status, ram_read, ram_write, ram_ioctl};

bool filesystem_sd_transfer_init(void)
{
    return true;
}

bool filesystem_flash_transfer_init(void)
{
    return true;
}

bool filesystem_flash_transfer_is_owner(void)
{
    return owner;
}

Service_StatusTypeDef filesystem_flash_transfer_open(void)
{
    return SERVICE_OK;
}

Service_StatusTypeDef filesystem_flash_transfer_format(void)
{
    flash_format_called = true;
    memset(ram_media[1], 0, sizeof(ram_media[1]));
    return SERVICE_OK;
}

Service_StatusTypeDef filesystem_flash_transfer_recover(void)
{
    flash_recover_called = true;
    return SERVICE_OK;
}

Service_StatusTypeDef filesystem_flash_transfer_reclaim(void)
{
    return SERVICE_OK;
}

/**
 * @brief 链接双 RAM 盘并初始化两个 Filesystem 后端。
 */
static void setup_volumes(void)
{
    uint8_t work[4096];
    TCHAR sd_drive[] = {'0', ':', 0};
    TCHAR user_drive[] = {'1', ':', 0};

    memset(ram_media, 0, sizeof(ram_media));
    assert(FATFS_LinkDriverEx(&ram_driver, SDPath, 0) == 0);
    assert(FATFS_LinkDriverEx(&ram_driver, USERPath, 1) == 0);
    retSD = 0U;
    retUSER = 0U;
    assert(SDPath[0] == '0');
    assert(USERPath[0] == '1');
    assert(Service_Filesystem_InitSD() == SERVICE_OK);
    assert(Service_Filesystem_InitFlash() == SERVICE_OK);
    assert(f_mkfs(sd_drive, FM_FAT | FM_SFD, 0, work, sizeof(work)) == FR_OK);
    assert(f_mkfs(user_drive, FM_FAT | FM_SFD, 0, work, sizeof(work)) == FR_OK);
    assert(Service_Filesystem_MountSD() == SERVICE_OK);
    assert(Service_Filesystem_MountFlash() == SERVICE_OK);
}

/**
 * @brief Flash tracer：创建、写入、Seek、读回、同步、关闭。
 */
static void test_flash_tracer(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    uint8_t data[] = {1, 2, 3, 4};
    uint8_t readback[4];
    uint32_t transferred;
    uint64_t position;
    uint64_t size;

    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "probe.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_OK);
    assert(Service_Filesystem_WriteFile(file, data, sizeof(data), &transferred) == SERVICE_OK);
    assert(transferred == sizeof(data));
    assert(Service_Filesystem_SeekFile(file, 0U) == SERVICE_OK);
    assert(Service_Filesystem_GetFilePosition(file, &position) == SERVICE_OK && position == 0U);
    assert(Service_Filesystem_ReadFile(file, readback, sizeof(readback), &transferred) ==
           SERVICE_OK);
    assert(transferred == sizeof(readback) && memcmp(readback, data, sizeof(data)) == 0);
    assert(Service_Filesystem_GetFileSize(file, &size) == SERVICE_OK && size == sizeof(data));
    assert(Service_Filesystem_SyncFile(file) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, "probe.bin") ==
           SERVICE_OK);
}

/**
 * @brief SD 与 Flash 使用相同相对路径时内容独立。
 */
static void test_volume_isolation(void)
{
    Service_Filesystem_FileHandleTypeDef sd_file = {0};
    Service_Filesystem_FileHandleTypeDef flash_file = {0};
    const char sd_text[] = "sd";
    const char flash_text[] = "flash";
    char buffer[8];
    uint32_t transferred;

    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_SD, "Music") == SERVICE_OK);
    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_FLASH, "Music") ==
           SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD,
                                       "Music/song.mp3",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &sd_file) == SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "Music/song.mp3",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &flash_file) == SERVICE_OK);
    assert(Service_Filesystem_WriteFile(sd_file, sd_text, 2U, &transferred) == SERVICE_OK);
    assert(Service_Filesystem_WriteFile(flash_file, flash_text, 5U, &transferred) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(sd_file) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(flash_file) == SERVICE_OK);

    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD,
                                       "Music/song.mp3",
                                       SERVICE_FILESYSTEM_FILE_MODE_READ,
                                       &sd_file) == SERVICE_OK);
    assert(Service_Filesystem_ReadFile(sd_file, buffer, sizeof(buffer), &transferred) == SERVICE_OK);
    assert(transferred == 2U && memcmp(buffer, sd_text, 2U) == 0);
    assert(Service_Filesystem_CloseFile(sd_file) == SERVICE_OK);

    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, "Music/song.mp3") ==
           SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "Music/song.mp3",
                                       SERVICE_FILESYSTEM_FILE_MODE_READ,
                                       &flash_file) == SERVICE_OK);
    assert(Service_Filesystem_ReadFile(flash_file, buffer, sizeof(buffer), &transferred) ==
           SERVICE_OK);
    assert(transferred == 5U && memcmp(buffer, flash_text, 5U) == 0);
    assert(Service_Filesystem_CloseFile(flash_file) == SERVICE_OK);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, "Music/song.mp3") ==
           SERVICE_OK);
}

/**
 * @brief 路径契约：拒绝盘符、绝对路径、反斜杠、点分量、非法 UTF-8 和超长路径。
 */
static void test_path_contract(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    char too_long[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 2U];
    const char invalid_utf8[] = {(char)0xC0, (char)0x80, 0};

    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "0:bad",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_INVALID_PARAM);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "/abs",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_INVALID_PARAM);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "a\\b",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_INVALID_PARAM);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       ".",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_INVALID_PARAM);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "..",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_INVALID_PARAM);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "a/../b",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_INVALID_PARAM);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       invalid_utf8,
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_INVALID_PARAM);
    memset(too_long, 'a', sizeof(too_long));
    too_long[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U] = '\0';
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       too_long,
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_INVALID_PARAM);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_INVALID_PARAM);
}

/**
 * @brief 目录创建、读取、rewind、关闭和删除。
 */
static void test_directory(void)
{
    Service_Filesystem_DirectoryHandleTypeDef directory = {0};
    Service_Filesystem_DirectoryEntryTypeDef entry;
    bool saw_notes = false;

    assert(Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_FLASH, "Docs") ==
           SERVICE_OK);
    Service_Filesystem_FileHandleTypeDef file = {0};
    uint32_t transferred;
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "Docs/notes.txt",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_OK);
    assert(Service_Filesystem_WriteFile(file, "x", 1U, &transferred) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);

    assert(Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_FLASH, "Docs", &directory) ==
           SERVICE_OK);
    for (;;)
    {
        assert(Service_Filesystem_ReadDirectory(directory, &entry) == SERVICE_OK);
        if (entry.Name[0] == '\0')
        {
            break;
        }
        if (strcmp(entry.Name, "notes.txt") == 0)
        {
            saw_notes = true;
            assert(!entry.IsDirectory && entry.Size == 1U);
        }
    }
    assert(saw_notes);
    assert(Service_Filesystem_RewindDirectory(directory) == SERVICE_OK);
    assert(Service_Filesystem_ReadDirectory(directory, &entry) == SERVICE_OK);
    assert(entry.Name[0] != '\0');
    assert(Service_Filesystem_CloseDirectory(directory) == SERVICE_OK);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, "Docs/notes.txt") ==
           SERVICE_OK);
    assert(Service_Filesystem_RemoveDirectory(SERVICE_FILESYSTEM_VOLUME_FLASH, "Docs") ==
           SERVICE_OK);
}

/**
 * @brief 卸载后旧句柄失效，另一卷句柄仍可用。
 */
static void test_handle_invalidation(void)
{
    Service_Filesystem_FileHandleTypeDef sd_file = {0};
    Service_Filesystem_FileHandleTypeDef flash_file = {0};
    uint8_t data = 9;
    uint32_t transferred;

    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD,
                                       "keep.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &sd_file) == SERVICE_OK);
    assert(Service_Filesystem_WriteFile(sd_file, &data, 1U, &transferred) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(sd_file) == SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "temp.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &flash_file) == SERVICE_OK);
    assert(Service_Filesystem_UnmountFlash() == SERVICE_OK);
    assert(Service_Filesystem_ReadFile(flash_file, &data, 1U, &transferred) ==
           SERVICE_INVALID_HANDLE);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD,
                                       "keep.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_READ,
                                       &sd_file) == SERVICE_OK);
    assert(Service_Filesystem_ReadFile(sd_file, &data, 1U, &transferred) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(sd_file) == SERVICE_OK);
    assert(Service_Filesystem_MountFlash() == SERVICE_OK);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_SD, "keep.bin") == SERVICE_OK);
}

/**
 * @brief 四种打开方式及已存在文件的错误码。
 */
static void test_file_modes(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    uint8_t payload[] = {1, 2};
    uint8_t extra[] = {3};
    uint8_t buffer[4];
    uint32_t transferred;
    uint64_t size;

    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "mode.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_READ,
                                       &file) == SERVICE_ERROR);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "mode.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_READ_WRITE,
                                       &file) == SERVICE_ERROR);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "mode.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_APPEND,
                                       &file) == SERVICE_ERROR);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "mode.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_OK);
    assert(Service_Filesystem_WriteFile(file, payload, sizeof(payload), &transferred) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "mode.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_BUSY);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "mode.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_APPEND,
                                       &file) == SERVICE_OK);
    assert(Service_Filesystem_WriteFile(file, extra, 1U, &transferred) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "mode.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_READ_WRITE,
                                       &file) == SERVICE_OK);
    assert(Service_Filesystem_GetFileSize(file, &size) == SERVICE_OK && size == 3U);
    assert(Service_Filesystem_SeekFile(file, 0U) == SERVICE_OK);
    assert(Service_Filesystem_ReadFile(file, buffer, 3U, &transferred) == SERVICE_OK);
    assert(transferred == 3U && buffer[0] == 1U && buffer[1] == 2U && buffer[2] == 3U);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, "mode.bin") == SERVICE_OK);
}

/**
 * @brief Rename 与 GetInfo 按卷操作，并校验路径。
 */
static void test_rename_and_info(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    Service_Filesystem_FileInfoTypeDef info;
    uint32_t transferred;

    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "old.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_OK);
    assert(Service_Filesystem_WriteFile(file, "ab", 2U, &transferred) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    assert(Service_Filesystem_RenameFile(SERVICE_FILESYSTEM_VOLUME_FLASH, "../x", "new.bin") ==
           SERVICE_INVALID_PARAM);
    assert(Service_Filesystem_RenameFile(SERVICE_FILESYSTEM_VOLUME_FLASH, "old.bin", "new.bin") ==
           SERVICE_OK);
    assert(Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_FLASH, "new.bin", &info) ==
           SERVICE_OK);
    assert(info.Size == 2U && !info.IsDirectory);
    assert(Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, "new.bin") == SERVICE_OK);
}

/**
 * @brief FormatAndMount / RecoverAndMount 成功后可打开文件；挂载失败不自动格式化。
 */
static void test_flash_lifecycle(void)
{
    Service_Filesystem_FileHandleTypeDef file = {0};
    uint32_t transferred;

    assert(Service_Filesystem_UnmountFlash() == SERVICE_OK);
    memset(ram_media[1], 0, sizeof(ram_media[1]));
    flash_format_called = false;
    assert(Service_Filesystem_MountFlash() == SERVICE_NO_FILESYSTEM);
    assert(!flash_format_called);

    assert(Service_Filesystem_FormatAndMountFlash() == SERVICE_OK);
    assert(flash_format_called);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "after.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_OK);
    assert(Service_Filesystem_WriteFile(file, "z", 1U, &transferred) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);

    flash_recover_called = false;
    assert(Service_Filesystem_RecoverAndMountFlash() == SERVICE_OK);
    assert(flash_recover_called);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "after.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_READ,
                                       &file) == SERVICE_OK);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
}

/**
 * @brief 运行全部卷感知文件/目录切片。
 * @return 断言全部通过返回 0。
 */
int main(void)
{
    setup_volumes();
    test_flash_tracer();
    test_volume_isolation();
    test_path_contract();
    test_directory();
    test_handle_invalidation();
    test_file_modes();
    test_rename_and_info();
    test_flash_lifecycle();
    puts("filesystem_service: volume-aware tests passed");
    return 0;
}
