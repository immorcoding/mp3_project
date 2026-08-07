/**
  ******************************************************************************
  * @file    app_tasks.h
  * @brief   应用任务创建入口和任务资源配置。
  ******************************************************************************
  */

#ifndef APP_TASKS_H
#define APP_TASKS_H

#define CREATE_TASK_STACK_WORDS   128U
#define LOG_TASK_STACK_WORDS      512U
#define STORAGE_TASK_STACK_WORDS  512U
#define OTHER_TASK_STACK_WORDS    256U

#define CREATE_TASK_PRIORITY      0U
#define LOG_TASK_PRIORITY         1U
#define STORAGE_TASK_PRIORITY     2U
#define OTHER_TASK_PRIORITY       1U

void app_tasks_init(void);

#endif /* APP_TASKS_H */
