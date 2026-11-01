/**
 * @file ram_disk.c
 * @brief 稀疏 RAM 盘：整个 FAT32 镜像在内存里，等价于“所有目录扇区都命中扇区缓存”。
 *
 * @details
 *          只给 FatFs 的磁盘驱动接口（`Diskio_drvTypeDef`）提供实现；未写过的扇区读出全零。
 *          读扇区次数按调用累加，用来断言“按定位符打开”的读扇区数与文件位置无关。
 */

#include "Tests/filesystem_locator/ram_disk.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define RAM_DISK_SECTOR_SIZE 512U

static uint8_t *ram_disk_sectors[RAM_DISK_SECTOR_COUNT];
static uint32_t ram_disk_calls;
static uint32_t ram_disk_sector_total;

/**
 * @brief 释放全部扇区并清零计数，得到一块空白盘。
 */
void ram_disk_reset(void)
{
    for (uint32_t index = 0U; index < RAM_DISK_SECTOR_COUNT; index++)
    {
        free(ram_disk_sectors[index]);
        ram_disk_sectors[index] = NULL;
    }
    ram_disk_clear_counters();
}

/**
 * @brief 清零读扇区计数。
 */
void ram_disk_clear_counters(void)
{
    ram_disk_calls = 0U;
    ram_disk_sector_total = 0U;
}

/**
 * @brief 读取 `disk_read` 被调用的次数。
 * @return 自上次清零以来的调用次数。
 */
uint32_t ram_disk_read_calls(void)
{
    return ram_disk_calls;
}

/**
 * @brief 读取 `disk_read` 读过的扇区总数。
 * @return 自上次清零以来的扇区数。
 */
uint32_t ram_disk_sectors_read(void)
{
    return ram_disk_sector_total;
}

/**
 * @brief 驱动初始化，恒成功。
 * @param[in] pdrv 物理驱动号（忽略）。
 * @return 0 表示就绪。
 */
static DSTATUS ram_disk_initialize(BYTE pdrv)
{
    (void)pdrv;
    return 0;
}

/**
 * @brief 驱动状态，恒就绪。
 * @param[in] pdrv 物理驱动号（忽略）。
 * @return 0 表示就绪。
 */
static DSTATUS ram_disk_status(BYTE pdrv)
{
    (void)pdrv;
    return 0;
}

/**
 * @brief 读扇区并计数。
 * @param[in] pdrv 物理驱动号（忽略）。
 * @param[out] buff 至少 count 个扇区。
 * @param[in] sector 起始扇区。
 * @param[in] count 扇区数。
 * @return RES_OK 或 RES_PARERR。
 */
static DRESULT ram_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    (void)pdrv;
    if ((buff == NULL) || ((sector + count) > RAM_DISK_SECTOR_COUNT))
    {
        return RES_PARERR;
    }
    ram_disk_calls++;
    ram_disk_sector_total += count;
    for (UINT index = 0U; index < count; index++)
    {
        const uint8_t *source = ram_disk_sectors[sector + index];

        if (source != NULL)
        {
            memcpy(buff + (index * RAM_DISK_SECTOR_SIZE), source, RAM_DISK_SECTOR_SIZE);
        }
        else
        {
            memset(buff + (index * RAM_DISK_SECTOR_SIZE), 0, RAM_DISK_SECTOR_SIZE);
        }
    }
    return RES_OK;
}

/**
 * @brief 写扇区，按需分配。
 * @param[in] pdrv 物理驱动号（忽略）。
 * @param[in] buff 至少 count 个扇区。
 * @param[in] sector 起始扇区。
 * @param[in] count 扇区数。
 * @return RES_OK 或 RES_PARERR。
 */
static DRESULT ram_disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    (void)pdrv;
    if ((buff == NULL) || ((sector + count) > RAM_DISK_SECTOR_COUNT))
    {
        return RES_PARERR;
    }
    for (UINT index = 0U; index < count; index++)
    {
        uint8_t **target = &ram_disk_sectors[sector + index];

        if (*target == NULL)
        {
            *target = malloc(RAM_DISK_SECTOR_SIZE);
            assert(*target != NULL);
        }
        memcpy(*target, buff + (index * RAM_DISK_SECTOR_SIZE), RAM_DISK_SECTOR_SIZE);
    }
    return RES_OK;
}

/**
 * @brief 提供 mkfs 所需的几何与同步命令。
 * @param[in] pdrv 物理驱动号（忽略）。
 * @param[in] cmd ioctl 命令。
 * @param[out] buff 命令输出。
 * @return RES_OK 或 RES_PARERR。
 */
static DRESULT ram_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;
    switch (cmd)
    {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            *(DWORD *)buff = RAM_DISK_SECTOR_COUNT;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1U;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = RAM_DISK_SECTOR_SIZE;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

const Diskio_drvTypeDef ram_disk_driver = {
    ram_disk_initialize, ram_disk_status, ram_disk_read, ram_disk_write, ram_disk_ioctl};
