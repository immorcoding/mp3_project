/**
 * @file filesystem_lock.h
 * @brief Filesystem Module 私有：每卷一把卷锁、窄表锁、调用者拒绝名单与持锁统计（ADR-0024、ARC-21）。
 *
 * @details
 *          规则（实现见 filesystem_lock.c）：
 *          - 每个公开函数在入口拿所属卷的锁一次、返回前放一次，中途不放；公开函数之间不互相调用，
 *            共用部分写成不加锁的内部函数，所以锁不需要递归。
 *          - 锁序固定：卷锁 → 表锁。表锁只在 `f_open`、`f_opendir` 和按定位符打开的 `f_open_dirent` 外面拿，
 *            表锁内不拿任何别的锁。绝不同时持两把卷锁，跨卷操作由调用者分成多次调用。
 *          - 同一任务重复拿同一把锁、同时持两把卷锁、持表锁时再拿卷锁，都是 `configASSERT` 停机。
 *          - 拒绝名单的检查在等锁之前。
 */

#ifndef FILESYSTEM_LOCK_H
#define FILESYSTEM_LOCK_H

#include <stdbool.h>

#include "Service/filesystem/filesystem_types.h"

/**
 * @brief 持锁统计里的“操作”：公开入口的短名。Release 构建不用名字，枚举值只是一个常数。
 * @note  名字表在 filesystem_lock.c，须与枚举同序；汇总日志里按名字输出（至多 9 个字符）。
 */
typedef enum
{
    FILESYSTEM_OP_NONE = 0, /**< 没有操作（锁空闲，或没有记录）。 */
    FILESYSTEM_OP_OPEN,     /**< `OpenFile`：按路径打开文件（卷锁里再拿表锁）。 */
    FILESYSTEM_OP_OPEN_LOCATOR, /**< `OpenFileByLocator`：按定位符打开文件（卷锁里再拿表锁）。 */
    FILESYSTEM_OP_OPEN_DIR, /**< `OpenDirectory`（卷锁里再拿表锁）。 */
    FILESYSTEM_OP_READ,     /**< `ReadFile`。 */
    FILESYSTEM_OP_WRITE,    /**< `WriteFile`。 */
    FILESYSTEM_OP_SEEK,     /**< `SeekFile`。 */
    FILESYSTEM_OP_TELL,     /**< `GetFilePosition`。 */
    FILESYSTEM_OP_SIZE,     /**< `GetFileSize`。 */
    FILESYSTEM_OP_SYNC,     /**< `SyncFile`。 */
    FILESYSTEM_OP_CLOSE,    /**< `CloseFile`。 */
    FILESYSTEM_OP_REMOVE,   /**< `RemoveFile`。 */
    FILESYSTEM_OP_RENAME,   /**< `RenameFile`。 */
    FILESYSTEM_OP_INFO,     /**< `GetFileInfo`。 */
    FILESYSTEM_OP_READ_DIR, /**< `ReadDirectory`。 */
    FILESYSTEM_OP_REWIND,   /**< `RewindDirectory`。 */
    FILESYSTEM_OP_CLOSE_DIR, /**< `CloseDirectory`。 */
    FILESYSTEM_OP_MKDIR,    /**< `CreateDirectory`。 */
    FILESYSTEM_OP_RMDIR,    /**< `RemoveDirectory`。 */
    FILESYSTEM_OP_INIT,     /**< `InitSD`、`InitFlash`。 */
    FILESYSTEM_OP_MOUNT,    /**< `MountSD`、`MountFlash`。 */
    FILESYSTEM_OP_UNMOUNT,  /**< `UnmountSD`、`UnmountFlash`。 */
    FILESYSTEM_OP_FORMAT,   /**< `FormatAndMountFlash`。 */
    FILESYSTEM_OP_RECOVER,  /**< `RecoverAndMountFlash`。 */
    FILESYSTEM_OP_RECLAIM,  /**< `ReclaimFlash`。 */
    FILESYSTEM_OP_FLASH_DIAG, /**< Flash 启动诊断的四个同步包装。 */
    FILESYSTEM_OP_START,    /**< `StartSD`：注册卡检测回调并初始化设备。 */
    FILESYSTEM_OP_REFRESH,  /**< `RefreshSD`：在 SD 卷锁内刷新卡的现状。 */
    FILESYSTEM_OP_COUNT     /**< 操作个数，不是操作。 */
} filesystem_lock_op_t;

bool filesystem_lock_volume_valid(Service_Filesystem_VolumeTypeDef volume);
void filesystem_lock_check_caller(void);
void filesystem_lock_assert_caller_holds_none(void);
void filesystem_lock_take(Service_Filesystem_VolumeTypeDef volume, filesystem_lock_op_t operation);
void filesystem_lock_give(Service_Filesystem_VolumeTypeDef volume);
void filesystem_lock_table_take(filesystem_lock_op_t operation);
void filesystem_lock_table_give(void);

#endif /* FILESYSTEM_LOCK_H */
