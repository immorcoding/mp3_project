cmake_minimum_required(VERSION 3.22)

project(music_catalog_tests LANGUAGES C)

enable_testing()

add_executable(music_catalog_name_tests
    test_catalog_name.c
    ../../APP/apps/music/catalog/catalog_name.c
)

target_include_directories(music_catalog_name_tests PRIVATE ../..)
target_compile_features(music_catalog_name_tests PRIVATE c_std_11)
target_compile_options(music_catalog_name_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME catalog_name COMMAND music_catalog_name_tests)

# 复制实际 FatFs 配置，仅移除不参与查表算法的 MCU/BSP 头文件依赖；GBK 查表链接真实 cc936.c。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../FATFS/Target/ffconf.h" HOST_FFCONF)
foreach(HEADER main.h stm32h7xx_hal.h bsp_driver_sd.h)
    string(REPLACE "#include \"${HEADER}\"" "" HOST_FFCONF "${HOST_FFCONF}")
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/host_config")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/host_config/ffconf.h" "${HOST_FFCONF}")

add_executable(music_catalog_text_tests
    test_catalog_text.c
    ../../APP/apps/music/catalog/catalog_text.c
    ../../APP/apps/music/catalog/catalog_name.c
    ../../Components/id3_reader/id3_reader.c
    ../../Middlewares/Third_Party/FatFs/src/option/cc936.c
)

target_include_directories(music_catalog_text_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/host_config" ../..
    ../../FATFS/Target ../../Middlewares/Third_Party/FatFs/src)
target_compile_features(music_catalog_text_tests PRIVATE c_std_11)
target_compile_options(music_catalog_text_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
# cc936.c 是 Vendor 代码，不对它启用 -Werror 警告集。
set_source_files_properties(../../Middlewares/Third_Party/FatFs/src/option/cc936.c
    PROPERTIES COMPILE_OPTIONS "-w")

add_test(NAME catalog_text COMMAND music_catalog_text_tests)

# 曲库扫描、发布规则、按文件定位符打开：真实曲库、播放列表、曲目打开与文本、诊断出口；
# Filesystem Service、Log Service、DWT Adapter 与卷状态函数在测试文件里用假实现替换，
# 曲库短锁走共用 FreeRTOS 桩（脚本化“拿不到锁”、观察拿放次数；桩目录放在 ../.. 之前，见 Tests/stubs/freertos/README.md）。
add_executable(music_catalog_open_tests
    test_catalog_open.c
    ../stubs/freertos/freertos_stub.c
    ../../APP/apps/music/music_background.c
    ../../APP/apps/music/catalog/catalog.c
    ../../APP/apps/music/catalog/catalog_sheet.c
    ../../APP/apps/music/catalog/catalog_track_open.c
    ../../APP/apps/music/catalog/catalog_track_text.c
    ../../APP/apps/music/catalog/catalog_track_diag.c
    ../../APP/apps/music/catalog/catalog_text.c
    ../../APP/apps/music/catalog/catalog_name.c
    ../../Components/id3_reader/id3_reader.c
)

target_include_directories(music_catalog_open_tests BEFORE PRIVATE ../stubs/freertos ../..)
target_compile_features(music_catalog_open_tests PRIVATE c_std_11)
target_compile_options(music_catalog_open_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME catalog_open COMMAND music_catalog_open_tests)

# 发布构建（曲库源码带 -DNDEBUG，与固件 Release 一致）：曲库短锁的持锁统计整段编译掉，不投 CATLOCK 日志；发布规则不受开关影响。
# 曲库源码编进 OBJECT 库单独带 -DNDEBUG；测试文件与桩仍用 -UNDEBUG，免得它们自己的 assert 被编掉。
add_library(music_catalog_release_objects OBJECT
    ../../APP/apps/music/catalog/catalog.c
    ../../APP/apps/music/catalog/catalog_sheet.c
    ../../APP/apps/music/catalog/catalog_name.c
)
target_include_directories(music_catalog_release_objects BEFORE PRIVATE ../stubs/freertos ../..)
target_compile_features(music_catalog_release_objects PRIVATE c_std_11)
target_compile_options(music_catalog_release_objects PRIVATE -Wall -Wextra -Werror -O2 -DNDEBUG)

add_executable(music_catalog_release_tests
    test_catalog_release.c
    $<TARGET_OBJECTS:music_catalog_release_objects>
    ../stubs/freertos/freertos_stub.c
)

target_include_directories(music_catalog_release_tests BEFORE PRIVATE ../stubs/freertos ../..)
target_compile_features(music_catalog_release_tests PRIVATE c_std_11)
target_compile_options(music_catalog_release_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME catalog_release COMMAND music_catalog_release_tests)
