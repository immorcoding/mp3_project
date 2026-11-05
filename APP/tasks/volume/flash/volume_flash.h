/**
 ******************************************************************************
 * @file    volume_flash.h
 * @brief   Flash 卷的启动流程：执行器初始化、可选基准、挂载、无文件系统时按 AUTO_FORMAT 格式化、发布卷就绪。
 *
 * @details
 *          `volume_flash_boot()` 是流程本身（纯粹的 Service 调用加日志加发事件，主机可测）；
 *          `volume_flash_task.c` 把它包成一次性的 Flash 启动任务：卷任务创建它、不等它，优先级 2，
 *          约 4 KiB 静态栈放 AXI 新段，跑完在自删前打一行栈水位。这样 `MountFlash`（实测 3.3 至 8.3 s）
 *          不在卷任务的调用栈里跑，插拔卡不用等它（STOR-9）。
 *
 *          挂载或格式化失败、卡上没有 FAT 以外的失败（损坏、不兼容等）都**不发卷事件，只记日志**；
 *          运行中的恢复这次不加：`FormatAndMountFlash` 只有这条启动路径用到，`RecoverAndMountFlash` 目前 APP 没有调用者。
 ******************************************************************************
 */

#ifndef VOLUME_FLASH_H
#define VOLUME_FLASH_H

#include <stdbool.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

bool volume_flash_boot(void);
TaskHandle_t volume_flash_task_create(void);

#endif /* VOLUME_FLASH_H */
