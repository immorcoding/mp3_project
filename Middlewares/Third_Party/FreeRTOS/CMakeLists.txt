# FreeRTOS：STM32H743 + GCC Cortex-M7 最小内核
add_library(freertos_kernel STATIC)

target_sources(freertos_kernel PRIVATE
    Source/list.c
    Source/queue.c
    Source/tasks.c

    # FreeRTOSConfig.h 当前启用了这三项，因此必须编译
    Source/timers.c
    Source/event_groups.c
    Source/stream_buffer.c

    # STM32H743：Cortex-M7 r0p1 GCC 移植层
    Source/portable/GCC/ARM_CM7/r0p1/port.c

    # 动态内存分配方案
    Source/portable/MemMang/heap_4.c
)

target_include_directories(freertos_kernel PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/Source/include
    ${CMAKE_CURRENT_LIST_DIR}/Source/portable/GCC/ARM_CM7/r0p1
)

# 让 FreeRTOS 编译时继承 STM32H743 的芯片宏、CMSIS 和 HAL 头文件路径
# target_link_libraries(freertos_kernel PRIVATE stm32cubemx)