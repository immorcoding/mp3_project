/**
 * @file ram_disk.h
 * @brief 主机用稀疏 RAM 盘：扇区按需分配，统计读扇区次数。
 */

#ifndef RAM_DISK_H
#define RAM_DISK_H

#include <stdint.h>
#include "Middlewares/Third_Party/FatFs/src/ff_gen_drv.h"

/** @brief RAM 盘总扇区数（2 GiB），足以放下 32 KiB 簇的 FAT32 卷。 */
#define RAM_DISK_SECTOR_COUNT (4096UL * 1024UL)

extern const Diskio_drvTypeDef ram_disk_driver;

void ram_disk_reset(void);
uint32_t ram_disk_read_calls(void);
uint32_t ram_disk_sectors_read(void);
void ram_disk_clear_counters(void);

#endif /* RAM_DISK_H */
