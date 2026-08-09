/**
  ******************************************************************************
  * @file    app_storage_sd.h
  * @brief   Storage Task 内部的 SD 介质生命周期接口。
  *
  * @details
  *          本模块集中 Platform SD 生命周期、FatFs 挂载状态和相关日志。
  *          调用者只能是 Storage Task；它不向其他应用任务暴露直接访问
  *          文件系统的接口。
  ******************************************************************************
  */

#ifndef APP_STORAGE_SD_H
#define APP_STORAGE_SD_H

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/**
  * @brief  初始化 Platform SD，并处理启动时已经插入的介质。
  * @param  task_handle 当前 Storage Task 的有效任务句柄。
  * @note   本函数会将卡检测 EXTI 回调绑定到 task_handle。若卡已经就绪，
  *         会尝试挂载 FAT 文件系统；未插卡是正常状态。
  */
void storage_sd_init(TaskHandle_t task_handle);

/**
  * @brief  处理已经完成消抖的一次 SD 卡检测事件。
  * @note   仅能由 Storage Task 在普通任务上下文调用。插卡时尝试挂载，
  *         拔卡时注销 FatFs 卷对象；本函数不执行机械消抖。
  */
void storage_sd_process(void);

/**
  * @brief  显式格式化 SD 卡为 FAT32，并在成功后重新挂载。
  * @warning 此操作会销毁 SD 卡上的所有文件，且绝不会由启动或热插拔路径自动调用。
  * @note    调用点必须位于 Storage Task；其他任务未来应通过 Storage Task 的
  *          命令机制提出格式化请求，而不是直接调用本函数。
  */
void storage_sd_format_and_mount(void);

#endif /* APP_STORAGE_SD_H */
