/**
 ******************************************************************************
 * @file    audio_reader_task.h
 * @brief   音频读取任务：Boot 里的内存准备、静态创建、任务入口。
 *
 * @details
 *          音频读取任务只读文件、填块池，到安全水位后让路，不解码（ADR-0023 决定 4）。播放侧合同（请求信箱、
 *          流记录信箱、块池、唤醒回调）在 `audio_reader_stream.h` 和 `audio_reader_block.h`，字段和语义与 0.9.0 相同。
 *          本头只给 `app_tasks.c`（Boot 任务）用，不是跨模块 Interface。
 ******************************************************************************
 */

#ifndef AUDIO_READER_TASK_H
#define AUDIO_READER_TASK_H

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/* Boot 任务调用一次：初始化块池和两个信箱。做完才创建用到它们的任务（音频读取任务、播放任务）。 */
void audio_reader_boot_init(void);

/* 静态创建音频读取任务（栈在 AXI 新段 .app_axi_ram）；由 app_tasks.c 的 Boot 任务调用。 */
TaskHandle_t audio_reader_task_create(void);

/* 任务入口，只交给 xTaskCreateStatic。 */
void audio_reader_task(void *handle);

#endif /* AUDIO_READER_TASK_H */
