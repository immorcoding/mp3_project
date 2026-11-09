/**
 * @file event_groups.h
 * @brief 主机共用 FreeRTOS 桩：事件组（静态创建、置位、清位、读位、等待），行为由 freertos_stub.h 脚本化。
 */
#ifndef HOST_STUB_EVENT_GROUPS_H
#define HOST_STUB_EVENT_GROUPS_H

#include "FreeRTOS.h"

/* 与 32 位 tick 的产品配置一致：位 0 至 23 可用。 */
typedef uint32_t EventBits_t;

/* 静态版本的缓冲区；桩把自己的状态放进去，大小由 freertos_stub.c 静态断言保证。 */
typedef struct
{
    void *opaque[8];
} StaticEventGroup_t;

typedef StaticEventGroup_t *EventGroupHandle_t;

EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *buffer);
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits_to_set);
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits_to_clear);
EventBits_t xEventGroupGetBits(EventGroupHandle_t group);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group,
                                EventBits_t bits_to_wait_for,
                                BaseType_t clear_on_exit,
                                BaseType_t wait_for_all_bits,
                                TickType_t ticks_to_wait);

#endif
