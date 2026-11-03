/**
 * @file filesystem_lock.h
 * @brief Filesystem Module 私有锁：公开入口一次调用拿一次、返回前放（spike，#191）。
 *
 * @details
 *          默认只有一把全局锁，覆盖所有卷、句柄槽表、FatFs 全局状态和两个传输执行器。
 *          定义 `SPIKE_LOCK_PER_VOLUME` 非 0 时改为 SD、Flash 各一把卷锁，外加一把短的“打开表锁”，
 *          包住会碰 FatFs `Files[]` 的调用和 Service 的文件槽/目录槽表。锁顺序固定：卷锁 → 表锁。
 *          `configUSE_RECURSIVE_MUTEXES` 为 0，所以公开函数之间互相调用时必须拆出不加锁的内部版本；
 *          同一任务重复拿同一把锁会死锁，这里用断言挡住。
 */

#ifndef FILESYSTEM_LOCK_H
#define FILESYSTEM_LOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "Service/filesystem/filesystem_types.h"

#if defined(SPIKE_LOCK_PER_VOLUME) && (SPIKE_LOCK_PER_VOLUME != 0)
#define FILESYSTEM_LOCK_PER_VOLUME 1
#else
#define FILESYSTEM_LOCK_PER_VOLUME 0
#endif

void filesystem_lock_init(void);
void filesystem_lock_acquire(Service_Filesystem_VolumeTypeDef volume, const char *operation);
void filesystem_lock_release(Service_Filesystem_VolumeTypeDef volume);

#if FILESYSTEM_LOCK_PER_VOLUME
void filesystem_lock_table_acquire(const char *operation);
void filesystem_lock_table_release(void);
#else
/* 全局一把时表由那一把锁覆盖，表锁不存在。 */
#define filesystem_lock_table_acquire(operation) ((void)(operation))
#define filesystem_lock_table_release() ((void)0)
#endif

#endif /* FILESYSTEM_LOCK_H */
