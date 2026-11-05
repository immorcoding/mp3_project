/**
 ******************************************************************************
 * @file    filesystem_service.h
 * @brief   FatFs 卷生命周期 Interface 与调用者限制；线程安全（每卷一把锁）。
 ******************************************************************************
 */

#ifndef FILESYSTEM_SERVICE_H
#define FILESYSTEM_SERVICE_H

#include <stdint.h>
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Service/service.h"

Service_StatusTypeDef Service_Filesystem_InitSD(void);
Service_StatusTypeDef Service_Filesystem_MountSD(void);
Service_StatusTypeDef Service_Filesystem_UnmountSD(void);

Service_StatusTypeDef Service_Filesystem_InitFlash(void);
Service_StatusTypeDef Service_Filesystem_MountFlash(void);
Service_StatusTypeDef Service_Filesystem_UnmountFlash(void);
Service_StatusTypeDef Service_Filesystem_FormatAndMountFlash(void);
Service_StatusTypeDef Service_Filesystem_RecoverAndMountFlash(void);
Service_StatusTypeDef Service_Filesystem_ReclaimFlash(void);

/**
 * @brief  登记一个不得调用文件接口的任务（GUI 与播放任务）。
 * @param  task 任务句柄，非空。重复登记同一个句柄无效果。
 * @note   APP 启动时在 Boot Task 里、被登记的任务开始运行之前调用。之后这个任务只要进入任何
 *         Filesystem 公开函数（文件、目录、卷生命周期、Flash 诊断；`Service_Filesystem_GbkToUnicode` 除外），
 *         入口立即 `configASSERT` 停机，Release 也保留，检查在等锁之前。
 *         登记超过 `FILESYSTEM_DENIED_CALLER_MAX` 个同样断言停机。用禁用名单而不是允许名单，
 *         是为了新增任务时共用层不用改。
 */
void Service_Filesystem_DenyCaller(TaskHandle_t task);

#endif /* FILESYSTEM_SERVICE_H */
