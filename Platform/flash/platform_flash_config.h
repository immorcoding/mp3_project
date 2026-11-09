/**
  ******************************************************************************
  * @file    platform_flash_config.h
  * @brief   当前 PCB W25Q256 QSPI 装配的私有参数。
  ******************************************************************************
  */

#ifndef PLATFORM_FLASH_CONFIG_H
#define PLATFORM_FLASH_CONFIG_H

#include "Components/w25qxx/w25qxx.h"

/* platform_flash.c */
/* 单次阻塞式 HAL QSPI 调用的软件时限，单位为毫秒：JEDEC ID、状态寄存器、SFDP、同步读、写使能与擦除命令，
 * 以及页编程的 `HAL_QSPI_Command` + `HAL_QSPI_Transmit`（256 B）都用它。
 * 为什么是 3000：HAL 在内部忙等标志位，并用墙钟（`HAL_GetTick`）判断超时，而且是先读标志、后读墙钟；
 * 调用者是优先级 1 的后台任务（#220 起 Flash 请求由后台任务和回收作业发起），发 256 B 的中途被更高优先级的任务
 * （GUI 等）抢占，回来时墙钟已过时限，HAL 就把 `hqspi.State` 置成 ERROR 并返回错误（第 6 次上板：
 * `hw=18/1`，被抢占推断在 1.3 s 以上）。而 `HAL_QSPI_Abort` 对 ERROR 态不起作用，QSPI 从此所有命令失败，
 * 直到重启 Flash 不可用、回收也停。所以时限要远大于任务可能被抢占的最长时间。
 * 代价：芯片或总线真的挂死时，同步调用要 3 s 才报错；启动期（调度器启动前，`Platform_Flash_Init` 依次做
 * JEDEC、SFDP、QE、探测读，第一个失败就返回）最坏一次多等 3 s，正常时序不受影响。
 * Service 的单次请求预算必须大于它：`FILESYSTEM_FLASH_RECLAIM_TIMEOUT_MS` 为此从 2000 改成 5000。 */
#define PLATFORM_FLASH_QSPI_TIMEOUT_MS                 3000u
#define PLATFORM_FLASH_EXPECTED_MANUFACTURER_ID        W25QXX_MANUFACTURER_ID_WINBOND  /* 当前 PCB 焊接 Winbond；MemoryType 不参与兼容性判定。 */
#define PLATFORM_FLASH_EXPECTED_CAPACITY_ID            W25QXX_CAPACITY_ID_256MBIT  /* 当前 PCB 焊接 W25Q256。 */

#define PLATFORM_FLASH_DIAGNOSTIC_HEAD_SECTOR_ADDRESS  0x00000000u   /* ADR-0009 低地址 4 KiB 破坏性自检扇区；不得交给 FTL 或资源包。 */
#define PLATFORM_FLASH_DIAGNOSTIC_TAIL_SECTOR_ADDRESS  0x01FFF000u   /* ADR-0009 高地址 4 KiB 破坏性自检扇区；不得交给 FTL 或资源包。 */
#define PLATFORM_FLASH_DIAGNOSTIC_BUFFER_SIZE_BYTES    W25QXX_SECTOR_ERASE_SIZE_BYTES  /* 自检读写缓冲字节数，等于一个 4 KiB 扇区。 */
#define PLATFORM_FLASH_DIAGNOSTIC_HEAD_PATTERN_SEED    0x13579BDFu   /* 低地址自检图样种子，避免与高地址产生相同低位数据。 */
#define PLATFORM_FLASH_DIAGNOSTIC_TAIL_PATTERN_SEED    0x2468ACE0u   /* 高地址自检图样种子，避免与低地址产生相同低位数据。 */

#define PLATFORM_FLASH_MEMORY_MAPPED_BASE_ADDRESS      0x90000000UL  /* STM32H743 QUADSPI AHB 窗口基址。 */
#define PLATFORM_FLASH_MEMORY_MAPPED_SIZE_BYTES        (32UL * 1024UL * 1024UL)  /* 当前 W25Q256 可安全访问的映射窗口字节数。 */

#define PLATFORM_FLASH_FTL_SIZE_BYTES                  (24UL * 1024UL * 1024UL)  /* FTL 物理范围：尾自检区前向低地址预留，4 KiB 对齐。 */
#define PLATFORM_FLASH_FTL_BASE_ADDRESS                (PLATFORM_FLASH_DIAGNOSTIC_TAIL_SECTOR_ADDRESS - PLATFORM_FLASH_FTL_SIZE_BYTES)  /* FTL 分区起始地址，由尾自检区向前推算。 */
#define PLATFORM_FLASH_FTL_MIN_ADDRESS                 0x00401000UL  /* ADR-0009 两镜像槽之后的最低允许地址；更低范围不参与 FTL 擦写。 */
#define PLATFORM_FLASH_FTL_DATA_BLOCKS                 (PLATFORM_FLASH_FTL_SIZE_BYTES / 4096UL - 2UL)  /* 扣除两卷头后的 FTL 数据块数。 */

#if (PLATFORM_FLASH_FTL_SIZE_BYTES % 4096UL) || \
    (PLATFORM_FLASH_FTL_SIZE_BYTES < 128UL * 1024UL) || \
    (PLATFORM_FLASH_FTL_SIZE_BYTES > \
     PLATFORM_FLASH_DIAGNOSTIC_TAIL_SECTOR_ADDRESS - PLATFORM_FLASH_FTL_MIN_ADDRESS)
#error "FTL partition violates reserved regions or erase alignment"
#endif

#endif /* PLATFORM_FLASH_CONFIG_H */
