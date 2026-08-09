cmake_minimum_required(VERSION 3.22)

#
# 本文件只会生成一次；
# 即使多次运行转换器，也不会重新生成本文件。
#
# 用户可以根据工程需要自由修改本文件。
#

# 配置 C 语言编译标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)



# 未由外部指定构建类型时，默认使用 Debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# 设置工程名称
set(CMAKE_PROJECT_NAME mp3_project)

# 引入 ARM GCC 交叉编译工具链文件
include("cmake/gcc-arm-none-eabi.cmake")

# 导出 compile_commands.json，便于 clangd 等工具索引源码
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# 创建 CMake 工程，并启用 C 语言和汇编语言
project(${CMAKE_PROJECT_NAME} LANGUAGES C ASM)
message("Build type: " ${CMAKE_BUILD_TYPE})

# 创建最终生成 ELF 固件的可执行目标
add_executable(${CMAKE_PROJECT_NAME})

# 加入 STM32CubeMX 生成代码和 FreeRTOS 内核子目录
add_subdirectory(cmake/stm32cubemx)
add_subdirectory(Middlewares/Third_Party/FreeRTOS)

# 配置链接器搜索库文件的目录
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # 在此添加用户自定义的库文件搜索路径
)

# 递归收集可复用且与具体硬件无关的 Component 源文件。
# CONFIGURE_DEPENDS 会让 CMake 在匹配的 .c 文件增加或删除时自动重新配置。
# 临时或测试用的 .c 文件不能放在这些目录中，否则它们也会被自动编入固件。
file(GLOB_RECURSE COMPONENT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Components/*.c"
)
list(JOIN COMPONENT_SOURCES "\n" COMPONENT_SOURCES_MESSAGE)
message(STATUS "Components Sources:\n" ${COMPONENT_SOURCES_MESSAGE})

# 收集 Component 所定义接口的硬件或厂商相关 Adapter 实现
file(GLOB_RECURSE ADAPTER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Adapters/*.c"
)
list(JOIN ADAPTER_SOURCES "\n" ADAPTER_SOURCES_MESSAGE)
message(STATUS "Adapters Sources:\n" ${ADAPTER_SOURCES_MESSAGE})

# 收集当前产品/PCB 的对象装配代码以及面向产品的硬件能力
file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/*.c"
)
list(JOIN PLATFORM_SOURCES "\n" PLATFORM_SOURCES_MESSAGE)
message(STATUS "Platform Sources:\n" ${PLATFORM_SOURCES_MESSAGE})

# 收集 CubeMX USB Device 连接代码。
# 这些文件仍由生成器管理；自定义代码必须写在 CubeMX 的 USER CODE 区域内，
# 这样重新生成工程时才能保留。
file(GLOB_RECURSE USB_DEVICE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/USB_DEVICE/App/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/USB_DEVICE/Target/*.c"
)
list(JOIN USB_DEVICE_SOURCES "\n" USB_DEVICE_SOURCES_MESSAGE)
message(STATUS "USB Device Sources:\n" ${USB_DEVICE_SOURCES_MESSAGE})

# app
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/APP/*.c"
)
list(JOIN APP_SOURCES "\n" APP_SOURCES_MESSAGE)
message(STATUS "APP Sources:\n" ${APP_SOURCES_MESSAGE})

# Service
file(GLOB_RECURSE SERVICE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Service/*.c"
)
list(JOIN SERVICE_SOURCES "\n" SERVICE_SOURCES_MESSAGE)
message(STATUS "Service Sources:\n" ${SERVICE_SOURCES_MESSAGE})

# FreeRTOS 内核源码保留在 Middlewares/Third_Party/FreeRTOS/Source；与本项目
# 相关的配置、Hook 和断言处理放在同级 Config，避免把它们误归入 HAL Adapter。
file(GLOB_RECURSE FREERTOS_CONFIG_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Config/*.c"
)
list(JOIN FREERTOS_CONFIG_SOURCES "\n" FREERTOS_CONFIG_SOURCES_MESSAGE)
message(STATUS "FreeRTOS Config Sources:\n" ${FREERTOS_CONFIG_SOURCES_MESSAGE})

# 将上面收集的自维护源文件以及显式指定的应用入口加入最终固件目标
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${COMPONENT_SOURCES}
    ${ADAPTER_SOURCES}
    ${PLATFORM_SOURCES}
    ${USB_DEVICE_SOURCES}
    ${SERVICE_SOURCES}
    ${FREERTOS_CONFIG_SOURCES}
    ${APP_SOURCES}
)

# 只把工程根目录加入头文件搜索路径。
# 因此源码使用 "Components/axp2101/axp2101.h" 这类从工程根目录开始的
# 明确完整路径，而不是把每个叶子目录都加入搜索路径后依赖可能重名的短文件名。
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}

    # 在此添加用户自定义的头文件搜索路径
)

# 添加工程级编译宏
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # 在此添加用户自定义的编译宏
)

# 链接最终固件所依赖的库目标
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    stm32cubemx
    # 在此添加用户自定义的库目标
    freertos_kernel
)

# 链接完成后，将 ELF 转换为常见烧录方式使用的文件格式：
# - Intel HEX 会保留地址信息；
# - 原始 BIN 只包含二进制数据，烧录时需要在外部指定 Flash 起始地址。
# $<TARGET_FILE...> 生成器表达式可自动取得目标文件路径，因此适用于所有构建预设。
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.hex

    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.bin

    COMMENT "正在生成 HEX 和 BIN 文件"
    VERBATIM
)
