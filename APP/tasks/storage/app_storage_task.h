/**
  ******************************************************************************
  * @file    app_storage_task.h
  * @brief   Storage Task 入口。
  ******************************************************************************
  */

#ifndef APP_STORAGE_TASK_H
#define APP_STORAGE_TASK_H

/**
  * @brief FreeRTOS Storage Task 入口。
  * @param handle 当前未使用，保留为 FreeRTOS TaskFunction_t 规定的参数。
  */
void storage_task(void *handle);

#endif /* APP_STORAGE_TASK_H */
