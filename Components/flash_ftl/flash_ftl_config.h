/**
 * @file flash_ftl_config.h
 * @brief FTL 私有格式与策略配置；容量变更需要显式兼容性检查。
 */

#ifndef FLASH_FTL_CONFIG_H
#define FLASH_FTL_CONFIG_H

/** @brief 分母为扣除两卷头后的物理数据块，向上取整；不是固定地址保留区。 */
#define FLASH_FTL_RESERVE_PERCENT 10U
/** @brief 分母为预留块预算：空闲数 <= START 启动，>= STOP 停止。 */
#define FLASH_FTL_GC_START_RESERVE_PERCENT 75U
#define FLASH_FTL_GC_STOP_RESERVE_PERCENT  90U
/** @brief 分配不得使确认擦除空闲数小于此值；不足先回收，不能擦当前有效块。 */
#define FLASH_FTL_MIN_FREE_BLOCKS 2U
/** @brief 盘上协议版本；字段解释变化必须同步升级并检查兼容性。 */
#define FLASH_FTL_FORMAT_VERSION 1U
/** @brief 卷描述记录魔数，序列化为小端。 */
#define FLASH_FTL_VOLUME_MAGIC 0x314C4F56UL
/** @brief 组头记录魔数，序列化为小端。 */
#define FLASH_FTL_HEADER_MAGIC 0x31524746UL
/** @brief 最后写入的提交记录魔数，序列化为小端。 */
#define FLASH_FTL_COMMIT_MAGIC 0x31544D43UL
/** @brief 反射 CRC32 多项式；初值和末次异或均为全 1。 */
#define FLASH_FTL_CRC_POLYNOMIAL 0xEDB88320UL
/** @brief 卷格式化准备阶段，不能按可用卷打开。 */
#define FLASH_FTL_PREPARING 1U
/** @brief 该代次数据区格式化完成的就绪阶段。 */
#define FLASH_FTL_READY 2U
/** @brief 页内 CRC 字节偏移，覆盖范围为该偏移之前的字节。 */
#define FLASH_FTL_CRC_OFFSET 252U
/** @brief 4 KiB 记录内提交页首偏移，提交页必须最后编程。 */
#define FLASH_FTL_COMMIT_OFFSET 3840U
/** @brief 一个逻辑组的有效载荷字节数，即七个 512 B 扇区。 */
#define FLASH_FTL_PAYLOAD_BYTES 3584U
/** @brief RAM 映射表无记录哨兵，不是可分配的物理块索引。 */
#define FLASH_FTL_UNMAPPED UINT32_MAX
#endif
