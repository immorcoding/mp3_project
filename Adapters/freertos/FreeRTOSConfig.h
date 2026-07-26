#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#define configUSE_PREEMPTION                                        1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION                     0
#define configUSE_TICKLESS_IDLE                                     0
#define configCPU_CLOCK_HZ                                          480000000
#define configTICK_RATE_HZ                                          1000
#define configMAX_PRIORITIES                                        5
#define configMINIMAL_STACK_SIZE                                    128
#define configMAX_TASK_NAME_LEN                                     16
#define configUSE_16_BIT_TICKS                                      0
#define configIDLE_SHOULD_YIELD                                     1
#define configUSE_TASK_NOTIFICATIONS                                1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES                       3
#define configUSE_MUTEXES                                           1
#define configUSE_RECURSIVE_MUTEXES                                 0
#define configUSE_COUNTING_SEMAPHORES                               1
#define configUSE_ALTERNATIVE_API                                   0 /* Deprecated! */
#define configQUEUE_REGISTRY_SIZE                                   10
#define configUSE_QUEUE_SETS                                        0
#define configUSE_TIME_SLICING                                      1
#define configUSE_NEWLIB_REENTRANT                                  0
#define configENABLE_BACKWARD_COMPATIBILITY                         0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS                     1
#define configUSE_MINI_LIST_ITEM                                    1
#define configSTACK_DEPTH_TYPE                                      uint16_t
#define configMESSAGE_BUFFER_LENGTH_TYPE                            size_t
#define configHEAP_CLEAR_MEMORY_ON_FREE                             0

/* Memory allocation related definitions. */
#define configSUPPORT_STATIC_ALLOCATION                             0
#define configSUPPORT_DYNAMIC_ALLOCATION                            1
#define configTOTAL_HEAP_SIZE                                       32768
#define configAPPLICATION_ALLOCATED_HEAP                            0
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP                   0

/* Hook function related definitions. */
#define configUSE_IDLE_HOOK                                 0
#define configUSE_TICK_HOOK                                 0
#define configCHECK_FOR_STACK_OVERFLOW                      0
#define configUSE_MALLOC_FAILED_HOOK                        0
#define configUSE_DAEMON_TASK_STARTUP_HOOK                  0
#define configUSE_SB_COMPLETED_CALLBACK                     0

/* Run time and task stats gathering related definitions. */
#define configGENERATE_RUN_TIME_STATS                       0
#define configUSE_TRACE_FACILITY                            1
#define configUSE_STATS_FORMATTING_FUNCTIONS                1

/* Co-routine related definitions. */
#define configUSE_CO_ROUTINES                               0
#define configMAX_CO_ROUTINE_PRIORITIES                     1

/* Software timer related definitions. */
#define configUSE_TIMERS                                    1
#define configTIMER_TASK_PRIORITY                           3
#define configTIMER_QUEUE_LENGTH                            10
#define configTIMER_TASK_STACK_DEPTH                        configMINIMAL_STACK_SIZE

void vAssertCalled(const char *file, uint32_t line);

/* Define to trap errors during development. */
#define configASSERT(x)                                                    \
    do                                                                     \
    {                                                                      \
        if ((x) == 0)                                                      \
        {                                                                  \
            vAssertCalled(__FILE__, (uint32_t)__LINE__);                   \
        }                                                                  \
    } while (0)
/* A header file that defines trace macro can be included here. */

/* Cortex-M specific definitions. */
#define configPRIO_BITS                                     4U
/* The lowest interrupt priority that can be used in a call to a "set priority"
function. */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY             0xF

/* The highest interrupt priority that can be used by any interrupt service
routine that makes calls to interrupt safe FreeRTOS API functions.  DO NOT CALL
INTERRUPT SAFE FREERTOS API FUNCTIONS FROM ANY INTERRUPT THAT HAS A HIGHER
PRIORITY THAN THIS! (higher priorities are lower numeric values. */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY        5

/* Interrupt priorities used by the kernel port layer itself.  These are generic
to all Cortex-M ports, and do not rely on any particular library functions. */
#define configKERNEL_INTERRUPT_PRIORITY                     ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
/* !!!! configMAX_SYSCALL_INTERRUPT_PRIORITY must not be set to zero !!!!
See http://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html. */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY                ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* Definitions that map the FreeRTOS port interrupt handlers to their CMSIS
   standard names. */
#define vPortSVCHandler                                     SVC_Handler
#define xPortPendSVHandler                                  PendSV_Handler

#endif /* FREERTOS_CONFIG_H */
