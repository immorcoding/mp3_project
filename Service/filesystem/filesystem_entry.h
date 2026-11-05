/**
 * @file filesystem_entry.h
 * @brief Filesystem Module 私有：文件与目录入口共用的“进门”和“出门”序列，以及 SD 的闸（ADR-0024、#195）。
 *
 * @details
 *          文件与目录的每个公开入口按同一顺序进门：
 *          1. 调用者拒绝名单（`filesystem_lock_check_caller`，由入口自己先做，等锁、等闸之前）；
 *          2. 卷没挂就立刻返回，不等闸也不等锁（拿锁前的检查）；
 *          3. SD 卷等闸打开（Flash 不等）；闸等过之后再看一次卷有没有被卸载；
 *          4. 拿所属卷的卷锁；拿到后在锁内再确认一次卷还挂着；
 *          5. 做事，然后 `filesystem_entry_leave` 放锁。
 *          进门失败时返回的状态里调用者不持锁；成功时持锁，必须配一次 `filesystem_entry_leave`。
 *          卷生命周期入口（Init、Mount、Unmount、启动、刷新）不走这里：它们不等闸，也不检查卷是否挂着。
 */

#ifndef FILESYSTEM_ENTRY_H
#define FILESYSTEM_ENTRY_H

#include "Service/filesystem/filesystem_lock.h"
#include "Service/service.h"

Service_StatusTypeDef filesystem_entry_enter(Service_Filesystem_VolumeTypeDef volume,
                                             filesystem_lock_op_t operation,
                                             Service_StatusTypeDef not_mounted_status);
Service_StatusTypeDef filesystem_entry_leave(Service_Filesystem_VolumeTypeDef volume, Service_StatusTypeDef status);

#endif /* FILESYSTEM_ENTRY_H */
