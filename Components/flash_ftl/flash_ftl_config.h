/**
 * @file flash_ftl_config.h
 * @brief FTL 私有格式与策略配置；容量变更需要显式兼容性检查。
 */

#ifndef FLASH_FTL_CONFIG_H
#define FLASH_FTL_CONFIG_H

#include <stdint.h>

/* flash_ftl.h */
#define FLASH_FTL_SECTOR_BYTES              512U          /* FatFs 看到的逻辑扇区字节数。 */
#define FLASH_FTL_BLOCK_BYTES               4096U         /* 一个组版本独占的物理块字节数。 */
#define FLASH_FTL_PAGE_BYTES                256U          /* 元数据与原始编程的页字节数。 */
#define FLASH_FTL_GROUP_SECTORS             7U            /* 同一组版本共同提交的逻辑扇区数。 */

/* flash_ftl.c */
#define FLASH_FTL_RESERVE_PERCENT           10U           /* 分母为扣除两卷头后的物理数据块，向上取整；不是固定地址保留区。 */
#define FLASH_FTL_GC_START_RESERVE_PERCENT  75U           /* 分母为预留块预算：空闲数 <= START 启动回收。 */
#define FLASH_FTL_GC_STOP_RESERVE_PERCENT   90U           /* 分母为预留块预算：空闲数 >= STOP 停止回收。 */
#define FLASH_FTL_MIN_FREE_BLOCKS           2U            /* 分配不得使确认擦除空闲数小于此值；不足先回收，不能擦当前有效块。 */

#define FLASH_FTL_FORMAT_VERSION            1U            /* 盘上协议版本；字段解释变化必须同步升级并检查兼容性。 */
#define FLASH_FTL_VOLUME_MAGIC              0x314C4F56UL  /* 卷描述记录魔数，序列化为小端。 */
#define FLASH_FTL_HEADER_MAGIC              0x31524746UL  /* 组头记录魔数，序列化为小端。 */
#define FLASH_FTL_COMMIT_MAGIC              0x31544D43UL  /* 最后写入的提交记录魔数，序列化为小端。 */
#define FLASH_FTL_CRC_POLYNOMIAL            0xEDB88320UL  /* 反射 CRC32 多项式；初值和末次异或均为全 1。 */

#define FLASH_FTL_PREPARING                 1U            /* 卷格式化准备阶段，不能按可用卷打开。 */
#define FLASH_FTL_READY                     2U            /* 该代次数据区格式化完成的就绪阶段。 */

#define FLASH_FTL_CRC_OFFSET                252U          /* 页内 CRC 字节偏移，覆盖范围为该偏移之前的字节。 */
#define FLASH_FTL_COMMIT_OFFSET             3840U         /* 4 KiB 记录内提交页首偏移，提交页必须最后编程。 */
#define FLASH_FTL_PAYLOAD_BYTES             3584U         /* 一个逻辑组的有效载荷字节数，即七个 512 B 扇区。 */
#define FLASH_FTL_UNMAPPED                  UINT32_MAX    /* RAM 映射表无记录哨兵，不是可分配的物理块索引。 */

#endif /* FLASH_FTL_CONFIG_H */
