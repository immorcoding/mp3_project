/**
 * @file loader_w25q256_config.h
 * @brief W25Q256 与本板 QSPI 的固定烧录参数。
 */

#ifndef LOADER_W25Q256_CONFIG_H
#define LOADER_W25Q256_CONFIG_H

#include <stdint.h>

/* loader_w25q256.h */
#define LOADER_W25Q256_PAGE_SIZE                   0x100UL   /* 页编程粒度，256 字节。 */
#define LOADER_W25Q256_SECTOR_SIZE                 0x1000UL  /* 扇区擦除粒度，4 KiB。 */
#define LOADER_W25Q256_FLASH_SIZE_POSITION         24U       /* HAL FlashSize 编码：2^(N+1) 字节对应 32 MiB。 */
#define LOADER_W25Q256_JEDEC_MANUFACTURER          0xEFU     /* JEDEC 制造商：Winbond。 */
#define LOADER_W25Q256_JEDEC_CAPACITY              0x19U     /* JEDEC 容量码：256 Mbit。 */

/* loader_w25q256.c */
#define LOADER_W25Q256_COMMAND_WRITE_ENABLE        0x06U     /* 写使能。 */
#define LOADER_W25Q256_COMMAND_READ_STATUS1        0x05U     /* 读取状态寄存器 1。 */
#define LOADER_W25Q256_COMMAND_READ_STATUS2        0x35U     /* 读取状态寄存器 2。 */
#define LOADER_W25Q256_COMMAND_WRITE_STATUS2       0x31U     /* 写入状态寄存器 2。 */
#define LOADER_W25Q256_COMMAND_READ_JEDEC_ID       0x9FU     /* 读取 JEDEC ID。 */
#define LOADER_W25Q256_COMMAND_RESET_ENABLE        0x66U     /* 复位使能。 */
#define LOADER_W25Q256_COMMAND_RESET               0x99U     /* 器件复位。 */
#define LOADER_W25Q256_COMMAND_PAGE_PROGRAM_4BYTE  0x34U     /* 4-byte 地址 Quad 页编程。 */
#define LOADER_W25Q256_COMMAND_SECTOR_ERASE_4BYTE  0x21U     /* 4-byte 地址 4 KiB 擦除。 */
#define LOADER_W25Q256_COMMAND_CHIP_ERASE          0xC7U     /* 整片擦除。 */
#define LOADER_W25Q256_COMMAND_READ_4BYTE_QUAD_IO  0xECU     /* 4-byte 地址 Quad I/O 快读。 */

#define LOADER_W25Q256_STATUS1_WIP                 0x01U     /* SR1 WIP 位掩码。 */
#define LOADER_W25Q256_STATUS1_WEL                 0x02U     /* SR1 WEL 位掩码。 */
#define LOADER_W25Q256_STATUS2_QE                  0x02U     /* SR2 QE 位掩码。 */

#define LOADER_W25Q256_COMMAND_TIMEOUT_TICKS       5000U     /* 普通命令轮询超时，单位为 HAL tick。 */
#define LOADER_W25Q256_PROGRAM_TIMEOUT_TICKS       10U       /* 页编程等待超时，单位为 HAL tick。 */
#define LOADER_W25Q256_ERASE_TIMEOUT_TICKS         1000U     /* 扇区擦除等待超时，单位为 HAL tick。 */
#define LOADER_W25Q256_CHIP_TIMEOUT_TICKS          300000U   /* 整片擦除等待超时，单位为 HAL tick。 */

#endif /* LOADER_W25Q256_CONFIG_H */
