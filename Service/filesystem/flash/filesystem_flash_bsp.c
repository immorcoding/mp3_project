/**
 * @file filesystem_flash_bsp.c
 * @brief USER DiskIO 契约强定义，连接 Service 私有 Flash 执行器。
 */

#include "FATFS/Target/bsp_driver_user_diskio.h"
#include "Service/filesystem/flash/filesystem_flash_transfer.h"
#include <string.h>

/**
 * @brief 检查 USER 逻辑盘是否就绪，不触发扫描或格式化。
 * @param lun 驱动内编号，当前后端仅使用 0；不是全局卷号。
 * @return 0 表示已就绪；STA_NOINIT 表示后端或调用上下文未就绪。
 * @note 强实现仅 Storage Task 调用；默认弱实现始终返回 STA_NOINIT。
 */
DSTATUS BSP_USER_DISKIO_Init(BYTE lun)
{
    return BSP_USER_DISKIO_GetStatus(lun);
}

/**
 * @brief 查询 USER 后端就绪状态，不访问介质。
 * @param lun 驱动内编号，当前仅支持 0。
 * @return 0 为可访问；STA_NOINIT 为未就绪或非所有者上下文。
 * @note 默认弱定义保持未就绪，不伪造介质存在或成功初始化。
 */
DSTATUS BSP_USER_DISKIO_GetStatus(BYTE lun)
{
    return lun == 0 && filesystem_flash_transfer_is_owner() &&
                   Platform_Flash_GetVolumeState() == PLATFORM_FLASH_VOLUME_READY
               ? 0
               : STA_NOINIT;
}

/**
 * @brief 检查 USER 读写参数、所有者及完整逻辑范围。
 * @param[in] lun 驱动内编号，仅支持 0。
 * @param[in] data 用于非空检查的调用者缓冲，此处不读写其内容。
 * @param[in] lba 起始逻辑扇区号。
 * @param[in] count 非零扇区数。
 * @return RES_OK 表示可发起请求；RES_PARERR 表示参数或范围无效；RES_NOTRDY 表示后端未就绪。
 * @note 使用容量减法判断范围，避免 lba + count 溢出；不触发介质访问。
 */
static DRESULT filesystem_flash_check(BYTE lun, const void *data, DWORD lba, UINT count)
{
    Platform_Flash_VolumeInfoTypeDef info;
    if (lun || !data || !count)
    {
        return RES_PARERR;
    }
    if (BSP_USER_DISKIO_GetStatus(lun))
    {
        return RES_NOTRDY;
    }
    if (Platform_Flash_GetVolumeInfo(&info) != PLATFORM_OK)
    {
        return RES_NOTRDY;
    }
    return lba >= info.SectorCount || count > info.SectorCount - lba ? RES_PARERR : RES_OK;
}

/**
 * @brief 按 FatFs 同步契约读取逻辑扇区。
 * @param lun 驱动内编号，当前仅支持 0。
 * @param data 至少 count * 512 B 输出，返回前保持有效且由调用者独占。
 * @param lba 起始逻辑扇区号。
 * @param count 非零扇区数，完整范围不得越过逻辑容量。
 * @return RES_OK 数据已可用；RES_PARERR 参数无效；RES_NOTRDY 未就绪；
 *         RES_ERROR 表示执行失败，输出可能部分更新。
 * @note 强实现仅 Storage Task 调用，DMA 使用 FTL 内部缓冲；默认弱定义安全失败。
 */
DRESULT BSP_USER_DISKIO_ReadBlocks(BYTE lun, BYTE *data, DWORD lba, UINT count)
{
    DRESULT result = filesystem_flash_check(lun, data, lba, count);
    if (result != RES_OK)
    {
        return result;
    }
    return filesystem_flash_transfer_read(lba, data, count) == SERVICE_OK ? RES_OK : RES_ERROR;
}

/**
 * @brief 按 FatFs 同步契约写入逻辑扇区，成功须等待提交完成。
 * @param lun 驱动内编号，当前仅支持 0。
 * @param data 至少 count * 512 B 输入，返回前保持有效且不改写。
 * @param lba 起始逻辑扇区号。
 * @param count 非零扇区数，完整范围必须有效。
 * @return RES_OK 涉及各组已提交；参数、未就绪和执行失败分别为
 *         RES_PARERR、RES_NOTRDY、RES_ERROR。
 * @note 强实现仅 Storage Task 调用；跨组失败可能部分提交，默认弱定义绝不伪报成功。
 */
DRESULT BSP_USER_DISKIO_WriteBlocks(BYTE lun, const BYTE *data, DWORD lba, UINT count)
{
    DRESULT result = filesystem_flash_check(lun, data, lba, count);
    if (result != RES_OK)
    {
        return result;
    }
    return filesystem_flash_transfer_write(lba, data, count) == SERVICE_OK ? RES_OK : RES_ERROR;
}

/**
 * @brief 提供同步确认和逻辑几何，不隐式格式化或执行 TRIM。
 * @param lun 驱动内编号，当前仅支持 0。
 * @param command CTRL_SYNC、GET_SECTOR_COUNT、GET_SECTOR_SIZE 或 GET_BLOCK_SIZE。
 * @param data 几何输出缓冲：分别为 DWORD、WORD、DWORD；CTRL_SYNC 允许 NULL。
 * @return 强实现按命令返回 RES_OK、RES_PARERR、RES_NOTRDY 或 RES_ERROR；
 *         默认弱定义固定返回 RES_NOTRDY。
 * @note 强实现仅 Storage Task 调用；GET_BLOCK_SIZE 返回 1，不暴露七扇区组约束。
 */
DRESULT BSP_USER_DISKIO_Ioctl(BYTE lun, BYTE command, void *data)
{
    Platform_Flash_VolumeInfoTypeDef info;
    if (lun)
    {
        return RES_PARERR;
    }
    if (BSP_USER_DISKIO_GetStatus(lun))
    {
        return RES_NOTRDY;
    }
    if (command == CTRL_SYNC)
    {
        return filesystem_flash_transfer_sync() == SERVICE_OK ? RES_OK : RES_ERROR;
    }
    if (!data)
    {
        return RES_PARERR;
    }
    if (Platform_Flash_GetVolumeInfo(&info) != PLATFORM_OK)
    {
        return RES_ERROR;
    }
    switch (command)
    {
        case GET_SECTOR_COUNT:
        {
            DWORD value = info.SectorCount;
            memcpy(data, &value, sizeof(value));
            return RES_OK;
        }
        case GET_SECTOR_SIZE:
        {
            WORD value = 512;
            memcpy(data, &value, sizeof(value));
            return RES_OK;
        }
        /* 当前 FatFs 要求 2 的幂；FTL 没有固定逻辑擦除对齐约束，报告 1。 */
        case GET_BLOCK_SIZE:
        {
            DWORD value = 1;
            memcpy(data, &value, sizeof(value));
            return RES_OK;
        }
        default:
            return RES_PARERR;
    }
}
