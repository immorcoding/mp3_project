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

# 导出 compile_commands.json，便于 clangd 等工具索引源码
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# 创建 CMake 工程，并启用 C 语言和汇编语言
project(${CMAKE_PROJECT_NAME} LANGUAGES C ASM)
message("Build type: " ${CMAKE_BUILD_TYPE})

# 创建最终生成 ELF 固件的可执行目标
add_executable(${CMAKE_PROJECT_NAME})

# 加入 STM32CubeMX 生成代码、FreeRTOS 内核和第三方图形库子目录（界面由 Service/gui/view 手写）
add_subdirectory(cmake/stm32cubemx)
add_subdirectory(Middlewares/Third_Party/FreeRTOS)
add_subdirectory(Middlewares/Third_Party/LVGL)

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
    FATFS/Target/bsp_driver_user_diskio.c
    ${FREERTOS_CONFIG_SOURCES}
    ${APP_SOURCES}
)

# 固件版本头：每次构建运行 cmake/fw_version.cmake（git describe --always --dirty），
# 内容不变不覆盖，增量构建不因版本不变而重编。不在 configure 时生成，否则不重新 configure 就会过期。
# 脚本构建与 IDE 构建共用这条路径。头文件以 "generated/fw_version.h" 包含。
set(FW_VERSION_HEADER "${CMAKE_BINARY_DIR}/generated/fw_version.h")
add_custom_target(fw_version
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUTPUT=${FW_VERSION_HEADER}
            -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/fw_version.cmake
    BYPRODUCTS ${FW_VERSION_HEADER}
    COMMENT "正在生成固件版本头"
    VERBATIM
)
add_dependencies(${CMAKE_PROJECT_NAME} fw_version)

# 只把工程根目录加入头文件搜索路径。
# 因此源码使用 "Components/axp2101/axp2101.h" 这类从工程根目录开始的
# 明确完整路径，而不是把每个叶子目录都加入搜索路径后依赖可能重名的短文件名。
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}

    # 在此添加用户自定义的头文件搜索路径
)

# 测试钩子（DIAG-7）：每个场景一个默认关闭的编译开关，代码里写成 if (开关)，默认构建也编译；发布构建不开。
# 板测要开时在 configure 时带上，例如：cmake --preset firmware-debug -DMP3_HOOK_SD_LATE_DMA=1
# （缓存变量会留在构建目录里，关掉要显式 -DMP3_HOOK_SD_LATE_DMA=0）。打开的钩子在启动日志里各有一行 HOOK。
set(MP3_HOOK_SD_LATE_DMA 0 CACHE STRING "迟到 DMA 测试钩子（#209，Service/filesystem）：1 打开，0 关闭（默认）")
if(MP3_HOOK_SD_LATE_DMA AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR "测试钩子不能进发布构建：MP3_HOOK_SD_LATE_DMA 只能在 Debug 构建里打开（当前 ${CMAKE_BUILD_TYPE}）。")
endif()

# 假抖动钩子（#211，APP/tasks/volume）：打开后卷任务每隔 5 s 自己调一次卡检测边沿入口，用于 R3.4；同样只允许 Debug。
# configure 时 cmake --preset firmware-debug -DMP3_HOOK_FAKE_BOUNCE=1，关掉要显式 -DMP3_HOOK_FAKE_BOUNCE=0。
set(MP3_HOOK_FAKE_BOUNCE 0 CACHE STRING "假抖动测试钩子（#211，APP/tasks/volume）：1 打开，0 关闭（默认）")
if(MP3_HOOK_FAKE_BOUNCE AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR "测试钩子不能进发布构建：MP3_HOOK_FAKE_BOUNCE 只能在 Debug 构建里打开（当前 ${CMAKE_BUILD_TYPE}）。")
endif()

# Flash 压力钩子（#213，APP/tasks/background）：打开后后台任务里多一个作业，在 Flash 卷上循环写满 4 MiB 的文件、
# 每块之后回收一次、写满删除，把空闲块压到回收水位以下（清单 1 C2/C3、R6.2 至 R6.4）；同样只允许 Debug。
# configure 时 cmake --preset firmware-debug -DMP3_HOOK_FLASH_PRESSURE=1，关掉要显式 -DMP3_HOOK_FLASH_PRESSURE=0。
set(MP3_HOOK_FLASH_PRESSURE 0 CACHE STRING "Flash 压力测试钩子（#213，APP/tasks/background）：1 打开，0 关闭（默认）")
if(MP3_HOOK_FLASH_PRESSURE AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR "测试钩子不能进发布构建：MP3_HOOK_FLASH_PRESSURE 只能在 Debug 构建里打开（当前 ${CMAKE_BUILD_TYPE}）。")
endif()

# Flash 压力钩子的两个调参（#220，第 6 次上板追加）：不用为每一版固件临时改源码。
#   MP3_HOOK_FLASH_PRESSURE_FREE_FLOOR  空闲块下限：低于它钩子停写、只等后台任务的回收作业抬回来，默认 0（不设下限，一路压到耗尽）；
#                                        300 把空闲块保持在回收水位（本板 462）以下、耗尽以上，擦除全来自回收作业，是产品节拍。
#   MP3_HOOK_FLASH_PRESSURE_FILE_MIB    一个压力文件写满多少 MiB 就关闭并删除，默认 4（16 做 16 MiB 满额删除；Flash 卷逻辑容量约 18.9 MiB）。
# 只允许 Debug（非默认值在发布构建里会在 configure 时报错）；缓存变量会留在构建目录里，要回默认值显式写 0 和 4。
set(MP3_HOOK_FLASH_PRESSURE_FREE_FLOOR 0 CACHE STRING "Flash 压力钩子的空闲块下限（#220）：0 不设（默认）")
set(MP3_HOOK_FLASH_PRESSURE_FILE_MIB 4 CACHE STRING "Flash 压力钩子的压力文件大小，单位 MiB（#220）：默认 4")
if(NOT MP3_HOOK_FLASH_PRESSURE_FREE_FLOOR MATCHES "^[0-9]+$")
    message(FATAL_ERROR "MP3_HOOK_FLASH_PRESSURE_FREE_FLOOR 必须是非负整数（当前 \"${MP3_HOOK_FLASH_PRESSURE_FREE_FLOOR}\"）。")
endif()
if(NOT MP3_HOOK_FLASH_PRESSURE_FILE_MIB MATCHES "^[0-9]+$" OR MP3_HOOK_FLASH_PRESSURE_FILE_MIB LESS 1 OR MP3_HOOK_FLASH_PRESSURE_FILE_MIB GREATER 16)
    message(FATAL_ERROR "MP3_HOOK_FLASH_PRESSURE_FILE_MIB 必须是 1 到 16 的整数（当前 \"${MP3_HOOK_FLASH_PRESSURE_FILE_MIB}\"，Flash 卷逻辑容量约 18.9 MiB）。")
endif()
if((NOT MP3_HOOK_FLASH_PRESSURE_FREE_FLOOR EQUAL 0 OR NOT MP3_HOOK_FLASH_PRESSURE_FILE_MIB EQUAL 4) AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR "测试钩子不能进发布构建：MP3_HOOK_FLASH_PRESSURE_FREE_FLOOR、MP3_HOOK_FLASH_PRESSURE_FILE_MIB 只能在 Debug 构建里改成非默认值（当前 ${CMAKE_BUILD_TYPE}）。")
endif()
math(EXPR MP3_HOOK_FLASH_PRESSURE_FILE_BYTES "${MP3_HOOK_FLASH_PRESSURE_FILE_MIB} * 1024 * 1024")

# 定位符失效钩子（#215，APP/tasks/audio_reader）：打开后音频读取任务在每次开流前把文件定位符清零，Filesystem 判核对不符、
# 走真实的退回按路径打开，用于 R4.4（12000 首同文件夹开最后一首，T_data 5 s 内要能出声）；同样只允许 Debug。
# configure 时 cmake --preset firmware-debug -DMP3_HOOK_LOCATOR_MISS=1，关掉要显式 -DMP3_HOOK_LOCATOR_MISS=0。
set(MP3_HOOK_LOCATOR_MISS 0 CACHE STRING "定位符失效测试钩子（#215，APP/tasks/audio_reader）：1 打开，0 关闭（默认）")
if(MP3_HOOK_LOCATOR_MISS AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR "测试钩子不能进发布构建：MP3_HOOK_LOCATOR_MISS 只能在 Debug 构建里打开（当前 ${CMAKE_BUILD_TYPE}）。")
endif()

# 翻页注入钩子（#218，APP/apps/music）：打开后 GUI 音乐分区每次看到新一首开始播放，就自动写一次翻页需求，让前台任务在块池补满的同时读一窗标签，
# 与音频读取任务争 SD 卷锁；前台任务量块池补满的耗时并记一行日志，用于 R5.4（水位与让路）；同样只允许 Debug。
# configure 时 cmake --preset firmware-debug -DMP3_HOOK_PAGE_TURN=1，关掉要显式 -DMP3_HOOK_PAGE_TURN=0。
set(MP3_HOOK_PAGE_TURN 0 CACHE STRING "翻页注入测试钩子（#218，APP/apps/music）：1 打开，0 关闭（默认）")
if(MP3_HOOK_PAGE_TURN AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR "测试钩子不能进发布构建：MP3_HOOK_PAGE_TURN 只能在 Debug 构建里打开（当前 ${CMAKE_BUILD_TYPE}）。")
endif()

# 添加工程级编译宏
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    FILESYSTEM_HOOK_SD_LATE_DMA=${MP3_HOOK_SD_LATE_DMA}
    VOLUME_HOOK_FAKE_BOUNCE=${MP3_HOOK_FAKE_BOUNCE}
    BACKGROUND_HOOK_FLASH_PRESSURE=${MP3_HOOK_FLASH_PRESSURE}
    BACKGROUND_HOOK_FLASH_PRESSURE_FREE_FLOOR=${MP3_HOOK_FLASH_PRESSURE_FREE_FLOOR}U
    BACKGROUND_HOOK_FLASH_PRESSURE_FILE_BYTES=${MP3_HOOK_FLASH_PRESSURE_FILE_BYTES}U
    AUDIO_READER_HOOK_LOCATOR_MISS=${MP3_HOOK_LOCATOR_MISS}
    MUSIC_HOOK_PAGE_TURN=${MP3_HOOK_PAGE_TURN}
    # 在此添加用户自定义的编译宏
)

# 链接最终固件所依赖的库目标
# cmake/stm32cubemx 用 plain 签名链接 MX_LINK_LIBS，同一目标不能再混用 PRIVATE 关键字
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    # 在此添加用户自定义的库目标
    freertos_kernel
    lvgl
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
