/**
  ******************************************************************************
  * @file    freertos_hooks.c
  * @brief   本项目启用的 FreeRTOS Hook 函数。
  ******************************************************************************
  */

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/timers.h"

#if (configSUPPORT_STATIC_ALLOCATION == 1)
/**
  * @brief  调度器启动时提供空闲任务的静态内存，不阻塞、不分配堆。
  * @param[out]  task_tcb 接收空闲任务 TCB 地址，由内核在任务存续期间独占使用。
  * @param[out]  task_stack 接收空闲任务栈地址，与 TCB 一同驻留 DTCM .bss。
  * @param[out]  stack_depth 接收以 StackType_t 元素数计的栈深度。
  */
void vApplicationGetIdleTaskMemory(StaticTask_t **task_tcb,
                                  StackType_t **task_stack,
                                  configSTACK_DEPTH_TYPE *stack_depth)
{
    static StaticTask_t idle_task_tcb;
    static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];

    *task_tcb = &idle_task_tcb;
    *task_stack = idle_task_stack;
    *stack_depth = configMINIMAL_STACK_SIZE;
}

#if (configUSE_TIMERS == 1)
/**
  * @brief  调度器启动时提供定时器服务任务的静态内存，不阻塞、不分配堆。
  * @param[out]  task_tcb 接收定时器服务任务 TCB 地址，由内核在任务存续期间独占使用。
  * @param[out]  task_stack 接收定时器服务任务栈地址，与 TCB 一同驻留 DTCM .bss。
  * @param[out]  stack_depth 接收以 StackType_t 元素数计的栈深度。
  */
void vApplicationGetTimerTaskMemory(StaticTask_t **task_tcb,
                                   StackType_t **task_stack,
                                   configSTACK_DEPTH_TYPE *stack_depth)
{
    static StaticTask_t timer_task_tcb;
    static StackType_t timer_task_stack[configTIMER_TASK_STACK_DEPTH];

    *task_tcb = &timer_task_tcb;
    *task_stack = timer_task_stack;
    *stack_depth = configTIMER_TASK_STACK_DEPTH;
}
#endif
#endif

/**
  * @brief  FreeRTOS 检测到任务栈溢出时的停机 Hook。
  * @param[in]  task 发生溢出的任务句柄。
  * @param[in]  task_name 发生溢出的任务名称。
  */
void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;

    taskDISABLE_INTERRUPTS();

    while (1)
    {
    }
}
