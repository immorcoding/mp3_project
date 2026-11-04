/**
 * @file filesystem_lock.h
 * @brief Filesystem Module 私有锁：公开入口一次调用拿一次、返回前放（spike，#191、#199）。
 *
 * @details
 *          默认只有一把全局锁，覆盖所有卷、句柄槽表、FatFs 全局状态和两个传输执行器。
 *          定义 `SPIKE_LOCK_PER_VOLUME` 非 0 时改为 SD、Flash 各一把卷锁，外加两把短锁：
 *          - **打开表锁**：保护 FatFs 的全局 `Files[]`。锁顺序固定：卷锁 → 表锁。
 *          - **槽表锁**：只保护 Service 的文件槽、目录槽表（纯内存读写，持有期间不调用任何会等锁的函数），
 *            是最内层的锁，所以不会和打开表锁的数字混在一起（#199）。
 *
 *          打开表锁的范围由 `SPIKE_TABLE_LOCK_NARROW` 决定（#199）：
 *          - 宽（默认）：`f_open`、`f_opendir`、`f_close`、`f_closedir`、`f_unlink`、`f_rename`、`f_mount`
 *            和按定位符打开都在表锁内。
 *          - 窄：只有会走到 FatFs `inc_lock` 的 `f_open`、`f_opendir` 和按定位符打开在表锁内。
 *            依据是读 `ff.c`：跨卷会冲突的只有 `inc_lock` 里“找空位并占用”这一步；`dec_lock`、`clear_lock`
 *            只写本卷的条目，`chk_lock`、`enq_lock` 只读，其他卷的条目 `fs` 不同，比较到第一个字段就跳过；
 *            前提是 `_FS_LOCK` 不小于槽总数，表不会满。
 *
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

#if FILESYSTEM_LOCK_PER_VOLUME && defined(SPIKE_TABLE_LOCK_NARROW) && (SPIKE_TABLE_LOCK_NARROW != 0)
#define FILESYSTEM_LOCK_TABLE_NARROW 1
#else
#define FILESYSTEM_LOCK_TABLE_NARROW 0
#endif

void filesystem_lock_init(void);
void filesystem_lock_acquire(Service_Filesystem_VolumeTypeDef volume, const char *operation);
void filesystem_lock_release(Service_Filesystem_VolumeTypeDef volume);

#if FILESYSTEM_LOCK_PER_VOLUME
/* 会走到 FatFs inc_lock 的调用（f_open、f_opendir、按定位符打开）：宽窄两种范围下都在表锁内。 */
void filesystem_lock_table_acquire(const char *operation);
void filesystem_lock_table_release(void);

/* Service 槽表的短锁，最内层，持有期间不调用任何别的锁。 */
void filesystem_lock_slot_acquire(const char *operation);
void filesystem_lock_slot_release(void);
#else
/* 全局一把时表和槽表由那一把锁覆盖，表锁、槽表锁不存在。 */
#define filesystem_lock_table_acquire(operation) ((void)(operation))
#define filesystem_lock_table_release() ((void)0)
#define filesystem_lock_slot_acquire(operation) ((void)(operation))
#define filesystem_lock_slot_release() ((void)0)
#endif

#if FILESYSTEM_LOCK_PER_VOLUME && !FILESYSTEM_LOCK_TABLE_NARROW
/* 只写本卷条目的调用（f_close、f_closedir、f_unlink、f_rename、f_mount）：只有宽表锁才把它们放进表锁。 */
#define filesystem_lock_table_wide_acquire(operation) filesystem_lock_table_acquire(operation)
#define filesystem_lock_table_wide_release() filesystem_lock_table_release()
#else
#define filesystem_lock_table_wide_acquire(operation) ((void)(operation))
#define filesystem_lock_table_wide_release() ((void)0)
#endif

#endif /* FILESYSTEM_LOCK_H */
