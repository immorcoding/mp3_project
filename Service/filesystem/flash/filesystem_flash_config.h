/**
 * @file filesystem_flash_config.h
 * @brief Flash 执行器私有预算；调整不改变 FTL 持久化格式。
 */
#ifndef FILESYSTEM_FLASH_CONFIG_H
#define FILESYSTEM_FLASH_CONFIG_H

/* filesystem_flash_transfer.c */
#define FILESYSTEM_FLASH_DIAG_LOG_ENABLE                     0U       /* 打开后输出格式版本、代次、块统计及扫描耗时。 */

#define FILESYSTEM_FLASH_WAIT_SLICE_MS                       2U       /* 硬件等待短周期重查，通知丢失时仍能触发超时收尾。 */
#define FILESYSTEM_FLASH_SOFTWARE_YIELD_STEPS                64U      /* 长扫描软件步骤也主动给较低优先级任务一次调度机会。 */

#define FILESYSTEM_FLASH_READ_TIMEOUT_MS                     30000U   /* 单次逻辑扇区读请求的最大等待，单位毫秒。 */
#define FILESYSTEM_FLASH_WRITE_TIMEOUT_MS                    120000U  /* 写请求与 CTRL_SYNC 的最大等待，单位毫秒。 */
#define FILESYSTEM_FLASH_OPEN_TIMEOUT_MS                     120000U  /* 扫描并打开 FTL 卷的最大等待，单位毫秒。 */
#define FILESYSTEM_FLASH_FORMAT_TIMEOUT_MS                   3600000U /* 24 MiB 全擦除约六千次，不是普通写请求预算。 */
#define FILESYSTEM_FLASH_RECOVERY_TIMEOUT_MS                 2000U    /* 等待器件恢复并重扫的最大时间，单位毫秒。 */
#define FILESYSTEM_FLASH_RECLAIM_TIMEOUT_MS                  2000U    /* 单次回收一块的最大等待，单位毫秒。 */

#endif /* FILESYSTEM_FLASH_CONFIG_H */
