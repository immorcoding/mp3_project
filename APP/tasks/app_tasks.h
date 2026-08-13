/**
  ******************************************************************************
  * @file    app_tasks.h
  * @brief   应用任务创建入口和任务资源配置。
  ******************************************************************************
  */

#ifndef APP_TASKS_H
#define APP_TASKS_H

#define APP_BOOT_TASK_STACK_WORDS      128U
#define APP_LOG_TASK_STACK_WORDS       512U
#define APP_STORAGE_TASK_STACK_WORDS   512U
#define APP_MONITOR_TASK_STACK_WORDS   256U
#define APP_LCD_TASK_STACK_WORDS       256U

#define APP_BOOT_TASK_PRIORITY         0U
#define APP_LOG_TASK_PRIORITY          1U
#define APP_STORAGE_TASK_PRIORITY      2U
#define APP_MONITOR_TASK_PRIORITY      1U
#define APP_LCD_TASK_PRIORITY          1U

void app_task_start(void);

#endif /* APP_TASKS_H */
