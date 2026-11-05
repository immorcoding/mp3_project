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
    FILESYSTEM_OP_COUNT     /**< 操作个数，不是操作。 */
} filesystem_lock_op_t;

/**
 * @brief 判断卷枚举是否可作为锁的下标。
 * @param[in] volume 调用者传入的卷。
 * @return true 表示 SD 或 Flash。
 */
bool filesystem_lock_volume_valid(Service_Filesystem_VolumeTypeDef volume);

/**
 * @brief 检查调用者不在拒绝名单里；每个会等锁的公开入口的第一件事，在等锁之前。
 * @note  名单里的任务（GUI、播放）进来就 `configASSERT` 停机，Release 也保留。
 */
void filesystem_lock_check_caller(void);

/**
 * @brief 拿所属卷的卷锁，等到拿到为止。
 * @param[in] volume 目标卷，必须合法（调用前用 `filesystem_lock_volume_valid` 挡掉）。
 * @param[in] operation 本次公开调用的操作，只用于持锁统计。
 * @note  第一次调用时创建三把锁（静态存储，不占堆）。同一任务已持这把锁、已持另一卷的锁、
 *        或正持表锁时断言停机。
 */
void filesystem_lock_take(Service_Filesystem_VolumeTypeDef volume, filesystem_lock_op_t operation);

/**
 * @brief 放所属卷的卷锁；之后在统计开关打开时按节拍投一批汇总日志（锁已放，不在持锁时投递）。
 * @param[in] volume 目标卷。
 * @note  必须由拿锁的任务调用，否则断言停机。
 */
void filesystem_lock_give(Service_Filesystem_VolumeTypeDef volume);

/**
 * @brief 拿窄表锁，等到拿到为止；只能在已持一把卷锁时调用。
 * @param[in] operation 表锁内的打开操作：`FILESYSTEM_OP_OPEN`、`_OPEN_LOCATOR` 或 `_OPEN_DIR`。
 * @note  已持表锁（重复拿）或没有持卷锁（锁序反了）时断言停机。表锁内不得拿任何别的锁。
 */
void filesystem_lock_table_take(filesystem_lock_op_t operation);

/**
 * @brief 放窄表锁。
 * @note  必须由拿锁的任务调用，否则断言停机。
 */
void filesystem_lock_table_give(void);

#endif /* FILESYSTEM_LOCK_H */
