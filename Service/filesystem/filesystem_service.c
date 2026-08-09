#include "filesystem_service.h"

#include "FATFS/App/fatfs.h"
#include "FATFS/Target/ffconf.h"

#include "Middlewares/Third_Party/FatFs/src/ff.h"


void Filesystem_ServiceInit(void)
{
    // Initialize the filesystem service
    MX_FATFS_Init();
}

void Filesystem_ServiceMkfs(void)
{
    uint8_t mkfs_work_buffer[1024];
    TCHAR sd_drive_path[sizeof(SDPath)];
    // TCHAR user_drive_path[sizeof(USERPath)];

    for(uint8_t i = 0; i < sizeof(SDPath); i++)
    {
        sd_drive_path[i] = (TCHAR)SDPath[i];
    }

    // for(uint8_t i = 0; i < sizeof(USERPath); i++)
    // {
    //     user_drive_path[i] = (TCHAR)USERPath[i];
    // }

    FRESULT result = f_mkfs(sd_drive_path, FM_FAT32, 0, mkfs_work_buffer, sizeof(mkfs_work_buffer));
    if (result != FR_OK)
    {
        // Handle error
    }

    // result = f_mkfs(user_drive_path, FM_FAT32, 0, mkfs_work_buffer, sizeof(mkfs_work_buffer));
    // if (result != FR_OK)
    // {
    //     // Handle error
    // }
}

void Filesystem_SDMount(void)
{
    f_mount(&SDFatFS, (const TCHAR*)SDPath, 1);
}

// void Filesystem_NorFlashMount(void)
// {
//     f_mount(&USERFatFS, (const TCHAR*)USERPath, 1);
// }
