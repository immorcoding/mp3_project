# LVGL v9.2.2 静态库。
#
# LVGL 作为第三方 Middleware 只提供图形核心；显示刷新、输入设备和任务调度
# 由上层 Platform/APP 通过 LVGL 的公开 API 装配。本文件不引入 HAL 或具体板级对象。

add_library(lvgl STATIC)

# 当前 lv_conf.h 使用软件渲染，并禁用了 ThorVG 等 C++ 后端；只编译 LVGL C 源。
# CONFIGURE_DEPENDS 使新增或删除 LVGL C 源时自动触发 CMake 重新配置。
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/src/*.c"
)

target_sources(lvgl PRIVATE
    ${LVGL_SOURCES}
)

# src/lv_conf_internal.h 通过 "lv_conf.h" 读取本项目保存的配置。
# 向依赖者公开 LVGL 入口、内部头文件根和配置目录，以支持后续 SquareLine 生成代码
# 直接使用 #include "lvgl.h"。
target_include_directories(lvgl PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_compile_definitions(lvgl PUBLIC
    LV_CONF_INCLUDE_SIMPLE
)

# 当前 LV_USE_OS 为 LV_OS_NONE；仍声明内核依赖，以便后续切换到
# LV_OS_FREERTOS 时 LVGL 自身编译能取得 FreeRTOS 的公开头文件。
target_link_libraries(lvgl PUBLIC
    freertos_kernel
)
